#include "syscall.h"
#include "uaccess.h"
#include "../arch/x86_64/gdt.h"
#include "../drivers/video/terminal.h"
#include "../drivers/serial/serial.h"
#include "../drivers/video/fb.h"
#include "../drivers/input/keyboard.h"
#include "../drivers/input/mouse.h"
#include "../drivers/driver.h"
#include "../sched/scheduler.h"
#include "../fs/vfs.h"
#include "../mm/pmm.h"
#include "../mm/vmm.h"
#include "../ipc/port.h"
#include "../proc/launch.h"
#include "../proc/process.h"
#include "uaccess.h"
#include "../lib/printf.h"
#include <stdint.h>
#include <stddef.h>

extern volatile uint64_t timer_ticks;
extern void syscall_entry(void);

/* MSR helpers */

static void wrmsr(uint32_t msr, uint64_t val)
{
    __asm__ volatile("wrmsr" ::"c"(msr),
                     "a"((uint32_t)(val & 0xffffffff)),
                     "d"((uint32_t)(val >> 32)));
}

static uint64_t rdmsr(uint32_t msr)
{
    uint32_t lo, hi;
    __asm__ volatile("rdmsr" : "=a"(lo), "=d"(hi) : "c"(msr));
    return ((uint64_t)hi << 32) | lo;
}

#define MSR_EFER 0xC0000080
#define MSR_STAR 0xC0000081
#define MSR_LSTAR 0xC0000082
#define MSR_SFMASK 0xC0000084

static int syscall_supported(void)
{
    uint32_t edx;
    __asm__ volatile(
        "movl $0x80000001, %%eax\n"
        "cpuid\n"
        : "=d"(edx)::"eax", "ebx", "ecx");
    return (edx >> 11) & 1;
}

/* brk */

/*
 * musl uses brk() to manage its heap. We give userspace a 4 MB heap region
 * starting just above the program break.  brk(0) returns current break;
 * brk(addr) sets it.  We back every new page with physical memory on demand.
 */
#define USER_BRK_BASE 0x0000000000400000ULL /* just above musl's top ~0x2b0000 */
#define USER_BRK_MAX 0x0000000010000000ULL

static uint64_t current_brk = 0;

pagemap_t current_user_pm; /* dummy - elf_load removed */

static uint64_t sys_brk(uint64_t addr)
{
    if (addr == 0 || addr < current_brk)
        return current_brk;

    if (addr > USER_BRK_MAX)
        return current_brk; /* refuse; musl will fall back to mmap */

    /* Map the new pages between current_brk and addr. */
    uint64_t start = (current_brk + 0xFFF) & ~0xFFFULL;
    uint64_t end = (addr + 0xFFF) & ~0xFFFULL;

    for (uint64_t page = start; page < end; page += PAGE_SIZE)
    {
        void *phys = pmm_alloc();
        if (!phys)
            return current_brk; /* OOM — return old break */
        vmm_map(current_user_pm, page, (uint64_t)phys,
                VMM_PRESENT | VMM_WRITE | VMM_USER);
    }

    current_brk = addr;
    return current_brk;
}

static process_t *cur_proc(void)
{
    thread_t *t = thread_current();
    return t ? t->owner : 0;
}

/* mmap */

/*
 * musl uses mmap for:
 *   - anonymous memory (malloc fallback, thread stacks)
 *   - mapping files
 *
 * We implement anonymous mmap only (MAP_ANON).  File-backed mmap returns
 * ENOSYS for now; musl falls back to read() in that case.
 *
 * Linux mmap flags we care about:
 */
#define MMAP_PROT_READ 0x1
#define MMAP_PROT_WRITE 0x2
#define MMAP_MAP_PRIVATE 0x2
#define MMAP_MAP_ANON 0x20
#define MMAP_MAP_FIXED 0x10

/* Simple bump allocator for anonymous mmap regions. */
#define MMAP_BASE 0x0000000020000000ULL /* 512 MB */
#define MMAP_MAX 0x0000000080000000ULL  /* 2   GB */
static uint64_t mmap_bump = MMAP_BASE;

