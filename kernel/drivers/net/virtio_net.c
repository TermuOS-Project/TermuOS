#include "virtio_net.h"
#include "pci.h"
#include "../../net/net.h"
#include "../../arch/x86_64/idt.h"
#include "../../mm/pmm.h"
#include "../../mm/vmm.h"
#include "../../lib/printf.h"
#include <stdint.h>
#include <stddef.h>

// ─── Virtio legacy register offsets ──────────────────────────────────────────
#define VIRTIO_PCI_HOST_FEATURES 0x00
#define VIRTIO_PCI_GUEST_FEATURES 0x04
#define VIRTIO_PCI_QUEUE_PFN 0x08
#define VIRTIO_PCI_QUEUE_SIZE 0x0C
#define VIRTIO_PCI_QUEUE_SEL 0x0E
#define VIRTIO_PCI_QUEUE_NOTIFY 0x10
#define VIRTIO_PCI_STATUS 0x12
#define VIRTIO_PCI_ISR 0x13
#define VIRTIO_PCI_CONFIG 0x14

#define VIRTIO_STATUS_ACKNOWLEDGE 1
#define VIRTIO_STATUS_DRIVER 2
#define VIRTIO_STATUS_DRIVER_OK 4
#define VIRTIO_NET_F_MAC (1 << 5)

#define VRING_DESC_F_NEXT 1
#define VRING_DESC_F_WRITE 2

// ─── Virtqueue structs (no event fields — legacy mode) ────────────────────────

typedef struct
{
    uint64_t addr;
    uint32_t len;
    uint16_t flags;
    uint16_t next;
} __attribute__((packed)) vring_desc_t;

typedef struct
{
    uint32_t id;
    uint32_t len;
} __attribute__((packed)) vring_used_elem_t;

typedef struct
{
    uint8_t flags;
    uint8_t gso_type;
    uint16_t hdr_len;
    uint16_t gso_size;
    uint16_t csum_start;
    uint16_t csum_offset;
} __attribute__((packed)) virtio_net_hdr_t;

// ─── Queue size (must match or be <= device-reported size) ───────────────────
// QEMU virtio-net defaults to 256. We use 256 to match exactly.
#define QUEUE_SIZE 256
#define RX_SLOTS (QUEUE_SIZE / 2) // each RX packet uses 2 descriptors

// ─── Flat queue memory layout (per virtio legacy spec) ───────────────────────
//
//  base (page-aligned):
//    [0]              desc table:   16 * QUEUE_SIZE bytes
//    [16*N]           avail ring:   6 + 2*N bytes  (flags, idx, ring[N])
//    [pad to 4K page] ...
//    [4096]           used ring:    6 + 8*N bytes  (flags, idx, ring[N])
//
// For N=256:
//   desc  = 4096 bytes  (exactly one page)
//   avail = 6 + 512 = 518 bytes
//   pad   = 4096 - 518 = 3578 bytes  → used starts at offset 8192
//   used  = 6 + 2048 = 2054 bytes
//   total = 8192 + 2054 = 10246 bytes → 3 pages

#define VRING_DESC_SIZE (16 * QUEUE_SIZE)
#define VRING_AVAIL_SIZE (6 + 2 * QUEUE_SIZE)
#define VRING_USED_OFFSET ((VRING_DESC_SIZE + VRING_AVAIL_SIZE + PAGE_SIZE - 1) & ~(PAGE_SIZE - 1))
#define VRING_USED_SIZE (6 + 8 * QUEUE_SIZE)
#define VRING_TOTAL (VRING_USED_OFFSET + VRING_USED_SIZE)
#define VRING_PAGES ((VRING_TOTAL + PAGE_SIZE - 1) / PAGE_SIZE)

// Raw byte arrays — aligned to page size, sized for 3 pages each
static uint8_t rxq_mem[VRING_PAGES * PAGE_SIZE] __attribute__((aligned(PAGE_SIZE)));
static uint8_t txq_mem[VRING_PAGES * PAGE_SIZE] __attribute__((aligned(PAGE_SIZE)));

typedef struct
{
    vring_desc_t *desc; // points into mem
    uint16_t *avail_flags;
    uint16_t *avail_idx;
    uint16_t *avail_ring;
    uint16_t *used_flags;
    uint16_t *used_idx;
    vring_used_elem_t *used_ring;
    uint16_t last_used;
    uint16_t free_head;
    uint16_t num_free;
} virtq_t;

