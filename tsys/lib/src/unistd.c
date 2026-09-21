#include <unistd.h>
#include <syscall.h>

ssize_t read(int fd, void *buf, size_t n)
{
    return (ssize_t)__syscall3(SYS_READ, fd, (long)buf, (long)n);
}

ssize_t write(int fd, const void *buf, size_t n)
{
    return (ssize_t)__syscall3(SYS_WRITE, fd, (long)buf, (long)n);
}

int open(const char *path, int flags, ...)
{
    /* mode ignored for now */
    return (int)__syscall3(SYS_OPEN, (long)path, flags, 0);
}

int close(int fd)
{
    return (int)__syscall1(SYS_CLOSE, fd);
}

void _exit(int code)
{
    __syscall1(SYS_EXIT, code);
    for (;;)
        ;
}

void exit(int code)
{
    _exit(code);
}
