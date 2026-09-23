#include "pmm.h"
#include "vmm.h"
#include "../lib/printf.h"
#include <stdint.h>
#include <stddef.h>

// Bitmap allocator
//
// Each bit tracks one 4 KB page. A value of 0 means free, while 1 means used.
// The bitmap lives in the first usable region that is large enough to hold it.

#define BITS_PER_ENTRY 64
#define PAGE_TO_BIT(p) ((p) / PAGE_SIZE)
#define BIT_IDX(bit) ((bit) / BITS_PER_ENTRY)
#define BIT_OFF(bit) ((bit) % BITS_PER_ENTRY)

static uint64_t *bitmap_phys = NULL; // bitmap's PHYSICAL base address
static size_t bm_size = 0;           // number of uint64_t entries in bitmap
static size_t total_pages = 0;
static size_t free_pages = 0;
static uint64_t highest_addr = 0; // highest usable physical address
static uint64_t physical_end = 0; // end of the contiguous RAM map

// The bitmap must always be dereferenced through the HHDM mapping, never as
// a raw physical pointer. Limine identity-maps low physical memory only in
// its own original boot pagemap — vmm_new_pagemap() copies just the
// higher-half (HHDM/kernel) PML4 entries into new process pagemaps, so once
// the scheduler switches CR3 to a process's own pagemap, that identity
// mapping is gone and a raw physical dereference page-faults.
//
// Before vmm_init() runs, hhdm_base is still 0, so PHYS_TO_VIRT() is a no-op
// and this resolves to the same raw physical address — safe, because at
// that point in boot we're still running under Limine's original pagemap.
static inline uint64_t *bm(void)
{
    return (uint64_t *)PHYS_TO_VIRT(bitmap_phys);
}

static inline void bitmap_set(size_t bit)
{
    bm()[BIT_IDX(bit)] |= (1ULL << BIT_OFF(bit));
}

static inline void bitmap_clear(size_t bit)
{
    bm()[BIT_IDX(bit)] &= ~(1ULL << BIT_OFF(bit));
}

static inline int bitmap_test(size_t bit)
{
    return (bm()[BIT_IDX(bit)] >> BIT_OFF(bit)) & 1;
}

// Initialization

void pmm_init(struct limine_memmap_response *memmap)
{
    // First pass: find the highest usable physical address.
    for (uint64_t i = 0; i < memmap->entry_count; i++)
    {
        struct limine_memmap_entry *e = memmap->entries[i];
        uint64_t end = e->base + e->length;
        if (e->type == LIMINE_MEMMAP_USABLE ||
            e->type == LIMINE_MEMMAP_ACPI_RECLAIMABLE ||
            e->type == LIMINE_MEMMAP_ACPI_NVS ||
            e->type == LIMINE_MEMMAP_BOOTLOADER_RECLAIMABLE ||
            e->type == LIMINE_MEMMAP_KERNEL_AND_MODULES)
        {
            if (end > physical_end)
                physical_end = end;
        }
        if (e->type == LIMINE_MEMMAP_USABLE)
        {
            if (end > highest_addr)
                highest_addr = end;
        }
    }

    // Include the reserved tail immediately following RAM, but stop before
    // unrelated high-address firmware and device ranges.
    for (uint64_t i = 0; i < memmap->entry_count; i++)
    {
        struct limine_memmap_entry *e = memmap->entries[i];
        if (e->base == physical_end && e->type != LIMINE_MEMMAP_BAD_MEMORY &&
            e->type != LIMINE_MEMMAP_FRAMEBUFFER)
        {
            physical_end = e->base + e->length;
            i = 0;
        }
    }

    // Figure out how many pages and bitmap entries are needed.
    size_t total_bits = highest_addr / PAGE_SIZE;
    bm_size = (total_bits + BITS_PER_ENTRY - 1) / BITS_PER_ENTRY;
    size_t bm_bytes = bm_size * sizeof(uint64_t);

    // Second pass: find a usable region large enough for the bitmap.
    for (uint64_t i = 0; i < memmap->entry_count; i++)
    {
        struct limine_memmap_entry *e = memmap->entries[i];
        if (e->type == LIMINE_MEMMAP_USABLE && e->length >= bm_bytes)
        {
            bitmap_phys = (uint64_t *)e->base;
            break;
        }
    }

    if (!bitmap_phys)
    {
        for (;;)
            __asm__ volatile("hlt");
    }

    // Mark every page as used at first.
    for (size_t i = 0; i < bm_size; i++)
        bm()[i] = 0xffffffffffffffff;

    // Third pass: mark all usable pages as free.
    for (uint64_t i = 0; i < memmap->entry_count; i++)
    {
        struct limine_memmap_entry *e = memmap->entries[i];
        if (e->type == LIMINE_MEMMAP_USABLE)
        {
            uint64_t base = e->base;
            uint64_t len = e->length;
            for (uint64_t off = 0; off < len; off += PAGE_SIZE)
            {
                bitmap_clear(PAGE_TO_BIT(base + off));
                free_pages++;
                total_pages++;
            }
        }
    }

    // Fourth pass: mark the bitmap pages themselves as used.
    size_t bm_pages = (bm_bytes + PAGE_SIZE - 1) / PAGE_SIZE;
    for (size_t i = 0; i < bm_pages; i++)
    {
        size_t bit = PAGE_TO_BIT((uint64_t)bitmap_phys + i * PAGE_SIZE);
        if (!bitmap_test(bit))
        {
            bitmap_set(bit);
            free_pages--;
        }
    }
}

// Allocation and freeing

void *pmm_alloc(void)
{
    uint64_t *bitmap = bm();
    for (size_t i = 0; i < bm_size; i++)
    {
        if (bitmap[i] == 0xffffffffffffffff)
            continue; // This 64-page chunk is fully allocated.

        // Find the first free page bit.
        for (int bit = 0; bit < BITS_PER_ENTRY; bit++)
        {
            if (!((bitmap[i] >> bit) & 1))
            {
                bitmap[i] |= (1ULL << bit);
                free_pages--;
                return (void *)((uint64_t)(i * BITS_PER_ENTRY + bit) * PAGE_SIZE);
            }
        }
    }

    kprintf("PMM: out of memory!\n");
    return NULL;
}

void pmm_free(void *addr)
{
    size_t bit = PAGE_TO_BIT((uint64_t)addr);
    if (bitmap_test(bit))
    {
        bitmap_clear(bit);
        free_pages++;
    }
}

size_t pmm_free_pages(void) { return free_pages; }
size_t pmm_total_pages(void) { return total_pages; }
size_t pmm_physical_pages(void)
{
    return (physical_end + PAGE_SIZE - 1) / PAGE_SIZE;
}
