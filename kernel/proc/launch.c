#include "launch.h"
#include "exec.h"
#include "process.h"
#include "../mm/vmm.h"
#include "../mm/heap.h"
#include "../sched/scheduler.h"
#include "../user/userspace.h"
#include "../lib/printf.h"
#include <stdint.h>

static void exec_thread_entry()
{
  thread_t *self = thread_current();
  launch_ctx_t *ctx = (launch_ctx_t *)self->owner->ob_header->body;

  proc_set_perm_mask(ctx->perm_mask);

  vmm_switch(ctx->pagemap);

  jump_userspace(ctx->entry, ctx->stack_top);

  thread_exit();
}

int exec_launch_args(const char *vfs_path, uint32_t perm_mask,
                     int argc, char *const argv[])
{
  if (!vfs_path)
    return -1;

  const char *name = vfs_path;
  for (const char *p = vfs_path; *p; p++)
    if (*p == '/')
      name = p + 1;

  process_t *proc = proc_create(name);
  if (!proc)
  {
    kprintf("exec: could not create process for '%s'\n", vfs_path);
    return -1;
  }

  uint64_t entry = 0;
  if (exec_load(vfs_path, proc, &entry) != 0)
  {
    kprintf("exec: exec_load failed for '%s'\n", vfs_path);
    proc_exit(proc, -1);
    return -1;
  }

  char *default_av[2];
  if (argc < 1 || !argv)
  {
    default_av[0] = (char *)vfs_path;
    default_av[1] = 0;
    argv = default_av;
    argc = 1;
  }

  uint64_t rsp = exec_setup_user_stack(proc, argc, argv);
  // uint64_t rsp = EXEC_USER_STACK_TOP;

  launch_ctx_t *ctx = (launch_ctx_t *)kmalloc(sizeof(launch_ctx_t));
  if (!ctx)
  {
    kprintf("exec: OOM allocating launch ctx\n");
    proc_exit(proc, -1);
    return -1;
  }

  ctx->entry = entry;
  ctx->stack_top = rsp;
  ctx->pagemap = proc->pagemap;
  ctx->perm_mask = perm_mask;

  proc->ob_header->body = ctx;

  thread_t *t = thread_create(name, exec_thread_entry, proc);
  if (!t)
  {
    kprintf("exec: failed to create thread for '%s'\n", name);
    kfree(ctx);
    proc_exit(proc, -1);
    return -1;
  }

  return (int)proc->pid;
}

int exec_launch(const char *vfs_path, uint32_t perm_mask)
{
  char *av[2];
  av[0] = (char *)vfs_path;
  av[1] = 0;
  return exec_launch_args(vfs_path, perm_mask, 1, av);
}

static uint32_t current_perm_mask = 0xffffffff; // kernel: all perms

void proc_set_perm_mask(uint32_t mask)
{
  current_perm_mask = mask;
}

int proc_check_perm(uint32_t perm)
{
  if (current_perm_mask == 0xffffffff)
    return 1; // kernel process
  if (current_perm_mask & perm)
    return 1;
  kprintf("proc: permission denied (needed 0x%x, have 0x%x)\n", perm, current_perm_mask);
  return 0;
}
