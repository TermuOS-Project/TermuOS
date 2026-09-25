#include <unistd.h>
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

static void puts_(const char *s)
{
    if (s)
        write(1, s, strlen(s));
}

static void put_u(unsigned long v)
{
    char buf[24];
    int i = 0;
    if (v == 0) {
        write(1, "0", 1);
        return;
    }
    while (v > 0 && i < 23) {
        buf[i++] = (char)('0' + (v % 10));
        v /= 10;
    }
    while (i > 0)
        write(1, &buf[--i], 1);
}

static void nl(void) { write(1, "\n", 1); }

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
    puts_(line);
    while (n < LOGO_WIDTH) {
        write(1, " ", 1);
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

    up = __syscall0(SYS_UPTIME); /* ticks if implemented; else ignore */

    /* side-by-side: logo | info */
    for (i = 0; i < LOGO_LINES; i++) {
        pad_logo(logo[i]);

        if (i == 0) {
            puts_(host);
            puts_("@localhost");
        } else if (i == 1) {
            puts_("----------------");
        } else if (i == 2) {
            puts_("OS:     TermuOS 1.0.0");
        } else if (i == 3) {
            puts_("Kernel: TermuOS x86_64");
        } else if (i == 4) {
            puts_("Shell:  shell");
        } else if (i == 5) {
            puts_("Arch:   x86_64");
        } else if (i == 6) {
            if (has_fb) {
                puts_("Res:    ");
                put_u((unsigned long)fb.width);
                puts_("x");
                put_u((unsigned long)fb.height);
                puts_(" @ ");
                put_u((unsigned long)fb.bpp);
                puts_(" bpp");
            } else {
                puts_("Res:    (no fb)");
            }
        } else if (i == 7) {
            puts_("Drivers:");
            put_u((unsigned long)ndrv);
            if (up > 0) {
                puts_("  up:");
                put_u((unsigned long)up);
            }
        }
        nl();
    }

    /* driver names on their own lines */
    if (ndrv > 0) {
        nl();
        puts_("Loaded:");
        nl();
        for (i = 0; i < (int)ndrv; i++) {
            puts_("  - ");
            puts_(drv[i].name);
            if (drv[i].status == 0)
                puts_(" (ok)");
            nl();
        }
    }

    /* colour bars (ASCII) */
    nl();
    puts_("    ");
    //puts_("\033[40m  \033[41m  \033[42m  \033[43m  \033[44m  \033[45m  \033[46m  \033[47m  \033[0m");
    puts_("    #### #### #### ####");
    nl();

    return 0;
}