static uint64_t sys_mmap(uint64_t addr, uint64_t len, uint64_t prot,
                         uint64_t flags, uint64_t fd, uint64_t off)
{
    (void)prot;
    (void)off;
    (void)fd;

    if (!(flags & MMAP_MAP_ANON))
        return (uint64_t)-1; /* ENOSYS — file mmap not supported */

    if (len == 0)
        return (uint64_t)-1;

    uint64_t pages = (len + PAGE_SIZE - 1) / PAGE_SIZE;
    uint64_t virt;

    if ((flags & MMAP_MAP_FIXED) && addr != 0)
        virt = addr;
    else
    {
        virt = (mmap_bump + 0xFFF) & ~0xFFFULL;
        mmap_bump = virt + pages * PAGE_SIZE;
        if (mmap_bump > MMAP_MAX)
            return (uint64_t)-1;
    }

    for (uint64_t i = 0; i < pages; i++)
    {
        void *phys = pmm_alloc();
        if (!phys)
            return (uint64_t)-1;
        vmm_map(current_user_pm, virt + i * PAGE_SIZE, (uint64_t)phys,
                VMM_PRESENT | VMM_WRITE | VMM_USER);
    }

    return virt;
}

static uint64_t sys_munmap(uint64_t addr, uint64_t len)
{
    /* TODO: free the pages and reclaim the PMM frames. */
    (void)addr;
    (void)len;
    return 0;
}

/* fstat */

/*
 * musl calls fstat on fd 1 (stdout) at startup to decide if it's a tty.
 * We return a minimal stat struct with st_mode = S_IFCHR (character device)
 * for fd 0/1/2, and a regular file stat for everything else.
 *
 * Linux stat64 layout (x86-64):
 */
struct kernel_stat
{
    uint64_t st_dev;
    uint64_t st_ino;
    uint64_t st_nlink;
    uint32_t st_mode;
    uint32_t st_uid;
    uint32_t st_gid;
    uint32_t __pad0;
    uint64_t st_rdev;
    int64_t st_size;
    int64_t st_blksize;
    int64_t st_blocks;
    uint64_t st_atime;
    uint64_t st_atime_nsec;
    uint64_t st_mtime;
    uint64_t st_mtime_nsec;
    uint64_t st_ctime;
    uint64_t st_ctime_nsec;
    int64_t __unused[3];
};

#define S_IFCHR 0020000
#define S_IFREG 0100000

static uint64_t sys_fstat(uint64_t fd, uint64_t buf_addr)
{
    process_t *proc = cur_proc();
    struct kernel_stat st;

    if (!proc)
        return (uint64_t)-1;

    /* Zero the whole struct. */
    uint8_t *p = (uint8_t *)&st;
    for (size_t i = 0; i < sizeof(st); i++)
        p[i] = 0;

    if (fd == 0 || fd == 1 || fd == 2)
    {
        st.st_mode = S_IFCHR | 0620;
        st.st_rdev = 0x0501; /* tty major/minor */
        st.st_blksize = 1024;
    }
    else
    {
        /* We do not have an fd-to-path lookup, so return a generic regular file. */
        st.st_mode = S_IFREG | 0644;
        st.st_blksize = 512;
        st.st_size = 0;
    }

    if (copy_to_user(proc, buf_addr, &st, sizeof st) < 0)
        return (uint64_t)-1;
    return 0;
}

static uint64_t sys_exit(uint64_t code)
{
    thread_t *t = thread_current();
    process_t *proc = t ? t->owner : NULL;

    kprintf("[kernel] process exited: %d\n", (int)code);

    if (proc && proc != proc_kernel())
        proc_exit(proc, (int32_t)code);

    if (t)
        t->state = THREAD_DEAD;

    /* Let the normal scheduler run the shell again */
    scheduler_yield();

    /* If yield returns, nothing is runnable */
    kprintf("sys_exit: no runnable thread\n");
    for (;;)
        __asm__ volatile("hlt");
}

/* Most bytes one write() call will move through the kernel bounce buffer. */
#define SYS_WRITE_MAX 512

