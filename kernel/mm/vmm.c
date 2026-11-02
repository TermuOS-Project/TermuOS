#include "Kernel.h"

#define PML4_IDX(v) (((v) >> 39) & 0x1ff)
#define PDPT_IDX(v) (((v) >> 30) & 0x1ff)
#define PD_IDX(v) (((v) >> 21) & 0x1ff)
#define PT_IDX(v) (((v) >> 12) & 0x1ff)
#define PAGE_MASK (~(uint64_t)(PAGE_SIZE - 1))

uint64_t hhdm_base = 0;

uint64_t termuos_hhdm_base(void) { return hhdm_base; }

static inline uint64_t *phys_to_virt_table(uint64_t phys)
{
    return (uint64_t *)((phys & PAGE_MASK) + hhdm_base);
}

static uint64_t alloc_table(void)
{
    uint64_t phys = (uint64_t)pmm_alloc();
    if (!phys)
    {
        kprintf("VMM: out of memory allocating page table!\n");
        for (;;)
            __asm__ volatile("hlt");
    }

    uint64_t *virt = phys_to_virt_table(phys);
    for (int i = 0; i < 512; i++)
        virt[i] = 0;
    return phys;
}

static uint64_t *get_or_create_entry(uint64_t *table, int idx, uint64_t flags)
{
    if (!(table[idx] & VMM_PRESENT))
    {
        uint64_t new_table = alloc_table();
        table[idx] = new_table | flags;
    }
    return phys_to_virt_table(table[idx] & PAGE_MASK);
}

void vmm_init(uint64_t hhdm, pagemap_t kernel_pml4)
{
    hhdm_base = hhdm;

    // switch to limine page map (already active, but confirms)
    vmm_switch(kernel_pml4);
}

pagemap_t vmm_new_pagemap(void)
{
    uint64_t pml4_phys = alloc_table();
    uint64_t *new_pml4 = phys_to_virt_table(pml4_phys);

    // copy kernels higher-half mappings
    uint64_t cr3;
    __asm__ volatile("movq %%cr3, %0" : "=r"(cr3));
    uint64_t *kernel_pml4 = phys_to_virt_table(cr3 & PAGE_MASK);

    for (int i = 256; i < 512; i++)
        new_pml4[i] = kernel_pml4[i];

    return pml4_phys;
}

void vmm_map(pagemap_t pm, uint64_t virt, uint64_t phys, uint64_t flags)
{
    uint64_t *pml4 = phys_to_virt_table(pm);

    uint64_t *pdpt = get_or_create_entry(pml4, PML4_IDX(virt), VMM_PRESENT | VMM_WRITE | VMM_USER);
    uint64_t *pd = get_or_create_entry(pdpt, PDPT_IDX(virt), VMM_PRESENT | VMM_WRITE | VMM_USER);
    uint64_t *pt = get_or_create_entry(pd, PD_IDX(virt), VMM_PRESENT | VMM_WRITE | VMM_USER);

    pt[PT_IDX(virt)] = (phys & PAGE_MASK) | flags;
}

void vmm_unmap(pagemap_t pm, uint64_t virt)
{
    uint64_t *pml4 = phys_to_virt_table(pm);

    if (!(pml4[PML4_IDX(virt)] & VMM_PRESENT))
        return;
    uint64_t *pdpt = phys_to_virt_table(pml4[PML4_IDX(virt)]);

    if (!(pdpt[PDPT_IDX(virt)] & VMM_PRESENT))
        return;
    uint64_t *pd = phys_to_virt_table(pdpt[PDPT_IDX(virt)]);

    if (!(pd[PD_IDX(virt)] & VMM_PRESENT))
        return;
    uint64_t *pt = phys_to_virt_table(pd[PD_IDX(virt)]);

    pt[PT_IDX(virt)] = 0;

    __asm__ volatile("invlpg (%0)" ::"r"(virt) : "memory");
}

void vmm_switch(pagemap_t pm)
{
    __asm__ volatile("movq %0, %%cr3" ::"r"(pm) : "memory");
}

uint64_t vmm_virt_to_pte(pagemap_t pm, uint64_t virt)
{
    uint64_t *pml4 = phys_to_virt_table(pm);
    uint64_t acc = ~(uint64_t)0; /* AND of the permission bits seen so far */

    if (!(pml4[PML4_IDX(virt)] & VMM_PRESENT))
        return 0;
    acc &= pml4[PML4_IDX(virt)];
    uint64_t *pdpt = phys_to_virt_table(pml4[PML4_IDX(virt)]);

    if (!(pdpt[PDPT_IDX(virt)] & VMM_PRESENT))
        return 0;
    acc &= pdpt[PDPT_IDX(virt)];
    uint64_t *pd = phys_to_virt_table(pdpt[PDPT_IDX(virt)]);

    if (!(pd[PD_IDX(virt)] & VMM_PRESENT))
        return 0;
    acc &= pd[PD_IDX(virt)];
    uint64_t *pt = phys_to_virt_table(pd[PD_IDX(virt)]);

    uint64_t pte = pt[PT_IDX(virt)];
    if (!(pte & VMM_PRESENT))
        return 0;
    acc &= pte;

    /* Keep the leaf's address bits, but the ancestors' effective USER/WRITE. */
    return (pte & ~(VMM_USER | VMM_WRITE)) | (acc & (VMM_USER | VMM_WRITE));
}

uint64_t vmm_virt_to_phys(pagemap_t pm, uint64_t virt)
{
    uint64_t *pml4 = phys_to_virt_table(pm);

    if (!(pml4[PML4_IDX(virt)] & VMM_PRESENT))
        return 0;
    uint64_t *pdpt = phys_to_virt_table(pml4[PML4_IDX(virt)]);

    if (!(pdpt[PDPT_IDX(virt)] & VMM_PRESENT))
        return 0;
    uint64_t *pd = phys_to_virt_table(pdpt[PDPT_IDX(virt)]);

    if (!(pd[PD_IDX(virt)] & VMM_PRESENT))
        return 0;
    uint64_t *pt = phys_to_virt_table(pd[PD_IDX(virt)]);

    if (!(pt[PT_IDX(virt)] & VMM_PRESENT))
        return 0;
    return (pt[PT_IDX(virt)] & PAGE_MASK) + (virt & (PAGE_SIZE - 1));
}