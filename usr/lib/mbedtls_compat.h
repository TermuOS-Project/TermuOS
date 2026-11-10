#pragma once
#include <stddef.h>
#include <stdarg.h>
int snprintf(char *buf, size_t n, const char *fmt, ...);
void explicit_bzero(void *buf, size_t len);