static uint64_t sys_write(uint64_t fd, uint64_t buf_addr, uint64_t len)
{
    process_t *proc = cur_proc();
    uint8_t tmp[SYS_WRITE_MAX];

    if (!proc || !buf_addr)
        return (uint64_t)-1;
    if (len == 0)
        return 0;

    /* fd 0 is the console's read end; it is not writable. */
    if ((int)fd == 0)
        return (uint64_t)-1;

    /* Short writes are fine — userspace loops — so cap each call rather
     * than failing an oversized request. */
    if (len > SYS_WRITE_MAX)
        len = SYS_WRITE_MAX;

    /* Take a copy before looking at the bytes: never hand a user pointer
     * to the console or to the filesystem. */
    if (copy_from_user(proc, tmp, buf_addr, (size_t)len) < 0)
        return (uint64_t)-1;

    /* fd 1 and 2: the console, on screen and down the serial line. */
    if ((int)fd == 1 || (int)fd == 2)
    {
        for (uint64_t i = 0; i < len; i++)
        {
            terminal_putchar(tmp[i]);
            serial_putchar(tmp[i]);
        }
        return len;
    }

    if (!proc_check_perm(PERM_FS_WRITE))
        return (uint64_t)-1;

    int n = vfs_write((int)fd, tmp, (size_t)len);
    return (uint64_t)(int64_t)n; /* negative errors kept as-is */
}

/* Most bytes one read() call will move through the kernel bounce buffer. */
#define SYS_READ_MAX 512

static uint64_t sys_read(uint64_t fd, uint64_t buf, uint64_t len)
{
    process_t *proc = cur_proc();
    uint8_t tmp[SYS_READ_MAX];

    if (!proc || !buf)
        return (uint64_t)-1;
    if (len == 0)
        return 0;

    /* fd 0: stdin — hand back one keystroke at a time. */
    if ((int)fd == 0)
    {
        while (!keyboard_haschar())
            scheduler_yield();
        char c = keyboard_getchar();
        if (copy_to_user(proc, buf, &c, 1) < 0)
            return (uint64_t)-1;
        return 1;
    }

    /* fd 1 and 2 are the write ends of the console; they are not readable. */
    if ((int)fd == 1 || (int)fd == 2)
        return (uint64_t)-1;

    if (!proc_check_perm(PERM_FS_READ))
        return (uint64_t)-1;

    /* fd > 2: read into a kernel buffer, then copy it out. Short reads are
     * fine — userspace loops — so cap each call rather than failing. */
    if (len > SYS_READ_MAX)
        len = SYS_READ_MAX;

    int n = vfs_read((int)fd, tmp, (size_t)len);
    if (n <= 0)
        return (uint64_t)(int64_t)n; /* 0 = EOF, negative = error, both kept */

    if (copy_to_user(proc, buf, tmp, (size_t)n) < 0)
        return (uint64_t)-1;
    return (uint64_t)n;
}

/*
 * open(path, flags)
 *
 * flags are the O_* bits from fs/vfs.h, not the Linux ones: the access mode
 * is a bit field here rather than a two-bit value. tsys/lib/include/syscall.h
 * must agree with it.
 *
 *   O_RDONLY 0x01   O_WRONLY 0x02   O_RDWR 0x03
 *   O_CREAT  0x04   O_TRUNC  0x08   O_APPEND 0x10
 *
 * The path is copied into the kernel before use and may be at most
 * VFS_PATH_MAX - 1 bytes plus its terminator; anything longer is rejected
 * rather than silently shortened.
 */
static uint64_t sys_open(uint64_t path_uva, uint64_t flags, uint64_t mode)
{
    (void)mode;
    process_t *proc = cur_proc();
    char path[VFS_PATH_MAX];

    if (!proc || !path_uva)
        return (uint64_t)-1;
    if (copy_user_str(proc, path, path_uva, sizeof path) < 0)
        return (uint64_t)-1;
    if (path[0] == '\0')
        return (uint64_t)-1;

    uint32_t need = (flags & O_WRONLY) ? PERM_FS_WRITE : PERM_FS_READ;
    if (!proc_check_perm(need))
        return (uint64_t)-1;

    int fd = vfs_open(path, (uint32_t)flags);
    if (fd < 0)
        return (uint64_t)-1;

    /* Tag it so process exit can reclaim it if the program never closes. */
    vfs_fd_set_owner(fd, proc->pid);
    return (uint64_t)fd;
}

