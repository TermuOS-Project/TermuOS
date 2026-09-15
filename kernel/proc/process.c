#include "process.h"
#include "../mm/vmm.h"
#include "../mm/heap.h"
#include "../lib/printf.h"
#include "../ob/object.h"
#include "../fs/vfs.h"
#include <stdint.h>
#include <stddef.h>

static process_t proc_table[MAX_PROCESSES];
static uint32_t next_pid = 0;

void proc_init(void)
{
  for (int i = 0; i < MAX_PROCESSES; i++)
  {
    proc_table[i].state = PROC_DEAD;
    proc_table[i].pid = 0;
  }

  // pid 0 - kernel process, uses the current (active) CR3
  process_t *kproc = &proc_table[0];
  kproc->pid = 0;
  kproc->state = PROC_RUNNING;
  kproc->exit_code = 0;

  uint64_t cr3;
  __asm__ volatile("movq %%cr3, %0" : "=r"(cr3));
  kproc->pagemap = cr3;

  int n = 0;
  const char *kname = "kernel";
  while (kname[n] && n < PROCESS_NAME_LEN - 1)
  {
    kproc->name[n] = kname[n];
    n++;
  }
  kproc->name[n] = '\0';

  next_pid = 1;
  kprintf("proc: kernel process created (pid 0)\n");
  handle_table_init(&kproc->handles);
  kproc->ob_header = ob_create(&ObTypeProcess, "kernel", kproc);
  ob_mkdir("\\Process");
  ob_insert("\\Process\\0", kproc->ob_header);
  __asm__ volatile("mov %%cr3, %0" : "=r"(cr3));
  proc_kernel()->pagemap = cr3 & ~0xFFFULL;
}

process_t *proc_create(const char *name)
{
  // find a free slot
  int slot = -1;
  for (int i = 1; i < MAX_PROCESSES; i++)
  {
    if (proc_table[i].state == PROC_DEAD)
    {
      slot = i;
      break;
    }
  }
  if (slot < 0)
  {
    kprintf("proc: process table full\n");
    return NULL;
  }

  process_t *p = &proc_table[slot];
  p->pid = next_pid++;
  p->state = PROC_RUNNING;
  p->exit_code = 0;
  p->pagemap = vmm_new_pagemap();

  handle_table_init(&p->handles);

  char ob_path[32];
  const char *prefix = "\\Process\\";
  int pi = 0;
  while (prefix[pi])
  {
    ob_path[pi] = prefix[pi];
    pi++;
  }
  uint32_t pid_tmp = p->pid;
  if (pid_tmp == 0)
  {
    ob_path[pi++] = '0';
  }
  else
  {
    char tmp[16];
    int ti = 0;
    while (pid_tmp)
    {
      tmp[ti++] = '0' + (pid_tmp % 10);
      pid_tmp /= 10;
    }
    while (ti > 0)
      ob_path[pi++] = tmp[--ti];
  }
  ob_path[pi] = '\0';

  p->ob_header = ob_create(&ObTypeProcess, ob_path, p);
  ob_insert(ob_path, p->ob_header);

  int n = 0;
  while (name[n] && n < PROCESS_NAME_LEN - 1)
  {
    p->name[n] = name[n];
    n++;
  }
  p->name[n] = '\0';

  kprintf("proc: create process %u '%s'\n", p->pid, p->name);
  return p;
}

void proc_exit(process_t *proc, int32_t code)
{
  if (!proc || proc == proc_kernel())
    return;

  proc->exit_code = code;
  proc->state = PROC_ZOMBIE;
  kprintf("proc: process %u '%s' exited with code %d\n",
          proc->pid, proc->name, code);

  for (int i = 0; i < MAX_HANDLES; i++)
    handle_close(&proc->handles, i);

  /* The VFS descriptor table is global, so anything this process left open
     would hold its slot for the rest of the boot. Take them back. */
  int leaked = vfs_close_all_owned(proc->pid);
  if (leaked > 0)
    kprintf("proc: reclaimed %d open fd(s) from pid %u\n", leaked, proc->pid);

  if (proc->ob_header)
  {
    ob_deref(proc->ob_header);
    proc->ob_header = NULL;
  }
}

void proc_list(void)
{
  kprintf("PID  STATE    NAME\n");
  kprintf("---  -------  ----------------\n");

  for (int i = 0; i < MAX_PROCESSES; i++)
  {
    if (proc_table[i].state == PROC_DEAD)
      continue;

    const char *state = "unknown";
    switch (proc_table[i].state)
    {
    case PROC_RUNNING:
      state = "running";
      break;
    case PROC_ZOMBIE:
      state = "zombie";
      break;
    default:
      state = "dead";
      break;
    }

    kprintf("%u  %s  %s\n",
            proc_table[i].pid,
            state,
            proc_table[i].name);
  }
}

process_t *proc_get(uint32_t pid)
{
  for (int i = 0; i < MAX_PROCESSES; i++)
  {
    if (proc_table[i].pid == pid &&
        proc_table[i].state != PROC_DEAD)
      return &proc_table[i];
  }
  return NULL;
}

process_t *proc_kernel(void)
{
  return &proc_table[0];
}

int proc_snapshot(process_t *out, int max_out)
{
  int n = 0;
  for (int i = 0; i < MAX_PROCESSES && n < max_out; i++)
  {
    if (proc_table[i].state == PROC_DEAD)
      continue;
    out[n++] = proc_table[i];
  }
  return n;
}

int proc_kill(uint32_t pid)
{
  if (pid == 0)
    return -1;
  process_t *p = proc_get(pid);
  if (!p || p->state == PROC_DEAD)
    return -1;
  proc_exit(p, -1);
  return 0;
}
