#include "net.h"
#include <syscall.h>

#ifndef SYS_TCP_CONNECT
#define SYS_TCP_CONNECT 70
#define SYS_TCP_SEND 71
#define SYS_TCP_RECV 72
#define SYS_TCP_CLOSE 73
#endif

int tcp_connect_ip(const uint8_t ip[4], uint16_t port, uint16_t local_port)
{
    return (int)__syscall3(SYS_TCP_CONNECT, (long)ip, (long)port, (long)local_port);
}

int tcp_send(const void *buf, size_t len)
{
    return (int)__syscall2(SYS_TCP_SEND, (long)buf, (long)len);
}

int tcp_recv(void *buf, size_t max)
{
    return (int)__syscall2(SYS_TCP_RECV, (long)buf, (long)max);
}

int tcp_close(void)
{
    return (int)__syscall0(SYS_TCP_CLOSE);
}
