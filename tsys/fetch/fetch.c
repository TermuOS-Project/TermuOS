#include <stdio.h>
#include <string.h>
#include <fb.h>
#include <syscall.h>

#define DRIVER_NAME_MAX 32

struct driver_info {
    char name[DRIVER_NAME_MAX];
    int priority;
    int status;
};

#ifndef SYS_UPTIME
#define SYS_UPTIME 201
#endif

static const char *logo[] = {
    "  _______                ",
    " |__   __|               ",
    "    | | ___ _ __ _ __ ___  _   _ ",
    "    | |/ _ \\ '__| '_ ` _ \\| | | |",
    "    | |  __/ |  | | | | | | |_| |",
    "    |_|\\___|_|  |_| |_| |_|\\__,_|",
    "         O S                 ",
    "                             ",
};

#define LOGO_LINES 8
#define LOGO_WIDTH 36

static void pad_logo(const char *line)
{
    int n = (int)strlen(line);
    printf("%s", line);
    while (n < LOGO_WIDTH) {
        printf(" ");
        n++;
    }
}

int main(int argc, char **argv)
{
    struct fb_info fb;
    struct driver_info drv[32];
    long ndrv = 0;
    long up = 0;
    int i;
    int has_fb = 0;
    const char *host = "TermuOS";

    (void)argc;
    (void)argv;

    if (fb_info(&fb) == 0 && fb.width > 0)
        has_fb = 1;

    ndrv = __syscall2(SYS_LSDRV, (long)drv, 32);
    if (ndrv < 0)
        ndrv = 0;

    up = __syscall0(SYS_UPTIME);

    for (i = 0; i < LOGO_LINES; i++) {
        pad_logo(logo[i]);

        if (i == 0)
            printf("%s@localhost", host);
        else if (i == 1)
            printf("----------------");
        else if (i == 2)
            printf("OS:     TermuOS 1.0.0");
        else if (i == 3)
            printf("Kernel: TermuOS x86_64");
        else if (i == 4)
            printf("Shell:  shell");
        else if (i == 5)
            printf("Arch:   x86_64");
        else if (i == 6) {
            if (has_fb)
                printf("Res:    %ux%u @ %u bpp",
                       (unsigned)fb.width, (unsigned)fb.height, (unsigned)fb.bpp);
            else
                printf("Res:    (no fb)");
        } else if (i == 7) {
            if (up > 0)
                printf("Uptime: %lu ticks", (unsigned long)up);
            else
                printf("Uptime: n/a");
        }
        printf("\n");
    }

    printf("\nDrivers (%ld):\n", ndrv);
    for (i = 0; i < (int)ndrv; i++) {
        printf("  - %s", drv[i].name);
        if (drv[i].status == 0)
            printf(" (ok)");
        printf("\n");
    }

    printf("\n    #### #### #### ####\n");
    return 0;
}