static uint64_t sys_close(uint64_t fd)
{
    process_t *proc = cur_proc();

    if (!proc)
        return (uint64_t)-1;

    /* 0, 1 and 2 are the console, not VFS descriptors. */
    if ((int)fd <= 2)
        return (uint64_t)-1;

    /* A process may only close what it opened. */
    if (vfs_fd_owner((int)fd) != proc->pid)
        return (uint64_t)-1;

    return (uint64_t)(int64_t)vfs_close((int)fd);
}

static uint64_t sys_getpid(void)
{
    return 1;
}

static uint64_t sys_yield(void)
{
    scheduler_yield();
    return 0;
}

static uint64_t sys_sleep(uint64_t ticks)
{
    uint64_t target = timer_ticks + ticks;
    while (timer_ticks < target)
        __asm__ volatile("hlt");
    return 0;
}

static uint64_t sys_uptime(void)
{
    return timer_ticks;
}

// Returns port pool index (>=0) or -1 if not found.
static uint64_t sys_port_find(uint64_t name_addr)
{
    process_t *proc = cur_proc();
    char name[PORT_NAME_MAX];

    if (!proc || copy_user_str(proc, name, name_addr, sizeof name) < 0)
        return (uint64_t)-1;

    port_t *p = port_find(name);
    if (!p)
        return (uint64_t)-1;

    // find its index in the pool (exposed in port.c as extern)
    extern port_t port_pool[];
    return (uint64_t)(p - port_pool);
}

#define IPC_INLINE_MAX 64

/* Largest payload a userspace sender may hand to a port in one message. */
#define IPC_USER_MAX 512

static uint64_t sys_port_send(uint64_t idx, uint64_t code,
                              uint64_t data_addr, uint64_t length)
{
    process_t *proc = cur_proc();
    uint8_t payload[IPC_USER_MAX];

    if (!proc || !proc_check_perm(PERM_IPC_SEND))
        return (uint64_t)-1;

    extern port_t port_pool[];
    extern uint8_t port_used[];
    if (idx >= 32 || !port_used[idx])
        return (uint64_t)-1;

    if (length > IPC_USER_MAX)
        return (uint64_t)-1;

    /* port_send() copies the payload onto the heap, so a local buffer is fine. */
    if (length > 0 && copy_from_user(proc, payload, data_addr, (size_t)length) < 0)
        return (uint64_t)-1;

    return (uint64_t)port_send(&port_pool[idx], (uint32_t)code,
                               length ? payload : 0, (uint32_t)length);
}

static uint64_t sys_port_receive(uint64_t idx, uint64_t out_addr)
{
    process_t *proc = cur_proc();

    if (!proc || !proc_check_perm(PERM_IPC_RECEIVE))
        return (uint64_t)-1;

    extern port_t port_pool[];
    extern uint8_t port_used[];
    if (idx >= 32 || !port_used[idx])
        return (uint64_t)-1;

    ipc_message_t msg;
    int r = port_receive(&port_pool[idx], &msg);
    if (r != 0)
        return (uint64_t)-1;

    /*
     * Userspace ipc_message_user_t layout (must match hello_gui_app.c):
     *
     *   offset  0: uint32_t sender_pid
     *   offset  4: uint32_t code
     *   offset  8: void*    data       ← we set this to &inline_data
     *   offset 16: uint32_t length
     *   offset 20: uint8_t  inline_data[64]
     */
    uint8_t out[20 + IPC_INLINE_MAX];
    for (size_t i = 0; i < sizeof out; i++)
        out[i] = 0;

    uint32_t *u32 = (uint32_t *)out;
    uint64_t *data_ptr = (uint64_t *)(out + 8);
    uint8_t *inlined = out + 20;

    u32[0] = msg.sender_pid;
    u32[1] = msg.code;
    u32[4] = msg.length;

    /* copy up to 64 bytes of payload into the inline buffer */
    uint32_t copy_len = msg.length < IPC_INLINE_MAX ? msg.length : IPC_INLINE_MAX;
    if (msg.data && copy_len > 0)
    {
        uint8_t *src = (uint8_t *)msg.data;
        for (uint32_t i = 0; i < copy_len; i++)
            inlined[i] = src[i];
    }

    /* point data at the caller's copy of the inline buffer */
    *data_ptr = out_addr + 20;

    if (copy_to_user(proc, out_addr, out, sizeof out) < 0)
        return (uint64_t)-1;

    return 0;
}