static virtq_t rxq, txq;
static uint16_t io_base;

// RX buffers
#define RX_BUF_SIZE 1536
static virtio_net_hdr_t rx_hdrs[RX_SLOTS] __attribute__((aligned(16)));
static uint8_t rx_bufs[RX_SLOTS][RX_BUF_SIZE] __attribute__((aligned(16)));

// TX buffers
static virtio_net_hdr_t tx_hdr __attribute__((aligned(16)));
static uint8_t tx_data_buf[2048] __attribute__((aligned(16)));

// ─── I/O helpers ─────────────────────────────────────────────────────────────
static inline void outb(uint16_t p, uint8_t v) { __asm__ volatile("outb %0,%1" ::"a"(v), "Nd"(p)); }
static inline void outw(uint16_t p, uint16_t v) { __asm__ volatile("outw %0,%1" ::"a"(v), "Nd"(p)); }
static inline void outl(uint16_t p, uint32_t v) { __asm__ volatile("outl %0,%1" ::"a"(v), "Nd"(p)); }
static inline uint8_t inb(uint16_t p)
{
    uint8_t v;
    __asm__ volatile("inb %1,%0" : "=a"(v) : "Nd"(p));
    return v;
}
static inline uint16_t inw(uint16_t p)
{
    uint16_t v;
    __asm__ volatile("inw %1,%0" : "=a"(v) : "Nd"(p));
    return v;
}

uint64_t kvirt_to_phys(void *virt)
{
    uint64_t va = (uint64_t)virt;
    uint64_t cr3;
    __asm__ volatile("movq %%cr3,%0" : "=r"(cr3));

    uint64_t page = vmm_virt_to_phys(cr3, va & ~0xFFFULL) & 0x000FFFFFFFFFF000ULL;
    return page | (va & 0xFFFULL);
}

// ─── Queue init ───────────────────────────────────────────────────────────────

static void virtq_init(virtq_t *q, uint8_t *mem)
{
    // Pointers into flat memory
    q->desc = (vring_desc_t *)mem;
    q->avail_flags = (uint16_t *)(mem + VRING_DESC_SIZE);
    q->avail_idx = (uint16_t *)(mem + VRING_DESC_SIZE + 2);
    q->avail_ring = (uint16_t *)(mem + VRING_DESC_SIZE + 4);
    q->used_flags = (uint16_t *)(mem + VRING_USED_OFFSET);
    q->used_idx = (uint16_t *)(mem + VRING_USED_OFFSET + 2);
    q->used_ring = (vring_used_elem_t *)(mem + VRING_USED_OFFSET + 4);
    q->last_used = 0;
    q->free_head = 0;
    q->num_free = QUEUE_SIZE;

    // Link free descriptor chain
    for (int i = 0; i < QUEUE_SIZE - 1; i++)
        q->desc[i].next = i + 1;
    q->desc[QUEUE_SIZE - 1].next = 0;
}

static int init_queue(uint16_t idx, virtq_t *q, uint8_t *mem)
{
    outw(io_base + VIRTIO_PCI_QUEUE_SEL, idx);
    uint16_t size = inw(io_base + VIRTIO_PCI_QUEUE_SIZE);
    if (!size)
        return -1;
    if (size != QUEUE_SIZE)
    {
        kprintf("virtio-net: queue %u size=%u expected %u\n", idx, size, (uint16_t)QUEUE_SIZE);
        return -1;
    }

    // Zero queue memory
    for (size_t i = 0; i < VRING_PAGES * PAGE_SIZE; i++)
        mem[i] = 0;

    virtq_init(q, mem);

    uint64_t phys = kvirt_to_phys(mem);
    if (phys & 0xfff)
    {
        kprintf("virtio-net: queue not page-aligned!\n");
        return -1;
    }

    outl(io_base + VIRTIO_PCI_QUEUE_PFN, (uint32_t)(phys / PAGE_SIZE));
    return 0;
}

// ─── RX refill ────────────────────────────────────────────────────────────────

