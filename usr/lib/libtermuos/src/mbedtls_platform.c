#include <stddef.h>

void explicit_bzero(void *buf, size_t len)
{
    volatile unsigned char *p = (volatile unsigned char *)buf;
    while (len--)
        *p++ = 0;
}

void *mbedtls_calloc(size_t n, size_t size)
{
    size_t t = n * size;
    void *p = malloc(t);
    if (p) {
        unsigned char *b = (unsigned char *)p;
        for (size_t i = 0; i < t; i++)
            b[i] = 0;
    }
    return p;
}

void mbedtls_free(void *p)
{
    free(p);
}

int mbedtls_hardware_poll(void *data, unsigned char *output,
                          size_t len, size_t *olen)
{
    (void)data;
    for (size_t i = 0; i < len; i++)
        output[i] = (unsigned char)(0xA5 ^ (unsigned char)i);
    if (olen)
        *olen = len;
    return 0;
}
