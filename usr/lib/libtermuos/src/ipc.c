#include <ipc.h>
#include <syscall.h>

#ifndef SYS_PORT_FIND
#define SYS_PORT_FIND 300
#define SYS_PORT_SEND 301
#define SYS_PORT_RECEIVE 302
#define SYS_PORT_CREATE 303
#define SYS_PORT_TRYRECEIVE 304
#endif

long port_create(const char *name)
{
    return __syscall1(SYS_PORT_CREATE, (long)name);
}

long port_find(const char *name)
{
    return __syscall1(SYS_PORT_FIND, (long)name);
}

long port_send(long idx, uint32_t code, const void *data, uint32_t len)
{
    return __syscall4(SYS_PORT_SEND, idx, (long)code, (long)data, (long)len);
}

long port_receive(long idx, ipc_message_user_t *out)
{
    return __syscall2(SYS_PORT_RECEIVE, idx, (long)out);
}

long port_try_receive(long idx, ipc_message_user_t *out)
{
    return __syscall2(SYS_PORT_TRYRECEIVE, idx, (long)out);
}
