#include "exec.h"
#include "../mm/pmm.h"
#include "../mm/vmm.h"
#include "../mm/heap.h"
#include "../fs/vfs.h"
#include "../user/uaccess.h"
#include "../lib/printf.h"
#include <stdint.h>
#include <stddef.h>

// ELF64 structures

#define ELF_MAGIC0 0x7f
#define ELF_MAGIC1 'E'
#define ELF_MAGIC2 'L'
#define ELF_MAGIC3 'F'

#define ELFCLASS64 2
#define ET_EXEC 2
#define EM_X86_64 62

#define PT_LOAD 1

#define PF_X (1u << 0) // segment executable
#define PF_W (1u << 1) // segment writable
#define PF_R (1u << 2) // segment readable

typedef struct __attribute__((packed))
{
  uint8_t e_ident[16];
  uint16_t e_type;
  uint16_t e_machine;
  uint32_t e_version;
  uint64_t e_entry;
  uint64_t e_phoff; // program header table offset
  uint64_t e_shoff;
  uint32_t e_flags;
  uint16_t e_ehsize;
  uint16_t e_phentsize;
  uint16_t e_phnum;
  uint16_t e_shentsize;
  uint16_t e_shnum;
  uint16_t e_shstrndx;
} elf64_ehdr_t;

typedef struct __attribute__((packed))
{
  uint32_t p_type;
  uint32_t p_flags;
  uint64_t p_offset; // offset in file
  uint64_t p_vaddr;  // virtual address to load at
  uint64_t p_paddr;  // (ignored for ET_EXEC)
  uint64_t p_filesz; // bytes in file
  uint64_t p_memsz;  // bytes in memory (>= filesz; remainder is BSS)
  uint64_t p_align;
} elf64_phdr_t;

// Internal helpers

static void exec_memset(void *dst, uint8_t val, size_t n)
{
  uint8_t *p = (uint8_t *)dst;
  for (size_t i = 0; i < n; i++)
    p[i] = val;
}

static void exec_memcpy(void *dst, const void *src, size_t n)
{
  uint8_t *d = (uint8_t *)dst;
  const uint8_t *s = (const uint8_t *)src;
  for (size_t i = 0; i < n; i++)
    d[i] = s[i];
}

// Round v up to the next multiple of align. The alignment must be a power of two.
static uint64_t align_up(uint64_t v, uint64_t align)
{
  return (v + align - 1) & ~(align - 1);
}

// Segment mapping
// Map one PT_LOAD segment into pm.
// file_buf holds the whole ELF image in memory, and the function returns 0 on success.

static int map_segment(pagemap_t pm,
                       const elf64_phdr_t *ph,
                       const uint8_t *file_buf,
                       uint64_t file_size)
{
  if (ph->p_memsz == 0)
    return 0;

  // Sanity check: the segment data must stay within the file.
  if (ph->p_filesz > 0 &&
      (ph->p_offset + ph->p_filesz > file_size ||
       ph->p_offset + ph->p_filesz < ph->p_offset)) // overflow guard
  {
    kprintf("exec: segment offset 0x%x+0x%x exceeds file size\n",
            (uint32_t)ph->p_offset,
            (uint32_t)ph->p_filesz);
    return -1;
  }

  // Choose the page-table flags for this segment.
  uint64_t vflags = VMM_PRESENT | VMM_USER;
  if (ph->p_flags & PF_W)
    vflags |= VMM_WRITE;
  if (!(ph->p_flags & PF_X))
    vflags |= VMM_NX;

  uint64_t vaddr_start = ph->p_vaddr & ~(uint64_t)(PAGE_SIZE - 1);
  uint64_t vaddr_end = align_up(ph->p_vaddr + ph->p_memsz, PAGE_SIZE);
  uint64_t num_pages = (vaddr_end - vaddr_start) / PAGE_SIZE;

  for (uint64_t i = 0; i < num_pages; i++)
  {
    void *phys = pmm_alloc();
    if (!phys)
    {
      kprintf("exec: OOM mapping segment page %u\n", (uint32_t)i);
      return -1;
    }

    // Virtual address of this page.
    uint64_t page_vaddr = vaddr_start + i * PAGE_SIZE;

    // Kernel virtual address of the fresh physical page.
    uint8_t *kvaddr = (uint8_t *)PHYS_TO_VIRT(phys);
    exec_memset(kvaddr, 0, PAGE_SIZE);

    // Copy the file data that belongs in this page.
    // [copy_start, copy_end) is the byte range of this page in virtual space
    uint64_t page_start = page_vaddr;
    uint64_t page_end = page_vaddr + PAGE_SIZE;

    // file data covers [ph->p_vaddr, ph->p_vaddr + ph->p_filesz)
    uint64_t data_start = ph->p_vaddr;
    uint64_t data_end = ph->p_vaddr + ph->p_filesz;

    // intersection of page range and file-data range
    uint64_t copy_vstart = page_start > data_start ? page_start : data_start;
    uint64_t copy_vend = page_end < data_end ? page_end : data_end;

    if (copy_vstart < copy_vend)
    {
      uint64_t file_off = ph->p_offset + (copy_vstart - data_start);
      uint64_t dst_off = copy_vstart - page_start;
      uint64_t copy_len = copy_vend - copy_vstart;
      exec_memcpy(kvaddr + dst_off, file_buf + file_off, copy_len);
    }
    // Any bytes between ph->p_filesz and ph->p_memsz are BSS and are already zeroed above.

    vmm_map(pm, page_vaddr, (uint64_t)phys, vflags);
  }

  return 0;
}

