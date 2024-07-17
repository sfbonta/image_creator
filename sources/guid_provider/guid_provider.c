#include "guid_provider.h"

#include <stdio.h>


void get_guid(uint8_t guid[16])
{
    FILE* Urandom = fopen("/dev/urandom", "rb");
    if (NULL != Urandom)
    {
        fread(guid, 1, 16, Urandom);
        fclose(Urandom);
    }
}
