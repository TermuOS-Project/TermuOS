#include <stdio.h>
#include "mbedtls/version.h"

int main(void)
{
    char buf[64];
    mbedtls_version_get_string_full(buf);
    printf("mbedtls: %s\n", buf);
    return 0;
}