// ELF loading

int exec_load(const char *vfs_path, process_t *proc, uint64_t *entry_out)
{
  if (!vfs_path || !proc || !entry_out)
    return -1;

  // Step 1: read the entire file into kernel memory.
  uint32_t ftype;
  uint64_t fsize;
  if (vfs_stat(vfs_path, &ftype, &fsize) < 0 || ftype != VFS_FILE)
  {
    kprintf("exec: not found or not a file: %s\n", vfs_path);
    return -1;
  }
  if (fsize < sizeof(elf64_ehdr_t))
  {
    kprintf("exec: file too small to be an ELF: %s\n", vfs_path);
    return -1;
  }
  // Keep the size reasonable so we do not abuse kmalloc.
  if (fsize > 16 * 1024 * 1024)
  {
    kprintf("exec: ELF too large (%u bytes)\n", (uint32_t)fsize);
    return -1;
  }

  uint8_t *buf = (uint8_t *)kmalloc((size_t)fsize);
  if (!buf)
  {
    kprintf("exec: OOM reading ELF\n");
    return -1;
  }

  int fd = vfs_open(vfs_path, O_RDONLY);
  if (fd < 0)
  {
    kfree(buf);
    kprintf("exec: cannot open %s\n", vfs_path);
    return -1;
  }
  int nread = vfs_read(fd, buf, (size_t)fsize);
  vfs_close(fd);

  if (nread <= 0 || (uint64_t)nread < fsize)
  {
    kfree(buf);
    kprintf("exec: short read on %s (%d of %u bytes)\n",
            vfs_path, nread, (uint32_t)fsize);
    return -1;
  }

  // Step 2: validate the ELF header.
  const elf64_ehdr_t *ehdr = (const elf64_ehdr_t *)buf;

  if (ehdr->e_ident[0] != ELF_MAGIC0 || ehdr->e_ident[1] != ELF_MAGIC1 ||
      ehdr->e_ident[2] != ELF_MAGIC2 || ehdr->e_ident[3] != ELF_MAGIC3)
  {
    kfree(buf);
    kprintf("exec: bad ELF magic in %s\n", vfs_path);
    return -1;
  }
  if (ehdr->e_ident[4] != ELFCLASS64)
  {
    kfree(buf);
    kprintf("exec: not a 64-bit ELF: %s\n", vfs_path);
    return -1;
  }
  if (ehdr->e_type != ET_EXEC)
  {
    kfree(buf);
    kprintf("exec: not an executable ELF (type %u): %s\n",
            ehdr->e_type, vfs_path);
    return -1;
  }
  if (ehdr->e_machine != EM_X86_64)
  {
    kfree(buf);
    kprintf("exec: wrong machine type %u: %s\n",
            ehdr->e_machine, vfs_path);
    return -1;
  }
  if (ehdr->e_phentsize < sizeof(elf64_phdr_t) || ehdr->e_phnum == 0)
  {
    kfree(buf);
    kprintf("exec: no program headers in %s\n", vfs_path);
    return -1;
  }
  if (ehdr->e_phoff + (uint64_t)ehdr->e_phnum * ehdr->e_phentsize > fsize)
  {
    kfree(buf);
    kprintf("exec: program header table out of bounds\n");
    return -1;
  }

  // ── 3. map PT_LOAD segments ───────────────────────────────────────────────
  const uint8_t *phdr_base = buf + ehdr->e_phoff;
  for (uint16_t i = 0; i < ehdr->e_phnum; i++)
  {
    const elf64_phdr_t *ph =
        (const elf64_phdr_t *)(phdr_base + i * ehdr->e_phentsize);

    if (ph->p_type != PT_LOAD)
      continue;

    if (map_segment(proc->pagemap, ph, buf, (uint64_t)fsize) != 0)
    {
      kfree(buf);
      kprintf("exec: failed to map segment %u of %s\n", i, vfs_path);
      return -1;
    }
  }

  // ── 4. allocate user stack ────────────────────────────────────────────────
  uint64_t stack_bottom = EXEC_USER_STACK_TOP - EXEC_USER_STACK_PAGES * PAGE_SIZE;
  for (int i = 0; i < EXEC_USER_STACK_PAGES; i++)
  {
    void *phys = pmm_alloc();
    if (!phys)
    {
      kfree(buf);
      kprintf("exec: OOM allocating user stack\n");
      return -1;
    }
    exec_memset(PHYS_TO_VIRT(phys), 0, PAGE_SIZE);
    vmm_map(proc->pagemap,
            stack_bottom + (uint64_t)i * PAGE_SIZE,
            (uint64_t)phys,
            VMM_PRESENT | VMM_WRITE | VMM_USER | VMM_NX);
  }

  kfree(buf);

  *entry_out = ehdr->e_entry;
  return 0;
}