static uint64_t sys_port_create(uint64_t name_addr)
{
    process_t *proc = cur_proc();
    char name[PORT_NAME_MAX];

    if (!proc || !proc_check_perm(PERM_IPC_RECEIVE))
        return (uint64_t)-1;

    if (copy_user_str(proc, name, name_addr, sizeof name) < 0)
        return (uint64_t)-1;

    port_t *p = port_create(name);
    if (!p)
        return (uint64_t)-1;

    extern port_t port_pool[];
    return (uint64_t)(p - port_pool);
}

struct fb_info_user
{
    uint64_t width, height, pitch;
    uint32_t bpp;
};

static long sys_fb_info(uint64_t out_addr)
{
    process_t *proc = cur_proc();
    struct fb_info_user info;

    if (!proc || !out_addr)
        return -1;
    struct limine_framebuffer *fb = fb_get();
    if (!fb)
        return -1;

    info.width = fb->width;
    info.height = fb->height;
    info.pitch = fb->pitch;
    info.bpp = fb->bpp;

    if (copy_to_user(proc, out_addr, &info, sizeof info) < 0)
        return -1;
    return 0;
}

static long sys_fb_clear(uint32_t colour)
{
    fb_clear(colour);
    return 0;
}

static long sys_fb_fill_rect(uint64_t x, uint64_t y,
                             uint64_t w, uint64_t h, uint32_t colour)
{
    fb_fill_rect(x, y, w, h, colour);
    return 0;
}

static long sys_fb_putpixel(uint64_t x, uint64_t y, uint32_t colour)
{
    fb_putpixel(x, y, colour);
    return 0;
}

static long sys_fb_getpixel(uint64_t x, uint64_t y)
{
    fb_getpixel(x, y);
    return 0;
}

static uint64_t sys_spawn(uint64_t path_addr)
{
    if (!path_addr)
        return (uint64_t)-1;

    process_t *proc = cur_proc();
    char path[VFS_PATH_MAX];

    if (!proc || copy_user_str(proc, path, path_addr, sizeof path) < 0)
        return (uint64_t)-1;
    if (path[0] == '\0')
        return (uint64_t)-1;

    int pid = exec_launch(path, 0xffffffff);
    return (pid < 0) ? (uint64_t)-1 : (uint64_t)pid;
}

static uint64_t sys_wait(uint64_t pid)
{
    if (pid == 0)
        return (uint64_t)-1;

    for (;;)
    {
        process_t *p = proc_get((uint32_t)pid);
        if (!p)
            return (uint64_t)-1;

        if (p->state == PROC_ZOMBIE)
        {
            int code = p->exit_code;
            p->state = PROC_DEAD; /* free the slot */
            break;
        }

        scheduler_yield();
    }
}

static uint64_t sys_lsdrv(uint64_t user_buf, uint64_t max)
{
    process_t *proc = cur_proc();

    if (!proc)
        return (uint64_t)-1;

    if (max == 0 || max > DRIVER_MAX)
        max = DRIVER_MAX;

    driver_info_t tmp[DRIVER_MAX];
    int n = drivers_list(tmp, (int)max);
    if (n <= 0)
        return 0;

    size_t bytes = (size_t)n * sizeof(driver_info_t);

    if (copy_to_user(proc, user_buf, tmp, bytes) != 0)
        return (uint64_t)-1;

    return (uint64_t)n;
}

static long sys_kbd_haschar(void)
{
    return keyboard_haschar() ? 1 : 0;
}

