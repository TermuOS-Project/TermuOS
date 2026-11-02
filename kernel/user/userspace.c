#include "Kernel.h"

void userspace_init(void)
{
    syscall_init();
}

void jump_userspace(uint64_t entry, uint64_t stack)
{
    __asm__ volatile("cli");

    process_t *proc = thread_current()->owner;
    if (proc && proc->pagemap)
        vmm_switch(proc->pagemap);

    tss_set_kernel_stack(gdt_get_exception_stack());
    enter_userspace(entry, stack);
}