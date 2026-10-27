#include "port.h"
#include "../ob/object.h"
#include "../mm/heap.h"
#include "../lib/printf.h"
#include "../sched/scheduler.h"
#include "../proc/process.h"
#include <stdint.h>
#include <stddef.h>

// Small string helpers
static void strcpy_s(char *d, const char *s, int max)
{
  int i = 0;
  while (s[i] && i < max - 1)
  {
    d[i] = s[i];
    i++;
  }
  d[i] = '\0';
}

static int strcmp_s(const char *a, const char *b) __attribute__((unused));
static int strcmp_s(const char *a, const char *b)
{
  while (*a && *b && *a == *b)
  {
    a++;
    b++;
  }
  return *a - *b;
}

// Port storage pool
#define MAX_PORTS 32
port_t port_pool[MAX_PORTS];
uint8_t port_used[MAX_PORTS];

// Initialize the subsystem
void ipc_init(void)
{
  for (int i = 0; i < MAX_PORTS; i++)
    port_used[i] = 0;

  kprintf("ipc: port subsystem ready\n");
}

// Create a port
port_t *port_create(const char *name)
{
  int slot = -1;
  for (int i = 0; i < MAX_PORTS; i++)
    if (!port_used[i])
    {
      slot = i;
      break;
    }
  if (slot < 0)
  {
    kprintf("ipc: port pool full\n");
  }

  port_t *p = &port_pool[slot];
  port_used[slot] = 1;

  strcpy_s(p->name, name, PORT_NAME_MAX);
  p->head = 0;
  p->tail = 0;
  p->count = 0;
  p->waiter_count = 0;

  // Register the port in the namespace under \Port\<name>.
  char path[64];
  const char *pre = "\\Port\\";
  int pi = 0;
  while (pre[pi])
  {
    path[pi] = pre[pi];
    pi++;
  }
  int ni = 0;
  while (name[ni] && pi < 63)
  {
    path[pi++] = name[ni++];
  }
  path[pi] = '\0';

  p->ob_header = ob_create(&ObTypePort, name, p);
  ob_insert(path, p->ob_header);

  kprintf("ipc: created port '\\Port\\%s'\n", name);
  return p;
}

// Look up a port
port_t *port_find(const char *name)
{
  char path[64];
  const char *pre = "\\Port\\";
  int pi = 0;
  while (pre[pi])
  {
    path[pi] = pre[pi];
    pi++;
  }
  int ni = 0;
  while (name[ni] && pi < 63)
  {
    path[pi++] = name[ni++];
  }
  path[pi] = '\0';

  object_header_t *obj = ob_lookup(path);
  if (!obj || obj->type != &ObTypePort)
    return NULL;
  return (port_t *)obj->body;
}

// Send a message
int port_send(port_t *port, uint32_t code, void *data, uint32_t length)
{
  if (!port)
    return -1;

  // Wait if the queue is full for now; proper sleeping can come later.
  int spins = 0;
  while (port->count >= PORT_QUEUE_SIZE)
  {
    if (++spins > 10000000)
    {
      kprintf("ipc: port '%s' queue full, send dropped\n");
      return -1;
    }
  }

  // Copy the payload onto the heap so the caller can free their buffer later.
  void *payload = NULL;
  if (data && length > 0)
  {
    payload = kmalloc(length);
    if (!payload)
    {
      kprintf("ipc: send OOM\n");
      return -1;
    }
    uint8_t *src = (uint8_t *)data;
    uint8_t *dst = (uint8_t *)payload;
    for (uint32_t i = 0; i < length; i++)
      dst[i] = src[i];
  }

  ipc_message_t *slot = &port->queue[port->tail];
  slot->sender_pid = thread_current()->owner
                         ? thread_current()->owner->pid
                         : 0;
  slot->code = code;
  slot->data = payload;
  slot->length = length;

  port->tail = (port->tail + 1) % PORT_QUEUE_SIZE;
  port->count++;

  kprintf("ipc: sent msg code=%u to port '%s' (queued=%u)\n", code, port->name, port->count);

  // Wake a waiting thread if one exists.
  if (port->waiter_count > 0)
  {
    thread_t *waiter = port->waiters[0];
    // shift waiter list
    for (uint32_t i = 0; i < port->waiter_count - 1; i++)
      port->waiters[i] = port->waiters[i + 1];
    port->waiter_count--;

    // hand the message directly to the waking thread
    port->pending = *slot;
    port->head = (port->head + 1) % PORT_QUEUE_SIZE;
    port->count--;

    thread_unblock(waiter);
  }

  return 0;
}

// Receive a message
int port_receive(port_t *port, ipc_message_t *out)
{
  if (!port || !out)
    return -1;

  __asm__ volatile("cli");

  if (port->count == 0)
  {
    // No messages are queued, so block this thread.
    if (port->waiter_count >= PORT_QUEUE_SIZE)
    {
      __asm__ volatile("sti");
      kprintf("ipc: too many waiters on port '%s'\n", port->name);
      return -1;
    }
    port->waiters[port->waiter_count++] = thread_current();
    __asm__ volatile("sti");
    thread_block(); // yeilds; resumes when port_send unblocks us

    // when we wake, pending holds our message
    *out = port->pending;
    return 0;
  }

  // A message is ready, so dequeue it directly.
  *out = port->queue[port->head];
  port->head = (port->head + 1) % PORT_QUEUE_SIZE;
  port->count--;

  __asm__ volatile("sti");
  return 0;
}

int port_try_receive(port_t *port, ipc_message_t *out)
{
  if (!port || !out)
    return -1;

  __asm__ volatile("cli");

  if (port->count == 0) {
    __asm__ volatile("sti");
    return -1;
  }

  *out = port->queue[port->head];
  port->head = (port->head + 1) % PORT_QUEUE_SIZE;
  port->count--;

  __asm__ volatile("sti");
  return 0;
}

// Destroy a port
void port_destroy(port_t *port)
{
  if (!port)
    return;

  // Free any queued message payloads.
  while (port->count > 0)
  {
    ipc_message_t *m = &port->queue[port->head];
    if (m->data)
      kfree(m->data);
    port->head = (port->head + 1) % PORT_QUEUE_SIZE;
    port->count--;
  }

  ob_deref(port->ob_header);

  int idx = (int)(port - port_pool);
  if (idx >= 0 && idx < MAX_PORTS)
    port_used[idx] = 0;

  kprintf("ipc: port '%s' destroyed\n", port->name);
}