static long sys_kbd_getchar(void)
{
    if (!keyboard_haschar())
        return -1;
    return (long)(unsigned char)keyboard_getchar();
}

static long sys_mouse_get_state(uint64_t user_addr)
{
    process_t *proc = cur_proc();
    mouse_state_t st;

    if (!proc || !user_addr)
        return -1;

    mouse_get_state(&st);

    if (copy_to_user(proc, user_addr, &st, sizeof(st)) < 0)
        return -1;
    return 0;
}

/* ── dispatch ────────────────────────────────────────────────────────────── */

uint64_t syscall_dispatch(uint64_t num, uint64_t a, uint64_t b, uint64_t c, uint64_t d, uint64_t e, uint64_t f)
{
    switch (num)
    {
    case SYS_READ:
        return sys_read(a, b, c);
    case SYS_WRITE:
        return sys_write(a, b, c);
    case SYS_OPEN:
        return sys_open(a, b, c);
    case SYS_CLOSE:
        return sys_close(a);
    case SYS_FSTAT:
        return sys_fstat(a, b);
    case SYS_MMAP:
        return sys_mmap(a, b, c, d, e, f);
    case SYS_MUNMAP:
        return sys_munmap(a, b);
    case SYS_BRK:
        return sys_brk(a);
    case SYS_GETPID:
        return sys_getpid();
    case SYS_YIELD:
        return sys_yield();
    case SYS_SLEEP:
        return sys_sleep(a);
    case SYS_EXIT:
        return sys_exit(a);
    case SYS_UPTIME:
        return sys_uptime();
    case SYS_PORT_FIND:
        return sys_port_find(a);
    case SYS_PORT_SEND:
        return sys_port_send(a, b, c, d);
    case SYS_PORT_RECEIVE:
        return sys_port_receive(a, b);
    case SYS_PORT_CREATE:
        return sys_port_create(a);
    case SYS_FB_INFO:
        return sys_fb_info(a);
    case SYS_FB_CLEAR:
        return sys_fb_clear((uint32_t)a);
    case SYS_FB_FILL_RECT:
        return sys_fb_fill_rect(a, b, c, d, (uint32_t)e);
    case SYS_FB_PUTPIXEL:
        return sys_fb_putpixel(a, b, (uint32_t)c);
    case SYS_SPAWN:
        return sys_spawn(a);
    case SYS_WAIT:
        return sys_wait(a);
    case SYS_LSDRV:
        return sys_lsdrv(a, b);
    case SYS_KBD_HASCHAR:
        return sys_kbd_haschar();
    case SYS_KBD_GETCHAR:
        return sys_kbd_getchar();
    case SYS_MOUSE_GET_STATE:
        return sys_mouse_get_state(a);
    case SYS_FB_GETPIXEL:
        return sys_fb_getpixel(a, b);
    default:
        kprintf("[kernel] unknown syscall %llu\n", num);
        return (uint64_t)-1;
    }
}

/* ── init ────────────────────────────────────────────────────────────────── */

void syscall_init(void)
{
    kprintf("Syscall: checking CPU support...\n");
    if (!syscall_supported())
    {
        kprintf("Syscall: not supported by CPU.\n");
        return;
    }

    uint64_t efer = rdmsr(MSR_EFER);
    efer |= 1;            // SCE: enable syscall/sysret
    efer |= (1ULL << 11); // NXE: must stay enabled -- kernel .rodata (see
                          // linker.ld) and exec.c's VMM_NX user pages both
                          // rely on the NX bit (PTE bit 63) being valid.
                          // With NXE=0, bit 63 becomes a reserved bit and
                          // ANY access to such a page page-faults.
    wrmsr(MSR_EFER, efer);

    uint64_t star = ((uint64_t)0x10 << 48) | ((uint64_t)GDT_KERNEL_CODE << 32);
    wrmsr(MSR_STAR, star);
    wrmsr(MSR_LSTAR, (uint64_t)syscall_entry);
    wrmsr(MSR_SFMASK, (1 << 9) | (1 << 10));

    kprintf("Syscall: ready.\n");
}
