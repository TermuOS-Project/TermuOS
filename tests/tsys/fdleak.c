/*
 * fdleak - opens files and exits without closing them (issue #43).
 *
 * Run before opentest: if exiting leaks descriptors, opentest's "first
 * descriptor is 3" check fails and the leak is visible.
 */
#include <unistd.h>
#include <syscall.h>
#include <string.h>

int main(void)
{
    int i, fd, last = -1;
    for (i = 0; i < 8; i++)
    {
        fd = open("/etc/motd", O_RDONLY);
        if (fd < 0)
            break;
        last = fd;
    }
    write(1, "fdleak: opened without closing\n", 31);
    (void)last;
    return 0;
}
