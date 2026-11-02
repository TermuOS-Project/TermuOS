#include "Kernel.h"

#define HEAP_START 0xffff910000000000ULL
#define HEAP_MAGIC 0xCAFEBABEDEADBEEFULL
#define ALIGN(x, a) (((x) + ((a) - 1)) & ~((a) - 1))
#define MIN_SPLIT 32

typedef struct block
{
    uint64_t magic;
    size_t size;
    int free;
    struct block *next;
} block_t;

#define HEADER_SIZE sizeof(block_t)

static block_t *heap_head = NULL;
static uint64_t heap_end = HEAP_START;

static void heap_grow(void)
{
    void *phys = pmm_alloc();
    if (!phys)
    {
        kprintf("heap: out of physical memory!\n");
        for (;;)
            __asm__ volatile("hlt");
    }
    uint64_t cr3;
    __asm__ volatile("movq %%cr3, %0" : "=r"(cr3));
    vmm_map(cr3, heap_end, (uint64_t)phys, VMM_KERNEL_FLAGS);
    heap_end += PAGE_SIZE;
}

void heap_init(void)
{
    heap_grow();

    heap_head = (block_t *)HEAP_START;
    heap_head->magic = HEAP_MAGIC;
    heap_head->size = PAGE_SIZE - HEADER_SIZE;
    heap_head->free = 1;
    heap_head->next = NULL;
}

void *kmalloc(size_t size)
{
    if (!size)
        return NULL;
    size = ALIGN(size, 8);

    block_t *b = heap_head;

    while (b)
    {
        if (b->magic != HEAP_MAGIC)
        {
            kprintf("heap: corruption at 0x%x!\n", (uint64_t)b);
            for (;;)
                __asm__ volatile("hlt");
        }

        // Last block, free, but too small — extend it
        if (!b->next && b->free && b->size < size)
        {
            while (b->size < size)
            {
                heap_grow();
                b->size += PAGE_SIZE;
            }
        }

        if (b->free && b->size >= size)
        {
            if (b->size >= size + HEADER_SIZE + MIN_SPLIT)
            {
                block_t *split = (block_t *)((uint8_t *)b + HEADER_SIZE + size);
                split->magic = HEAP_MAGIC;
                split->size = b->size - size - HEADER_SIZE;
                split->free = 1;
                split->next = b->next;
                b->next = split;
                b->size = size;
            }
            b->free = 0;
            return (void *)((uint8_t *)b + HEADER_SIZE);
        }

        // Last block is used — append a new free block
        if (!b->next)
        {
            uint64_t new_block_addr = heap_end;
            heap_grow();
            block_t *nb = (block_t *)new_block_addr;
            nb->magic = HEAP_MAGIC;
            nb->size = PAGE_SIZE - HEADER_SIZE;
            nb->free = 1;
            nb->next = NULL;
            b->next = nb;
        }

        b = b->next;
    }

    return NULL;
}

void kfree(void *ptr)
{
    if (!ptr)
        return;

    block_t *b = (block_t *)((uint8_t *)ptr - HEADER_SIZE);

    if (b->magic != HEAP_MAGIC)
    {
        kprintf("heap: kfree bad pointer 0x%x!\n", (uint64_t)ptr);
        return;
    }
    if (b->free)
    {
        kprintf("heap: double-free at 0x%x!\n", (uint64_t)ptr);
        return;
    }

    b->free = 1;

    // Coalesce adjacent free blocks
    block_t *cur = heap_head;
    while (cur && cur->next)
    {
        if (cur->free && cur->next->free)
        {
            cur->size += HEADER_SIZE + cur->next->size;
            cur->next = cur->next->next;
        }
        else
        {
            cur = cur->next;
        }
    }
}

void *kcalloc(size_t count, size_t size)
{
    size_t total = count * size;
    void *ptr = kmalloc(total);
    if (!ptr)
        return NULL;
    uint8_t *p = (uint8_t *)ptr;
    for (size_t i = 0; i < total; i++)
        p[i] = 0;
    return ptr;
}

void *krealloc(void *ptr, size_t size)
{
    if (!ptr)
        return kmalloc(size);
    if (!size)
    {
        kfree(ptr);
        return NULL;
    }
    block_t *b = (block_t *)((uint8_t *)ptr - HEADER_SIZE);
    if (b->size >= size)
        return ptr;
    void *new = kmalloc(size);
    if (!new)
        return NULL;
    uint8_t *src = (uint8_t *)ptr;
    uint8_t *dst = (uint8_t *)new;
    size_t copy = b->size < size ? b->size : size;
    for (size_t i = 0; i < copy; i++)
        dst[i] = src[i];
    kfree(ptr);
    return new;
}