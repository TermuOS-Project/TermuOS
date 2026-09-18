#include <unistd.h>
#include <string.h>
#include <syscall.h>

#define DRIVER_NAME_MAX 32

struct driver_info {
    char name[DRIVER_NAME_MAX];
    int priority;
    int status;
};

static void put_int(int v)
{
    char buf[16];
    int i = 0;
    int neg = 0;

    if (v < 0) {
        neg = 1;
        v = -v;
    }
    if (v == 0) {
        write(1, "0", 1);
        return;
    }
    while (v > 0 && i < 15) {
        buf[i++] = (char)('0' + (v % 10));
        v /= 10;
    }
    if (neg)
        write(1, "-", 1);
    while (i > 0)
        write(1, &buf[--i], 1);
}

int main(int argc, char **argv)
{
    struct driver_info list[64];
    long n;
    long i;

    (void)argc;
    (void)argv;

    n = __syscall2(SYS_LSDRV, (long)list, 64);
    if (n < 0) {
        write(1, "lsdrv: failed\n", 14);
        return 1;
    }

    write(1, "NAME                             PRIO  STATUS\n", 46);
    write(1, "----                             ----  ------\n", 46);

    for (i = 0; i < n; i++) {
        int len = (int)strlen(list[i].name);
        int p;

        write(1, list[i].name, (size_t)len);
        for (p = len; p < 32; p++)
            write(1, " ", 1);

        put_int(list[i].priority);
        write(1, "  ", 2);

        if (list[i].status == 0)
            write(1, "ok\n", 3);
        else if (list[i].status == 1)
            write(1, "registered\n", 11);
        else {
            write(1, "fail ", 5);
            put_int(list[i].status);
            write(1, "\n", 1);
        }
    }

    return 0;
}
