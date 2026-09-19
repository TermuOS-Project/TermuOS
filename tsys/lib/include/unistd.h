#pragma once

#include <stddef.h>
#include <syscall.h>

struct mouse_state {
    int x, y;
    unsigned char buttons;
    signed char dx, dy;
};

ssize_t read(int fd, void *buf, size_t n);
ssize_t write(int fd, const void *buf, size_t n);
int open(const char *path, int flags, ...);
int close(int fd);
void _exit(int code);
void exit(int code);

int kbd_haschar(void);
int kbd_getchar(void); /* -1 if none */
int mouse_get_state(struct mouse_state *out);
