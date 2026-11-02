#include "Kernel.h"

bool disk_read(uint32_t lba, uint32_t count, void *buffer)
{
    return ata_read28(lba, (uint8_t)count, buffer);
}

bool disk_write(uint32_t lba, uint32_t count, const void *buffer)
{
    return ata_write28(lba, (uint8_t)count, buffer);
}