#include "fat32_system_format.h"

#include <stdint.h>
#include <stdlib.h>
#include <string.h>
#include <time.h>
#include <sys/stat.h>

#define DT_DIRECTORY 4
#define DT_REGULAR_FILE 8

#define ATTRIBUTE_READ_ONLY 0x01
#define ATTRIBUTE_HIDDEN 0x02
#define ATTRIBUTE_SYSTEM 0x04
#define ATTRIBUTE_VOLUME_ID 0x08
#define ATTRIBUTE_DIRECTORY 0x10
#define ATTRIBUTE_ARCHIVE 0x20

#define FIRST_USABLE_SECTOR 2048
#define BYTES_PER_SECTOR 512
#define SECTORS_PER_CLUSTER 8
#define TOTAL_SECTORS 4ULL * 1024 * 1024 * 1024 / BYTES_PER_SECTOR
#define FAT_SIZE 8ULL * 1024 * 1024 / BYTES_PER_SECTOR

#define NUMBER_OF_ENTRIES_IN_A_CLUSTERS 4096 / 32

static void *volume_buffer = NULL;
static uint32_t FirstDataSector;
static uint32_t *FATs = NULL;
static uint32_t *MirrorFATs = NULL;

typedef struct _SECTOR
{
    uint8_t sector_buffer[BYTES_PER_SECTOR];
} __attribute__((packed)) SECTOR;

typedef struct _CLUSTER
{
    uint8_t cluster_buffer[BYTES_PER_SECTOR * SECTORS_PER_CLUSTER];
} __attribute__((packed)) CLUSTER;

static SECTOR *volume_sector_buffer = NULL;
static CLUSTER *data_cluster_buffer = NULL;

typedef struct _BIOS_PARAMETER_BLOCK
{
    uint8_t JumpBoot[3];
    char OEMName[8];
    uint16_t BytesPerSector;
    uint8_t SectorsPerCluster;
    uint16_t ReservedSectorsCount;
    uint8_t NumberFATs;
    uint16_t RootEntryCount;
    uint16_t TotalSectors16;
    uint8_t Media;
    uint16_t FATSize16;
    uint16_t SectorsPerTrack;
    uint16_t NumberOfHeads;
    uint32_t HidenSectorsCount;
    uint32_t TotalSectors32;
    uint32_t FATSize32;
    uint16_t Flags;
    uint16_t FSVersion;
    uint32_t RootCluster;
    uint16_t FSInfo;
    uint16_t BackupBootSector;
    uint8_t Reserved[12];
    uint8_t DriverNumber;
    uint8_t Reserved1;
    uint8_t ExtendedBootSignature;
    uint32_t VolumeID;
    char VolumeLabel[11];
    char FileSystemType[8];
    uint8_t BootCode[510 - 90];
    uint16_t BootSignature;
} __attribute__((packed)) BIOS_PARAMETER_BLOCK;

typedef struct _FILE_SECTOR_INFO
{
    uint32_t LeadSignature;
    uint8_t Reserved1[480];
    uint32_t StructureSignature;
    uint32_t FreeCount;
    uint32_t NextFreeCluster;
    uint8_t Reserved2[12];
    uint32_t TrailSignature;
} __attribute__((packed)) FILE_SECTOR_INFO;

static FILE_SECTOR_INFO *FSInfo = NULL;

typedef struct _DIRECTORY_ENTRY
{
    char Name[11];
    uint8_t Attribute;
    uint8_t NTReserved;
    uint8_t CreationTimeTenth;
    uint16_t CreationTime;
    uint16_t CreationDate;
    uint16_t LastAccessDate;
    uint16_t FirstClusterHigh;
    uint16_t WriteTime;
    uint16_t WriteDate;
    uint16_t FirstClusterLow;
    uint32_t FileSize;
} __attribute__((packed)) DIRECTORY_ENTRY;

typedef struct _LONG_DIRECTORY_ENTRY
{
    uint8_t Order;
    char Name1[10];
    uint8_t Attributes;
    uint8_t Type;
    uint8_t Checksum;
    char Name2[12];
    uint16_t FirstClusterLow;
    char Name3[4];
} __attribute__((packed)) LONG_DIRECTORY_ENTRY;