static void rx_refill(int slot)
{
    if (rxq.num_free < 2)
        return;

    uint16_t d0 = rxq.free_head;
    rxq.free_head = rxq.desc[d0].next;
    rxq.num_free--;

    uint16_t d1 = rxq.free_head;
    rxq.free_head = rxq.desc[d1].next;
    rxq.num_free--;

    rxq.desc[d0].addr = kvirt_to_phys(&rx_hdrs[slot]);
    rxq.desc[d0].len = sizeof(virtio_net_hdr_t);
    rxq.desc[d0].flags = VRING_DESC_F_WRITE | VRING_DESC_F_NEXT;
    rxq.desc[d0].next = d1;

    rxq.desc[d1].addr = kvirt_to_phys(rx_bufs[slot]);
    rxq.desc[d1].len = RX_BUF_SIZE;
    rxq.desc[d1].flags = VRING_DESC_F_WRITE;
    rxq.desc[d1].next = 0;

    uint16_t avail_pos = *rxq.avail_idx % QUEUE_SIZE;
    rxq.avail_ring[avail_pos] = d0;
    __asm__ volatile("" ::: "memory");
    (*rxq.avail_idx)++;
    __asm__ volatile("" ::: "memory");
}

// ─── TX ──────────────────────────────────────────────────────────────────────

static void virtio_send(const void *data, size_t len)
{
    if (txq.num_free < 2)
    {
        kprintf("virtio-net: tx full\n");
        return;
    }
    if (len > sizeof(tx_data_buf))
    {
        kprintf("virtio-net: packet too large\n");
        return;
    }

    uint8_t *dst = tx_data_buf;
    const uint8_t *src = (const uint8_t *)data;
    for (size_t i = 0; i < len; i++)
        dst[i] = src[i];

    uint8_t *h = (uint8_t *)&tx_hdr;
    for (size_t i = 0; i < sizeof(tx_hdr); i++)
        h[i] = 0;

    uint16_t d0 = txq.free_head;
    txq.free_head = txq.desc[d0].next;
    txq.num_free--;

    uint16_t d1 = txq.free_head;
    txq.free_head = txq.desc[d1].next;
    txq.num_free--;

    txq.desc[d0].addr = kvirt_to_phys(&tx_hdr);
    txq.desc[d0].len = sizeof(virtio_net_hdr_t);
    txq.desc[d0].flags = VRING_DESC_F_NEXT;
    txq.desc[d0].next = d1;

    txq.desc[d1].addr = kvirt_to_phys(tx_data_buf);
    txq.desc[d1].len = len;
    txq.desc[d1].flags = 0;
    txq.desc[d1].next = 0;

    uint16_t avail_pos = *txq.avail_idx % QUEUE_SIZE;
    txq.avail_ring[avail_pos] = d0;
    __asm__ volatile("" ::: "memory");
    (*txq.avail_idx)++;
    __asm__ volatile("" ::: "memory");

    outw(io_base + VIRTIO_PCI_QUEUE_NOTIFY, 1);
}

// ─── RX poll / IRQ ───────────────────────────────────────────────────────────

void virtio_net_poll(void)
{
    while (rxq.last_used != *rxq.used_idx)
    {
        vring_used_elem_t *e = &rxq.used_ring[rxq.last_used % QUEUE_SIZE];
        uint16_t d = (uint16_t)e->id;
        uint32_t plen = e->len > sizeof(virtio_net_hdr_t)
                            ? e->len - sizeof(virtio_net_hdr_t)
                            : 0;

        // Find which RX slot this head descriptor belongs to
        for (int i = 0; i < RX_SLOTS; i++)
        {
            if (rxq.desc[d].addr == kvirt_to_phys(&rx_hdrs[i]))
            {
                if (plen > 0)
                    net_receive(rx_bufs[i], plen);
                // Return both descriptors to free list
                uint16_t d1 = rxq.desc[d].next;
                rxq.desc[d1].next = rxq.free_head;
                rxq.free_head = d1;
                rxq.num_free++;
                rxq.desc[d].next = rxq.free_head;
                rxq.free_head = d;
                rxq.num_free++;
                rx_refill(i);
                break;
            }
        }
        rxq.last_used++;
    }

    // Reclaim TX descriptors
    while (txq.last_used != *txq.used_idx)
    {
        vring_used_elem_t *e = &txq.used_ring[txq.last_used % QUEUE_SIZE];
        uint16_t d = (uint16_t)e->id;
        uint16_t d1 = txq.desc[d].next;
        txq.desc[d].next = txq.free_head;
        txq.free_head = d;
        txq.num_free++;
        txq.desc[d1].next = txq.free_head;
        txq.free_head = d1;
        txq.num_free++;
        txq.last_used++;
    }
}

