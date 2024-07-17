#ifndef _FAT32_SYSTEM_FORMAT_H_
#define _FAT32_SYSTEM_FORMAT_H_

#include <stdio.h>
#include <stdbool.h>
#include <dirent.h>

void init_fat32_file_system(void);

void format_fat32_file_system(void);

void copy_input_directory(const char* inputDirectoryPath);

void write_fat32_file_system(FILE *outputFile);

#endif /* _FAT32_SYSTEM_FORMAT_H_ */
