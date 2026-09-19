#pragma once

#define SYS_READ 0
#define SYS_WRITE 1
#define SYS_OPEN 2
#define SYS_CLOSE 3
#define SYS_FB_INFO 50
#define SYS_FB_CLEAR 51
#define SYS_FB_FILL_RECT 52
#define SYS_FB_PUTPIXEL 53
#define SYS_KBD_HASCHAR     54
#define SYS_KBD_GETCHAR     55
#define SYS_MOUSE_GET_STATE 56
#define SYS_EXIT 60
#define SYS_LSDRV 501

/* Must match kernel/fs/vfs.h — these go straight to vfs_open(). They are
 * not the Linux values: this kernel treats the access mode as flag bits. */
#define O_RDONLY 0x01
#define O_WRONLY 0x02
#define O_RDWR 0x03
#define O_CREAT 0x04
#define O_TRUNC 0x08
#define O_APPEND 0x10

long __syscall0(long n);
long __syscall1(long n, long a);
long __syscall2(long n, long a, long b);
long __syscall3(long n, long a, long b, long c);
long __syscall5(long n, long a, long b, long c, long d, long e);
