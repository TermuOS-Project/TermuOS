#include <stddef.h>
#include <stdlib.h>
#include <stdio.h>
#include <string.h>

void *mbedtls_calloc(size_t n, size_t size)
{
    size_t t = n * size;
    void *p = malloc(t);
    if (p)
        memset(p, 0, t);
    return p;
}

void mbedtls_free(void *p)
{
    free(p);
}

void mbedtls_platform_zeroize(void *buf, size_t len)
{
    volatile unsigned char *p = (volatile unsigned char *)buf;
    while (len--)
        *p++ = 0;
}

void explicit_bzero(void *buf, size_t len)
{
    mbedtls_platform_zeroize(buf, len);
}

int mbedtls_hardware_poll(void *data, unsigned char *output, size_t len, size_t *olen)
{
    (void)data;
    for (size_t i = 0; i < len; i++)
        output[i] = (unsigned char)(0xA5u ^ (unsigned char)i ^ (unsigned char)(len + i));
    if (olen)
        *olen = len;
    return 0;
}
