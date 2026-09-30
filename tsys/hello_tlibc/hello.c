#include <stdio.h>
#include <stdlib.h>
#include <unistd.h>

int main(void)
{
    void *b0 = brk(0);
    printf("brk0=%p\n", b0);

    char *p = malloc(1024 * 1024);
    if (!p) {
        printf("malloc failed\n");
        return 1;
    }
    p[0] = 'A';
    p[1024 * 1024 - 1] = 'Z';
    printf("malloc 1M ok brk=%p\n", brk(0));
    free(p);
    return 0;
}
