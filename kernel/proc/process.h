#pragma once
#include <stdint.h>
#include <stddef.h>
#include "../mm/vmm.h"
#include "../ob/object.h"

#define MAX_PROCESSES 32
#define PROCESS_NAME_LEN 32

typedef enum
{
  PROC_DEAD = 0,
  PROC_RUNNING = 1,
  PROC_ZOMBIE = 2,
} proc_state_t;

typedef struct process
{
  uint32_t pid;
  uint32_t ppid;
  char name[PROCESS_NAME_LEN];
  proc_state_t state;
  pagemap_t pagemap;
  int32_t exit_code;
  handle_table_t handles;
  object_header_t *ob_header;
  uint64_t brk;
} process_t;

void proc_init(void);
process_t *proc_create(const char *name);
void proc_exit(process_t *proc, int32_t code);
void proc_list(void);
process_t *proc_get(uint32_t pid);
process_t *proc_kernel(void); // returns pid-0 kernel process
int proc_snapshot(process_t *out, int max_out);
int proc_kill(uint32_t pid);