static int on_user_stack(uint64_t uva)
{
  uint64_t bottom = EXEC_USER_STACK_TOP - EXEC_USER_STACK_PAGES * PAGE_SIZE;
  return uva >= bottom && uva < EXEC_USER_STACK_TOP;
}

static void put_u64_user(process_t *proc, uint64_t uva, uint64_t val)
{
  uint8_t bytes[8];

  if (!on_user_stack(uva) || !on_user_stack(uva + 7))
  {
    kprintf("exec: put_u64 off stack uva=0x%x\n", (uint32_t)uva);
    return;
  }

  for (int i = 0; i < 8; i++)
    bytes[i] = (uint8_t)(val >> (8 * i));

  copy_to_user(proc, uva, bytes, sizeof bytes);
}

static void put_bytes_user(process_t *proc, uint64_t uva, const void *src, size_t n)
{
  copy_to_user(proc, uva, src, n);
}

static size_t exec_strlen(const char *s)
{
  size_t n = 0;
  if (!s)
    return 0;
  while (s[n])
    n++;
  return n;
}

uint64_t exec_setup_user_stack(process_t *proc, int argc, char *const argv[])
{
  uint64_t sp = EXEC_USER_STACK_TOP;
  uint64_t str_ptr[32];
  int i;

  if (!proc)
    return EXEC_USER_STACK_TOP;

  if (argc < 1 || !argv)
    argc = 0;
  if (argc > 32)
    argc = 32;

  for (i = argc - 1; i >= 0; i--)
  {
    const char *s = argv[i] ? argv[i] : "";
    size_t len = 0;
    while (s[len])
      len++;
    sp -= len + 1;
    put_bytes_user(proc, sp, s, len + 1);
    str_ptr[i] = sp;
  }

  sp &= ~0xFULL;

  sp -= 8;
  put_u64_user(proc, sp, 0); /* envp NULL */

  sp -= 8;
  put_u64_user(proc, sp, 0); /* argv NULL */

  for (i = argc - 1; i >= 0; i--)
  {
    sp -= 8;
    put_u64_user(proc, sp, str_ptr[i]);
  }

  sp -= 8;
  put_u64_user(proc, sp, (uint64_t)(uint32_t)argc);

  return sp;
}
