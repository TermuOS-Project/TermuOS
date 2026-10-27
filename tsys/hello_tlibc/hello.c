#include <stdio.h>
#include <ob.h>

int main(void)
{
    long h = ob_open("\\Device\\ata0", OB_ACCESS_READ);
    printf("open ata0 -> %d\n", h);
    if (h >= 0) {
        long h2 = ob_dup((int)h);
        printf("dup -> %d\n", h2);
        ob_close((int)h);
        ob_close((int)h2);
    }
    return 0;
}
