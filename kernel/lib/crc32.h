#pragma once
#include "Kernel.h"

uint32_t crc32(const void *data, size_t len);
void crc32_selftest(void);
