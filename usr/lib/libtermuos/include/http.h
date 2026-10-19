#pragma once
#include <stdint.h>
#include <stddef.h>

int http_get(const uint8_t ip[4], uint16_t port,
             const char *path, char *out, size_t out_max);
const char *http_body(const char *buf);