static void virtio_irq(registers_t *r)
{
    (void)r;
    (void)inb(io_base + VIRTIO_PCI_ISR);
}

// ─── Init ─────────────────────────────────────────────────────────────────────

int virtio_net_init(void)
{
    kprintf("virtio-net: enter\n");

    pci_device_t dev;
    if (pci_find(PCI_VENDOR_VIRTIO, PCI_DEVICE_NET, &dev) < 0)
    {
        kprintf("virtio-net: not found\n");
        return -1;
    }
    kprintf("virtio-net: found bar0=%x irq=%u\n", dev.bar[0], dev.irq);

    if (!(dev.bar[0] & 1))
    {
        kprintf("virtio-net: no I/O BAR - skip\n");
        return -1;
    }
    io_base = (uint16_t)(dev.bar[0] & ~0x3u);
    kprintf("virtio-net: io_base=%x\n", io_base);

    pci_enable_busmaster(&dev);
    kprintf("virtio-net: busmaster ok\n");

    // Reset
    outb(io_base + VIRTIO_PCI_STATUS, 0);
    kprintf("virtio-net: reset ok\n");
    outb(io_base + VIRTIO_PCI_STATUS, VIRTIO_STATUS_ACKNOWLEDGE);
    outb(io_base + VIRTIO_PCI_STATUS, VIRTIO_STATUS_ACKNOWLEDGE | VIRTIO_STATUS_DRIVER);

    // Negotiate MAC feature only
    uint32_t host_feat = 0;
    __asm__ volatile("inl %1,%0" : "=a"(host_feat) : "Nd"((uint16_t)(io_base + VIRTIO_PCI_HOST_FEATURES)));
    kprintf("virtio-net: host_feat=%x\n", host_feat);

    uint32_t our_feat = host_feat & VIRTIO_NET_F_MAC;
    __asm__ volatile("outl %0,%1" ::"a"(our_feat), "Nd"((uint16_t)(io_base + VIRTIO_PCI_GUEST_FEATURES)));
    kprintf("virtio-net: guest features set\n");

    // Read MAC
    for (int i = 0; i < 6; i++)
        netif.mac.b[i] = inb(io_base + VIRTIO_PCI_CONFIG + i);
    kprintf("virtio-net: mac ok\n");

    // Setup queues
    if (init_queue(0, &rxq, rxq_mem) < 0)
    {
        kprintf("virtio-net: rx queue fail\n");
        return -1;
    }
    kprintf("virtio-net: rx queue ok\n");

    if (init_queue(1, &txq, txq_mem) < 0)
    {
        kprintf("virtio-net: tx queue fail\n");
        return -1;
    }
    kprintf("virtio-net: tx queue ok\n");

    // Fill RX queue with all slots
    for (int i = 0; i < RX_SLOTS; i++)
        rx_refill(i);
    kprintf("virtio-net: rx filled\n");

    // Driver ready
    outb(io_base + VIRTIO_PCI_STATUS,
         VIRTIO_STATUS_ACKNOWLEDGE | VIRTIO_STATUS_DRIVER | VIRTIO_STATUS_DRIVER_OK);
    kprintf("virtio-net: DRIVER_OK\n");

    // Configure interface
    netif.ip.b[0] = 10;
    netif.ip.b[1] = 0;
    netif.ip.b[2] = 2;
    netif.ip.b[3] = 15;
    netif.gateway.b[0] = 10;
    netif.gateway.b[1] = 0;
    netif.gateway.b[2] = 2;
    netif.gateway.b[3] = 2;
    netif.netmask.b[0] = 255;
    netif.netmask.b[1] = 255;
    netif.netmask.b[2] = 255;
    netif.netmask.b[3] = 0;
    netif.send = virtio_send;

    kprintf("virtio-net: net_init...\n");
    net_init();
    kprintf("virtio-net: done\n");
    return 0;
}