#include <stdint.h>
#include <stddef.h>
#include <limine.h>

#include "drivers/video/fb.h"
#include "drivers/video/terminal.h"
#include "drivers/serial/serial.h"
#include "drivers/input/keyboard.h"
#include "drivers/input/mouse.h"
#include "drivers/rtc_rust/rtc_rust.h"
#include "arch/x86_64/idt.h"
#include "arch/x86_64/gdt.h"
#include "arch/x86_64/pit.h"
#include "arch/x86_64/fpu.h"
#include "mm/pmm.h"
#include "mm/vmm.h"
#include "mm/heap.h"
#include "sched/scheduler.h"
#include "fs/vfs.h"
#include "fs/ramfs.h"
#include "fs/tfs.h"
#include "fs/devfs.h"
#include "fs/install.h"
#include "drivers/storage/ata.h"
#include "drivers/net/pci.h"
#include "drivers/driver.h"
#include "shell/shell.h"
#include "lib/printf.h"
#include "lib/cxxabi.h"
#include "lib/crc32.h"
#include "lib/rdrand.h"
#include "proc/process.h"
#include "ob/object.h"
#include "io/ioman.h"
#include "ipc/port.h"
#include "proc/exec.h"
#include "proc/launch.h"
#include "user/userspace.h"

LIMINE_BASE_REVISION(3);

__attribute__((used, section(".limine_requests_start"))) static volatile LIMINE_REQUESTS_START_MARKER

    __attribute__((used, section(".limine_requests"))) static volatile struct limine_framebuffer_request fb_request = {
        .id = LIMINE_FRAMEBUFFER_REQUEST, .revision = 0};

__attribute__((used, section(".limine_requests"))) static volatile struct limine_memmap_request memmap_request = {
    .id = LIMINE_MEMMAP_REQUEST, .revision = 0};

__attribute__((used, section(".limine_requests"))) static volatile struct limine_hhdm_request hhdm_request = {
    .id = LIMINE_HHDM_REQUEST, .revision = 0};

__attribute__((used, section(".limine_requests"))) static volatile struct limine_kernel_address_request kaddr_request = {
    .id = LIMINE_KERNEL_ADDRESS_REQUEST, .revision = 0};

__attribute__((used, section(".limine_requests"))) volatile struct limine_module_request module_request = {
    .id = LIMINE_MODULE_REQUEST,
    .revision = 0,
};

__attribute__((used, section(".limine_requests_end"))) static volatile LIMINE_REQUESTS_END_MARKER

    static uint64_t
    read_cr3(void)
{
    uint64_t cr3;
    __asm__ volatile("movq %%cr3, %0" : "=r"(cr3));
    return cr3;
}

void virtio_gpu_rust_probe(void);

void kernel_main(void)
{
    serial_init();
    serial_write("TermuOS: kernel_main() entered\n");

    if (!fb_request.response || fb_request.response->framebuffer_count < 1)
    {
        serial_write("TermuOS: no framebuffer response from Limine, halting\n");
        for (;;)
            __asm__ volatile("hlt");
    }

    struct limine_framebuffer *fb = fb_request.response->framebuffers[0];

    fb_init(fb);
    terminal_init();
    terminal_set_offset(0, 0);
    terminal_set_size(fb->width, fb->height);
    fb_clear(0x000000);

    gdt_init();
    tss_set_kernel_stack(gdt_get_exception_stack());
    idt_init(GDT_KERNEL_CODE);
    fpu_init();
    userspace_init();

    pmm_init(memmap_request.response);
    vmm_init(hhdm_request.response->offset, read_cr3());
    heap_init();
    cxx_init();
    keyboard_init();
    mouse_init();

    vfs_init();

    ata_init();

    ob_init();
    ioman_init();
    ipc_init();
    proc_init();
    scheduler_init();
    pit_init(100);

    ata_ioman_register();
    keyboard_ioman_register();

    crc32_selftest();
    rdrand_selftest();

    if (tfs_mount() == 0)
    {
        vfs_mount("/", tfs_get_root());
        kprintf("tfs: root fs ready\n");

        vfs_mkdir("/bin");
        vfs_mkdir("/etc");
        vfs_mkdir("/home");
        vfs_mkdir("/home/root");
        install_bin_if_needed();
    }
    else
    {
        kprintf("tfs: no disk — falling back to ramfs\n");
        vfs_node_t *root = ramfs_create_root();
        vfs_mount("/", root);
        vfs_mkdir("/bin");
        vfs_mkdir("/etc");
        vfs_mkdir("/home");
        vfs_mkdir("/home/root");
        vfs_mkdir("/mnt");
    }
    vfs_mkdir("/dev");
    vfs_mount("/dev", devfs_create());

    pci_init();
    drivers_register_all();
    drivers_init();

    process_t *sp = proc_create("shell");
    if (!sp)
        sp = proc_kernel();
    thread_create("shell", shell_thread_entry, sp);

    for (;;)
        __asm__ volatile("hlt");
}
