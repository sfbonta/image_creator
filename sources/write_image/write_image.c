#include "write_image.h"

#include <stdint.h>
#include <uchar.h>

#include "guid_provider.h"
#include "fat32_system_format.h"

#define LBA_SIZE 512
#define ALIGNMENT 1ULL * 1024 * 1024 / LBA_SIZE
#define NUMBER_OF_USABLE_BLOCKS 4ULL * 1024 * 1024 * 1024 / LBA_SIZE
#define NUMBER_OF_BLOCKS ALIGNMENT * 2 + NUMBER_OF_USABLE_BLOCKS
#define SIZE_OF_PARTITION_ENTRY 128

#define EFI_SYSTEM_PARTITION_GUID                                                                      \
    {                                                                                                  \
        0x28, 0x73, 0x2A, 0xC1, 0x1F, 0xF8, 0xD2, 0x11, 0xBA, 0x4B, 0x00, 0xA0, 0xC9, 0x3E, 0xC9, 0x3B \
    }

static uint32_t crc_table[256];

static void create_crc32_table(void);
static uint32_t calculate_crc32(void *buf, int32_t len);

typedef struct _PARTITION_RECORD
{
    uint8_t BootIndicator;
    uint8_t StartingCHS[3];
    uint8_t OsType;
    uint8_t EndingCHS[3];
    uint32_t StartingLBA;
    uint32_t SizeInLBA;
} __attribute__((packed)) PARTITION_RECORD;

typedef struct _PROTECTIVE_MBR
{
    uint8_t BootCode[440];
    uint32_t UniqueMbrDiskSignature;
    uint16_t Unkown;
    PARTITION_RECORD PartitionRecords[4];
    uint16_t Signature;
#if LBA_SIZE > 512
    uint8_t Reserved[LBA_SIZE - 512];
#endif /* LBA_SIZE > 512 */
} __attribute__((packed)) PROTECTIVE_MBR;

typedef struct _GPT_HEADER
{
    uint64_t Signature;
    uint32_t Revision;
    uint32_t HeaderSize;
    uint32_t HeaderCRC32;
    uint32_t Reserved;
    uint64_t MyLBA;
    uint64_t AlternateLBA;
    uint64_t FirstUsableLba;
    uint64_t LastUsableLba;
    uint8_t DiskGUID[16];
    uint64_t PartitionEntryLBA;
    uint32_t NumberOfPartitionEntries;
    uint32_t SizeOfPartitionEntries;
    uint32_t PartitionEntryCRC32;
    uint8_t ReservedPadding[LBA_SIZE - 92];
}__attribute__((packed)) GPT_HEADER;

typedef struct _GPT_ENTRY
{
    uint8_t PartitionTypeGUID[16];
    uint8_t UniquePartitionGUID[16];
    uint64_t StartingLBA;
    uint64_t EndingLBA;
    uint64_t Attributes;
    char16_t PartitionName[36];
#if SIZE_OF_PARTITION_ENTRY > 128
    uint8_t Reserved[SIZE_OF_PARTITION_ENTRY - 128];
#endif /* SIZE_OF_PARTITION_ENTRY > 128 */
} __attribute__((packed)) GPT_ENTRY;

