#pragma once
#include <stdint.h>
#include <stddef.h>
#include "../proc/process.h"

// ── exec: ELF64 loader ───────────────────────────────────────────────────────
//
// Loads a static ELF64 executable from the VFS into a process's address space.
//
// What it does:
//   - Validates ELF magic, class (64-bit), machine (x86-64), and type (ET_EXEC)
//   - Maps every PT_LOAD segment into proc->pagemap via vmm_map()
//   - Zeroes the BSS region (memsz > filesz) within each segment
//   - Allocates a user stack at EXEC_USER_STACK_TOP
//
// What it does NOT do:
//   - Dynamic linking / shared libraries (not needed for initial tlib launch)
//   - argv / envp / aux vectors (add later via the stack before jump)
//   - Switch pagemaps or jump to userspace — the caller does that
//
// Usage:
//   uint64_t entry;
//   if (exec_load("/mnt/MyApp.tapp/bin/MyApp", proc, &entry) == 0)
//     jump_userspace(entry, EXEC_USER_STACK_TOP);
// ────────────────────────────────────────────────────────────────────────────

// user stack: 512 KiB at the top of the lower half
#define EXEC_USER_STACK_TOP 0x0000700000000000ULL
#define EXEC_USER_STACK_PAGES 128 // 128 x 4KB = 512 KiB

// Load ELF at `vfs_path` into `proc`'s pagemap.
// On success, writes the entry point to *entry_out and returns 0.
// On failure, returns -1 (process pagemap may be partially populated;
// caller should destroy the process).
int exec_load(const char *vfs_path, process_t *proc, uint64_t *entry_out);
uint64_t exec_setup_user_stack(process_t *proc, int argc, char *const argv[]);