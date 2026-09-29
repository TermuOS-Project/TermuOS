#include <stdio.h>
#include <string.h>
#include <syscall.h>

#define DRIVER_NAME_MAX 32

struct driver_info {
    char name[DRIVER_NAME_MAX];
    int priority;
    int status;
};

int main(int argc, char **argv)
{
    struct driver_info list[64];
    long n, i;

    (void)argc;
    (void)argv;

    n = __syscall2(SYS_LSDRV, (long)list, 64);
    if (n < 0) {
        printf("lsdrv: failed\n");
        return 1;
    }

    printf("NAME                             PRIO  STATUS\n");
    printf("----                             ----  ------\n");

    for (i = 0; i < n; i++) {
        const char *st;
        if (list[i].status == 0)
            st = "ok";
        else if (list[i].status == 1)
            st = "registered";
        else
            st = "fail";

        int len = (int)strlen(list[i].name);
        printf("%s", list[i].name);
        for (int p = len; p < 32; p++)
            printf(" ");
        printf("%d  %s\n", list[i].priority, st);
        if (list[i].status != 0 && list[i].status != 1)
            printf(" %d", list[i].status);
        printf("\n");
    }

    return 0;
}
