#include <stdio.h>
#include <stdlib.h>

#include "write_image.h"

int main(int argc, const char** argv)
{
    if (argc != 3) {
        perror("Invalid number of parameters.");
        exit(1);
    }

    FILE* outputFile = fopen(argv[2], "wb");
    write_image(argv[1], outputFile);
    return 0;
}
