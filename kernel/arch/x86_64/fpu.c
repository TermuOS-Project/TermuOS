#include "Kernel.h"

void fpu_init(void)
{
    uint64_t cr0, cr4;
    __asm__ volatile("mov %%cr0, %0" : "=r"(cr0));
    cr0 &= ~(1ull << 2);
    cr0 |= (1ull << 1);
    __asm__ volatile("mov %0, %%cr0" ::"r"(cr0));

    __asm__ volatile("mov %%cr4, %0" : "=r"(cr4));
    cr4 |= (1ull << 9);
    cr4 |= (1ull << 10);
    __asm__ volatile("mov %0, %%cr4" ::"r"(cr4));

    __asm__ volatile("fninit");
}