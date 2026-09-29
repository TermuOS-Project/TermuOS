#include <stdio.h>
#include <stdlib.h>
#include <string.h>

int main(void)
{
    printf("hello from tlibc %d\n", 42);
    char *p = malloc(32);
    if (p) {
        strcpy(p, "heap ok");
        printf("%s\n", p);
        free(p);
    }
    return 0;
}
