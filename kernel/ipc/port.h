#pragma once
#include <stdint.h>
#include <stddef.h>
#include "../ob/object.h"
#include "../sched/scheduler.h"

#define PORT_QUEUE_SIZE 64
#define PORT_NAME_MAX   32

// message
typedef struct ipc_message
{
  uint32_t sender_pid; // filled by port_send()
  uint32_t code; // caller-defined message type
  void *data; // heap-allocated payload
  uint32_t length; // payload length in bytes
} ipc_message_t;

// port
typedef struct port
{
  char name[PORT_NAME_MAX];
  object_header_t *ob_header;

  // ring buffer
  ipc_message_t queue[PORT_QUEUE_SIZE];
  uint32_t head;
  uint32_t tail;
  uint32_t count;

  // threads blocked waiting to receive
  thread_t *waiters[PORT_QUEUE_SIZE];
  uint32_t waiter_count;

  // received message handed to unblocked thread
  ipc_message_t pending;
} port_t;

extern port_t   port_pool[32];
extern uint8_t  port_used[32];

// API
void ipc_init(void);
port_t *port_create(const char *name);
port_t *port_find(const char *name);
int port_send(port_t *port, uint32_t code, void *data, uint32_t length);
int port_receive(port_t *port, ipc_message_t *out); // blocks if empty
int port_try_receive(port_t *port, ipc_message_t *out);
void port_destroy(port_t *port);

extern object_type_t ObTypePort;