void write_image(const char* inputDirectoryPath, FILE *outputFile)
{
    create_crc32_table();

    PROTECTIVE_MBR ProtectedMbr =
    {
        .BootCode = {0},
        .UniqueMbrDiskSignature = 0x00000000,
        .Unkown = 0x0000,
        .PartitionRecords =
            {
                {
                    .BootIndicator = 0x00,
                    .StartingCHS = {0x00, 0x02, 0x00},
                    .OsType = 0xEE,
                    .EndingCHS = {0xFF, 0xFF, 0xFF},
                    .StartingLBA = 0x00000001,
                    .SizeInLBA = NUMBER_OF_BLOCKS - 1,
                },
                {0},
                {0},
                {0},
            },
        .Signature = 0xAA55,
#if LBA_SIZE > 512
        .Reserved = {0},
#endif /* LBA_SIZE > 512 */
    };

    GPT_ENTRY GptEntryTable[ALIGNMENT * 4 - 8] = {
        {
            .PartitionTypeGUID = EFI_SYSTEM_PARTITION_GUID,
            .UniquePartitionGUID = {0},
            .StartingLBA = ALIGNMENT,
            .EndingLBA = NUMBER_OF_BLOCKS - ALIGNMENT,
            .Attributes = 0,
            .PartitionName = u"BontaOS.hdd1",
#if SIZE_OF_PARTITION_ENTRY > 128
            .Reserved = {0},
#endif
        },
        {0},
    };
    get_guid(GptEntryTable[0].UniquePartitionGUID);

    GPT_HEADER GptHeader =
        {
            .Signature = 0x5452415020494645,
            .Revision = 0x00010000,
            .HeaderSize = 92,
            .HeaderCRC32 = 0,
            .Reserved = 0,
            .MyLBA = 1,
            .AlternateLBA = NUMBER_OF_BLOCKS - 1,
            .FirstUsableLba = ALIGNMENT,
            .LastUsableLba = NUMBER_OF_BLOCKS - ALIGNMENT,
            .DiskGUID = {0},
            .PartitionEntryLBA = 2,
            .NumberOfPartitionEntries = ALIGNMENT * 4 - 8,
            .SizeOfPartitionEntries = SIZE_OF_PARTITION_ENTRY,
            .PartitionEntryCRC32 = 0,
            .ReservedPadding = {0},
        };

    get_guid(GptHeader.DiskGUID);
    GptHeader.PartitionEntryCRC32 = calculate_crc32(GptEntryTable, (ALIGNMENT * 4 - 8) * sizeof(*GptEntryTable));
    GptHeader.HeaderCRC32 = calculate_crc32(&GptHeader, GptHeader.HeaderSize);

    GPT_HEADER BackupGptHeader =
        {
            .Signature = 0x5452415020494645,
            .Revision = 0x00010000,
            .HeaderSize = 92,
            .HeaderCRC32 = 0,
            .Reserved = 0,
            .MyLBA = NUMBER_OF_BLOCKS - 1,
            .AlternateLBA = 1,
            .FirstUsableLba = ALIGNMENT,
            .LastUsableLba = NUMBER_OF_BLOCKS - ALIGNMENT,
            .DiskGUID = {0},
            .PartitionEntryLBA = NUMBER_OF_BLOCKS - ALIGNMENT + 1,
            .NumberOfPartitionEntries = ALIGNMENT * 4 - 8,
            .SizeOfPartitionEntries = SIZE_OF_PARTITION_ENTRY,
            .PartitionEntryCRC32 = 0,
            .ReservedPadding = {0},
        };

    for (uint8_t i = 0; i < 16; ++i)
    {
        BackupGptHeader.DiskGUID[i] = GptHeader.DiskGUID[i];
    }
    BackupGptHeader.PartitionEntryCRC32 = calculate_crc32(GptEntryTable, (ALIGNMENT * 4 - 8) * sizeof(*GptEntryTable));
    BackupGptHeader.HeaderCRC32 = calculate_crc32(&BackupGptHeader, BackupGptHeader.HeaderSize);

    fwrite(&ProtectedMbr, sizeof(ProtectedMbr), 1, outputFile);
    fwrite(&GptHeader, sizeof(GptHeader), 1, outputFile);
    fwrite(GptEntryTable, sizeof(*GptEntryTable), ALIGNMENT * 4 - 8, outputFile);

    init_fat32_file_system();
    format_fat32_file_system();
    copy_input_directory(inputDirectoryPath);
    write_fat32_file_system(outputFile);
    
    uint8_t lba[LBA_SIZE] = {0};
    fwrite(lba, sizeof(lba), 1, outputFile);
    fwrite(GptEntryTable, sizeof(*GptEntryTable), ALIGNMENT * 4 - 8, outputFile);
    fwrite(&BackupGptHeader, sizeof(BackupGptHeader), 1, outputFile);

    fclose(outputFile);
}

static void create_crc32_table(void)
{
    uint32_t c = 0;

    for (int32_t n = 0; n < 256; n++)
    {
        c = (uint32_t)n;
        for (uint8_t k = 0; k < 8; k++)
        {
            if (c & 1)
                c = 0xedb88320L ^ (c >> 1);
            else
                c = c >> 1;
        }
        crc_table[n] = c;
    }
}

static uint32_t calculate_crc32(void *buf, int32_t len)
{
    uint8_t *bufp = buf;
    uint32_t c = 0xFFFFFFFFL;

    for (int32_t n = 0; n < len; n++)
        c = crc_table[(c ^ bufp[n]) & 0xFF] ^ (c >> 8);

    // Invert bits for return value
    return c ^ 0xFFFFFFFFL;
}