static void _copy_input_directory(const char *inputDirectoryPath, uint32_t parent_directory_cluster);
static uint32_t _make_entry(const char *directory_name, uint32_t first_cluster, uint32_t parent_directory_cluster, bool is_directory, uint32_t file_size);
static void _copy_file_contents(FILE *inputFile, uint32_t first_cluster);
static uint32_t _create_directory_entry(DIRECTORY_ENTRY *directory_entry, const char *name, bool is_directory, uint32_t file_size, uint32_t cluster_number);
static void _create_default_directory_entries(uint32_t cluster, uint32_t parent_directory_cluster);
static uint32_t _get_next_free_cluster(void);
static void _get_time_and_date(uint16_t *outputTime, uint16_t *outputDate);
static void _append_path(const char *currentPath, const char *currentDirectoryName, char *output);
static void _format_name(const char *entryName, char *output);

void init_fat32_file_system(void)
{
    volume_buffer = calloc(TOTAL_SECTORS * BYTES_PER_SECTOR, 1);
    if (NULL == volume_buffer)
    {
        perror("Error allocating volume buffer");
        exit(1);
    }

    volume_sector_buffer = (SECTOR *)volume_buffer;
}

void format_fat32_file_system(void)
{
    BIOS_PARAMETER_BLOCK BiosParamterBlock = {
        .JumpBoot = {0xEB, 0x00, 0x90},
        .OEMName = "MSWIN4.1",
        .BytesPerSector = BYTES_PER_SECTOR,
        .SectorsPerCluster = SECTORS_PER_CLUSTER,
        .ReservedSectorsCount = 32,
        .NumberFATs = 2,
        .RootEntryCount = 0,
        .TotalSectors16 = 0,
        .Media = 0xF0,
        .FATSize16 = 0,
        .SectorsPerTrack = 0,
        .NumberOfHeads = 0,
        .HidenSectorsCount = FIRST_USABLE_SECTOR,
        .TotalSectors32 = TOTAL_SECTORS,
        .FATSize32 = 0,
        .Flags = 0,
        .FSVersion = 0x0000,
        .RootCluster = 2,
        .FSInfo = 1,
        .BackupBootSector = 6,
        .Reserved = {0},
        .DriverNumber = 0x80,
        .Reserved1 = 0x00,
        .ExtendedBootSignature = 0x29,
        .VolumeID = 0x12348888,
        .VolumeLabel = "NO NAME    ",
        .FileSystemType = "FAT32   ",
        .BootCode = {0},
        .BootSignature = 0xAA55,
    };

    uint32_t DiskSizeInSectors = 16777216; // Disks up to 8GB with 4k clusters
    uint32_t TempVal1 = DiskSizeInSectors - BiosParamterBlock.ReservedSectorsCount;
    uint32_t TempVal2 = (256 * BiosParamterBlock.SectorsPerCluster + BiosParamterBlock.NumberFATs) / 2;
    BiosParamterBlock.FATSize32 = (TempVal1 + TempVal2 - 1) / TempVal2; // 8 MiB of FATs => 2 * 1024 * 1024 of addressable clusters.

    uint32_t DataSectors = BiosParamterBlock.TotalSectors32 - BiosParamterBlock.ReservedSectorsCount - BiosParamterBlock.FATSize32 * BiosParamterBlock.NumberFATs;
    uint32_t ClusterCount = DataSectors / SECTORS_PER_CLUSTER;

    FILE_SECTOR_INFO FileSectorInfo = {
        .LeadSignature = 0x41615252,
        .Reserved1 = {0},
        .StructureSignature = 0x61417272,
        .FreeCount = ClusterCount - 1,
        .NextFreeCluster = 3,
        .Reserved2 = {0},
        .TrailSignature = 0xAA550000,
    };

    memcpy(volume_sector_buffer[0].sector_buffer, &BiosParamterBlock, sizeof(BiosParamterBlock));
    memcpy(volume_sector_buffer[1].sector_buffer, &FileSectorInfo, sizeof(FileSectorInfo));

    BIOS_PARAMETER_BLOCK BackupBiosParamterBlock = BiosParamterBlock;
    FILE_SECTOR_INFO BackupFileSectorInfo = FileSectorInfo;

    memcpy(volume_sector_buffer[BiosParamterBlock.BackupBootSector].sector_buffer, &BackupBiosParamterBlock, sizeof(BackupBiosParamterBlock));
    memcpy(volume_sector_buffer[BiosParamterBlock.BackupBootSector + 1].sector_buffer, &BackupFileSectorInfo, sizeof(BackupFileSectorInfo));

    FSInfo = (FILE_SECTOR_INFO *)(volume_sector_buffer + 1);

    FATs = (uint32_t *)(volume_sector_buffer + BiosParamterBlock.ReservedSectorsCount);
    MirrorFATs = (uint32_t *)(volume_sector_buffer + BiosParamterBlock.ReservedSectorsCount + BiosParamterBlock.FATSize32);

    FATs[0] = 0x0FFFFFF0;
    FATs[1] = 0x0FFFFFFF;
    FATs[2] = 0x0FFFFFFF;

    MirrorFATs[0] = 0x0FFFFFF0;
    MirrorFATs[1] = 0x0FFFFFFF;
    MirrorFATs[2] = 0x0FFFFFFF;

    FirstDataSector = BiosParamterBlock.ReservedSectorsCount + BiosParamterBlock.FATSize32 * BiosParamterBlock.NumberFATs;
    data_cluster_buffer = (CLUSTER *)(volume_sector_buffer + FirstDataSector);
}

