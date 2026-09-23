#pragma once
#include <stdint.h>
#include <stddef.h>
#include <limine.h>

#define PAGE_SIZE 4096

void pmm_init(struct limine_memmap_response *memmap);
void *pmm_alloc(void);           // allocate one 4KB page, returns physical addr
void pmm_free(void *addr);       // free one 4KB page
size_t pmm_free_pages(void);     // number of free pages remaining
size_t pmm_total_pages(void);    // total usable pages
size_t pmm_physical_pages(void); // pages in the contiguous RAM map