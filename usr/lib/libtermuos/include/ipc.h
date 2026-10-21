#pragma once
#include <stdint.h>

#define IPC_INLINE_MAX 64

typedef struct {
    uint32_t sender_pid;
    uint32_t code;
    void *data;
    uint32_t length;
    uint8_t inline_data[IPC_INLINE_MAX];
} ipc_message_user_t;

long port_create(const char *name);
long port_find(const char *name);
long port_send(long idx, uint32_t code, const void *data, uint32_t len);
long port_receive(long idx, ipc_message_user_t *out);
long port_try_receive(long idx, ipc_message_user_t *out);