void copy_input_directory(const char *inputDirectoryPath)
{
    _copy_input_directory(inputDirectoryPath, 2);
}

void write_fat32_file_system(FILE *outputFile)
{
    fwrite(volume_buffer, BYTES_PER_SECTOR, TOTAL_SECTORS, outputFile);
}

static void _copy_input_directory(const char *inputDirectoryPath, uint32_t parent_directory_cluster)
{
    DIR *inputDirectory = opendir(inputDirectoryPath);
    if (NULL == inputDirectory) {
        fprintf(stderr, "Can not open directory %s", inputDirectoryPath);
        exit(1);
    }
    struct dirent *directory_entry = NULL;

    while (NULL != (directory_entry = readdir(inputDirectory)))
    {
        if (0 != strcmp(directory_entry->d_name, ".") && 0 != strcmp(directory_entry->d_name, ".."))
        {
            char newPath[1024] = {0};
            char entryName[1024] = {0};
            uint32_t cluster_number;
            _append_path(inputDirectoryPath, directory_entry->d_name, newPath);
            _format_name(directory_entry->d_name, entryName);

            printf("Adding entry: %s\n", newPath);

            if (DT_DIRECTORY == directory_entry->d_type)
            {
                cluster_number = _make_entry(entryName, parent_directory_cluster, parent_directory_cluster, true, 0);
                _copy_input_directory(newPath, cluster_number);
            }
            else if (DT_REGULAR_FILE == directory_entry->d_type)
            {
                FILE *inputFile = fopen(newPath, "rb");
                fseek(inputFile, 0L, SEEK_END);
                uint32_t file_size = ftell(inputFile);
                fseek(inputFile, 0L, SEEK_SET);

                cluster_number = _make_entry(entryName, parent_directory_cluster, parent_directory_cluster, false, file_size);
                _copy_file_contents(inputFile, cluster_number);
            }
            else
            {
                fprintf(stderr, "Skipped file: %s file type unkown", newPath);
            }
        }
    }

    closedir(inputDirectory);
}

// Returns the cluster number for the entry that was created/found
static uint32_t _make_entry(const char *directory_name, uint32_t first_cluster, uint32_t parent_directory_cluster, bool is_directory, uint32_t file_size)
{
    DIRECTORY_ENTRY *DirectoryEntries = (DIRECTORY_ENTRY *)(data_cluster_buffer + first_cluster - 2);

    for (uint32_t i = 0; i < NUMBER_OF_ENTRIES_IN_A_CLUSTERS; ++i)
    {
        if (DirectoryEntries[i].Name[0] == 0x00)
        {
            // The directory was not found
            // Creating new directory
            uint32_t cluster_number = _get_next_free_cluster();
            FATs[cluster_number] = 0x0FFFFFFF;
            MirrorFATs[cluster_number] = 0x0FFFFFFF;

            _create_directory_entry(&DirectoryEntries[i], directory_name, is_directory, file_size, cluster_number);
            if (is_directory) {
                _create_default_directory_entries(cluster_number, parent_directory_cluster);
            }
            return cluster_number;
        }

        if (0 == memcmp(DirectoryEntries[i].Name, directory_name, sizeof(DirectoryEntries[i].Name)))
        {
            uint32_t cluster = DirectoryEntries[i].FirstClusterHigh;
            cluster = cluster << 16;
            cluster = cluster | DirectoryEntries[i].FirstClusterLow;

            return cluster;
        }
    }

    // If this point was reached without return, check if there is a linked cluster if not alocate one and go to that cluster.
    if (FATs[first_cluster] == 0x0FFFFFFF)
    {
        FATs[first_cluster] = _get_next_free_cluster();
        MirrorFATs[first_cluster] = FATs[first_cluster];
        FATs[FATs[first_cluster]] = 0x0FFFFFFF;
        MirrorFATs[FATs[first_cluster]] = 0x0FFFFFFF;
    }

    return _make_entry(directory_name, FATs[first_cluster], parent_directory_cluster, is_directory, file_size);
}

