#pragma once
#include <stdint.h>

/*
 * Syscall numbers match the Linux x86-64 ABI so musl can be used
 * without modification.
 */
#define SYS_READ 0
#define SYS_WRITE 1
#define SYS_OPEN 2
#define SYS_CLOSE 3
#define SYS_FSTAT 5
#define SYS_MMAP 9
#define SYS_MUNMAP 11
#define SYS_BRK 12
#define SYS_YIELD 24   /* sched_yield */
#define SYS_SLEEP 35   /* nanosleep */
#define SYS_GETPID 39
#define SYS_FB_INFO 50
#define SYS_FB_CLEAR 51
#define SYS_FB_FILL_RECT 52
#define SYS_FB_PUTPIXEL 53
#define SYS_SPAWN 57
#define SYS_EXIT 60
#define SYS_WAIT 61
#define SYS_UPTIME 201
#define SYS_PORT_FIND 300
#define SYS_PORT_SEND 301
#define SYS_PORT_RECEIVE 302
#define SYS_PORT_CREATE 303
#define SYS_LSDRV 501

void syscall_init(void);
uint64_t syscall_dispatch(uint64_t num, uint64_t a, uint64_t b, uint64_t c,
                          uint64_t d, uint64_t e, uint64_t f);
