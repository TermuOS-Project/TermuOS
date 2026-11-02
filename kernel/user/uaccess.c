#include "Kernel.h"

#define UA_PAGE_SIZE 4096ULL
#define UA_PAGE_OFF(a) ((a) & (UA_PAGE_SIZE - 1))

/* PTE → physical page: drop NX and the flag bits, keep address bits 51:12. */
#define PTE_ADDR_MASK 0x000FFFFFFFFFF000ULL

int user_range_ok(uint64_t uva, size_t n)
{
    if (!uva)
        return 0;
    if (n == 0)
        return 1;
    if (uva + n < uva) /* wraps around the top of the address space */
        return 0;
    return uva + n <= USER_VA_MAX;
}

void *user_kptr(process_t *proc, uint64_t uva, int for_write)
{
    if (!proc || !user_range_ok(uva, 1))
        return 0;

    uint64_t pte = vmm_virt_to_pte(proc->pagemap, uva);
    if (!(pte & VMM_PRESENT) || !(pte & VMM_USER))
        return 0;
    if (for_write && !(pte & VMM_WRITE))
        return 0;

    uint64_t phys = pte & PTE_ADDR_MASK;
    if (!phys)
        return 0;

    return (void *)(hhdm_base + phys + UA_PAGE_OFF(uva));
}

/* Bytes from uva to the end of its page. */
static size_t page_tail(uint64_t uva, size_t n)
{
    size_t tail = (size_t)(UA_PAGE_SIZE - UA_PAGE_OFF(uva));
    return tail < n ? tail : n;
}

int copy_from_user(process_t *proc, void *kdst, uint64_t usrc, size_t n)
{
    uint8_t *d = (uint8_t *)kdst;

    if (!kdst || !user_range_ok(usrc, n))
        return -1;

    while (n)
    {
        const uint8_t *s = (const uint8_t *)user_kptr(proc, usrc, 0);
        if (!s)
            return -1;

        size_t chunk = page_tail(usrc, n);
        for (size_t i = 0; i < chunk; i++)
            d[i] = s[i];

        d += chunk;
        usrc += chunk;
        n -= chunk;
    }
    return 0;
}

int copy_to_user(process_t *proc, uint64_t udst, const void *ksrc, size_t n)
{
    const uint8_t *s = (const uint8_t *)ksrc;

    if (!ksrc || !user_range_ok(udst, n))
        return -1;

    while (n)
    {
        uint8_t *d = (uint8_t *)user_kptr(proc, udst, 1);
        if (!d)
            return -1;

        size_t chunk = page_tail(udst, n);
        for (size_t i = 0; i < chunk; i++)
            d[i] = s[i];

        s += chunk;
        udst += chunk;
        n -= chunk;
    }
    return 0;
}

int copy_user_str(process_t *proc, char *kdst, uint64_t usrc, size_t max)
{
    if (!kdst || max == 0)
        return -1;

    kdst[0] = '\0';

    for (size_t i = 0; i < max; i++)
    {
        const uint8_t *s = (const uint8_t *)user_kptr(proc, usrc + i, 0);
        if (!s)
            return -1;

        kdst[i] = (char)*s;
        if (kdst[i] == '\0')
            return 0;
    }

    /* No terminator within max bytes — truncated, so reject it. */
    kdst[max - 1] = '\0';
    return -1;
}