static void _copy_file_contents(FILE *inputFile, uint32_t first_cluster)
{
    FATs[first_cluster] = 0x0FFFFFFF;
    MirrorFATs[first_cluster] = 0x0FFFFFFF;

    if (fread(data_cluster_buffer + first_cluster - 2, 1, sizeof(*data_cluster_buffer), inputFile) == 4096)
    {
        uint32_t next_cluster = _get_next_free_cluster();

        FATs[first_cluster] = next_cluster;
        MirrorFATs[first_cluster] = next_cluster;

        _copy_file_contents(inputFile, next_cluster);
    }
}

static uint32_t _create_directory_entry(DIRECTORY_ENTRY *directory_entry, const char *name, bool is_directory, uint32_t file_size, uint32_t cluster_number)
{
    uint16_t directory_time = 0;
    uint16_t directory_date = 0;
    _get_time_and_date(&directory_time, &directory_date);

    memcpy(directory_entry->Name, name, sizeof(directory_entry->Name));
    directory_entry->Attribute = 0;
    if (is_directory)
    {
        directory_entry->Attribute |= ATTRIBUTE_DIRECTORY;
    }
    directory_entry->NTReserved = 0;
    directory_entry->CreationTimeTenth = 0;
    directory_entry->CreationTime = directory_time;
    directory_entry->CreationDate = directory_date;
    directory_entry->LastAccessDate = directory_date;
    directory_entry->FirstClusterHigh = (cluster_number >> 16) & 0xFFFF;
    directory_entry->WriteDate = directory_time;
    directory_entry->WriteDate = directory_date;
    directory_entry->FirstClusterLow = cluster_number & 0xFFFF;
    directory_entry->FileSize = file_size;

    return cluster_number;
}

static void _create_default_directory_entries(uint32_t cluster, uint32_t parent_directory_cluster)
{
    DIRECTORY_ENTRY *DirectoryEntries = (DIRECTORY_ENTRY *)(data_cluster_buffer + cluster - 2);

    if (2 == parent_directory_cluster) {
        parent_directory_cluster = 0;
    }

    _create_directory_entry(&DirectoryEntries[0], ".          ", true, 0, cluster);
    _create_directory_entry(&DirectoryEntries[1], "..         ", true, 0, parent_directory_cluster);
}

// Since there is no remove file option this is always true.
static uint32_t _get_next_free_cluster(void)
{
    FSInfo->FreeCount--;
    FSInfo->NextFreeCluster++;

    return FSInfo->NextFreeCluster - 1;
}

static void _get_time_and_date(uint16_t *outputTime, uint16_t *outputDate)
{
    time_t current_timestamp = time(NULL);
    struct tm tm = *localtime(&current_timestamp);

    *outputDate = ((tm.tm_year - 80) << 9) | ((tm.tm_mon + 1) << 5) | tm.tm_mday;

    if (tm.tm_sec == 60)
    {
        tm.tm_sec = 59;
    }

    *outputTime = tm.tm_hour << 11 | tm.tm_min << 5 | (tm.tm_sec / 2);
}

static void _append_path(const char *currentPath, const char *currentDirectoryName, char *output)
{
    strcpy(output, currentPath);
    strcat(output, "/");
    strcat(output, currentDirectoryName);
}
static void _format_name(const char *entryName, char *output)
{
    for (uint8_t i = 0; i <= 11; ++i)
    {
        output[i] = ' ';
    }

    for (uint8_t i = 0; i < strlen(entryName); ++i)
    {
        if (entryName[i] == '.')
        {
            uint8_t offset = 11;
            for (uint8_t j = strlen(entryName); j > i; j--)
            {
                output[offset] = entryName[j];
                offset--;
            }

            return;
        }
        output[i] = entryName[i];
    }
}
