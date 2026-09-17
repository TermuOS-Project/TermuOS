#include "../net/pci.h"
#include "../../lib/printf.h"
#include "../../mm/vmm.h"
#include <stdint.h>
#include <stddef.h>

#define PAGE_SIZE 4096
#define PHYS_MASK 0x000FFFFFFFFFF000ULL

#define VIRTIO_STATUS_ACKNOWLEDGE 1
#define VIRTIO_STATUS_DRIVER 2
#define VIRTIO_STATUS_FEATURES_OK 8
#define VIRTIO_STATUS_DRIVER_OK 4

#define VRING_DESC_F_NEXT 1
#define VRING_DESC_F_WRITE 2

#define VIRTIO_GPU_CMD_GET_DISPLAY_INFO 0x100
#define VIRTIO_GPU_RESP_OK_DISPLAY_INFO 0x101

#define CFG_DFSEL 0x08
#define CFG_DFEATURE 0x0c
#define CFG_STATUS 0x14
#define CFG_QSEL 0x16
#define CFG_QSIZE 0x18
#define CFG_QENABLE 0x1c
#define CFG_QNOTIFY 0x1e
#define CFG_QDESC 0x20
#define CFG_QDRIVER 0x28
#define CFG_QDEVICE 0x30

struct virtio_gpu_ctrl_hdr
{
    uint32_t type;
    uint32_t flags;
    uint64_t fence_id;
    uint32_t ctx_id;
    uint32_t ring_idx;
    uint32_t padding;
} __attribute__((packed));

struct virtio_gpu_rect
{
    uint32_t x, y, width, height;
} __attribute__((packed));

struct virtio_gpu_display_one
{
    struct virtio_gpu_rect r;
    uint32_t enabled;
    uint32_t flags;
} __attribute__((packed));

struct virtio_gpu_resp_display_info
{
    struct virtio_gpu_ctrl_hdr hdr;
    struct virtio_gpu_display_one pmodes[16];
} __attribute__((packed));

struct vring_desc
{
    uint64_t addr;
    uint32_t len;
    uint16_t flags;
    uint16_t next;
} __attribute__((packed));

extern uint64_t kvirt_to_phys(void *virt);
extern uint64_t termuos_hhdm_base(void);

static uint8_t qmem[64 * 1024] __attribute__((aligned(4096)));
static struct virtio_gpu_ctrl_hdr req __attribute__((aligned(16)));
static struct virtio_gpu_resp_display_info resp __attribute__((aligned(16)));

static inline void mmio_w8(volatile uint8_t *b, uint32_t off, uint8_t v)
{
    *(volatile uint8_t *)(b + off) = v;
}
static inline uint8_t mmio_r8(volatile uint8_t *b, uint32_t off)
{
    return *(volatile uint8_t *)(b + off);
}
static inline void mmio_w16(volatile uint8_t *b, uint32_t off, uint16_t v)
{
    *(volatile uint16_t *)(b + off) = v;
}
static inline uint16_t mmio_r16(volatile uint8_t *b, uint32_t off)
{
    return *(volatile uint16_t *)(b + off);
}
static inline void mmio_w32(volatile uint8_t *b, uint32_t off, uint32_t v)
{
    *(volatile uint32_t *)(b + off) = v;
}
static inline void mmio_w64(volatile uint8_t *b, uint32_t off, uint64_t v)
{
    *(volatile uint64_t *)(b + off) = v;
}

static uint64_t clean_phys(void *v)
{
    return kvirt_to_phys(v) & PHYS_MASK;
}

/*
 * common = mapped common cfg
 * notify = mapped notify base (cap offset already applied)
 * notify_mult = multiplier from notify cap
 */
int virtio_gpu_vq_init_and_get_display(volatile uint8_t *common,
                                       volatile uint8_t *notify,
                                       uint32_t notify_mult)
{
    if (!common || !notify)
        return -1;

    /* VERSION_1 */
    mmio_w32(common, CFG_DFSEL, 1);
    mmio_w32(common, CFG_DFEATURE, 1);
    mmio_w32(common, CFG_DFSEL, 0);
    mmio_w32(common, CFG_DFEATURE, 0);

    uint8_t st = mmio_r8(common, CFG_STATUS);
    mmio_w8(common, CFG_STATUS, st | VIRTIO_STATUS_FEATURES_OK);
    if (!(mmio_r8(common, CFG_STATUS) & VIRTIO_STATUS_FEATURES_OK))
    {
        kprintf("virtio-gpu: FEATURES_OK rejected\n");
        return -1;
    }

    mmio_w16(common, CFG_QSEL, 0);
    uint16_t qsz = mmio_r16(common, CFG_QSIZE);
    if (qsz == 0)
        return -1;
    if (qsz > 64)
    {
        qsz = 64;
        mmio_w16(common, CFG_QSIZE, qsz);
    }

    for (size_t i = 0; i < sizeof(qmem); i++)
        qmem[i] = 0;

    uint32_t desc_bytes = 16u * qsz;
    uint32_t avail_off = desc_bytes;
    uint32_t used_off = (desc_bytes + 4 + 2 * qsz + 0xfff) & ~0xfffu;

    uint64_t desc_p = clean_phys(&qmem[0]);
    uint64_t avail_p = clean_phys(&qmem[avail_off]);
    uint64_t used_p = clean_phys(&qmem[used_off]);

    kprintf("virtio-gpu: desc=%x avail=%x used=%x qsz=%u\n",
            (uint32_t)desc_p, (uint32_t)avail_p, (uint32_t)used_p, qsz);

    mmio_w64(common, CFG_QDESC, desc_p);
    mmio_w64(common, CFG_QDRIVER, avail_p);
    mmio_w64(common, CFG_QDEVICE, used_p);
    mmio_w16(common, CFG_QENABLE, 1);

    st = mmio_r8(common, CFG_STATUS);
    mmio_w8(common, CFG_STATUS, st | VIRTIO_STATUS_DRIVER_OK);

    /* --- GET_DISPLAY_INFO --- */
    for (size_t i = 0; i < sizeof(req); i++)
        ((uint8_t *)&req)[i] = 0;
    for (size_t i = 0; i < sizeof(resp); i++)
        ((uint8_t *)&resp)[i] = 0;
    req.type = VIRTIO_GPU_CMD_GET_DISPLAY_INFO;

    struct vring_desc *desc = (struct vring_desc *)&qmem[0];
    desc[0].addr = clean_phys(&req);
    desc[0].len = sizeof(req);
    desc[0].flags = VRING_DESC_F_NEXT;
    desc[0].next = 1;
    desc[1].addr = clean_phys(&resp);
    desc[1].len = sizeof(resp);
    desc[1].flags = VRING_DESC_F_WRITE;
    desc[1].next = 0;

    uint16_t *avail_flags = (uint16_t *)&qmem[avail_off];
    uint16_t *avail_idx = (uint16_t *)&qmem[avail_off + 2];
    uint16_t *avail_ring = (uint16_t *)&qmem[avail_off + 4];
    uint16_t *used_idx = (uint16_t *)&qmem[used_off + 2];

    uint16_t before = *used_idx;
    uint16_t aidx = *avail_idx;
    avail_ring[aidx % qsz] = 0;
    __sync_synchronize();
    *avail_idx = (uint16_t)(aidx + 1);
    __sync_synchronize();

    mmio_w16(common, CFG_QSEL, 0);
    uint16_t noff = mmio_r16(common, CFG_QNOTIFY);
    if (notify_mult == 0)
        notify_mult = 1;
    *(volatile uint32_t *)(notify + (uint32_t)noff * notify_mult) = 0;

    for (volatile uint32_t i = 0; i < 50000000u; i++)
    {
        if (*used_idx != before)
            break;
    }
    if (*used_idx == before)
    {
        kprintf("virtio-gpu: phase4 timeout (C)\n");
        return -1;
    }

    if (resp.hdr.type != VIRTIO_GPU_RESP_OK_DISPLAY_INFO)
    {
        kprintf("virtio-gpu: bad resp type 0x%x\n", resp.hdr.type);
        return -1;
    }

    kprintf("virtio-gpu: phase4 ok %ux%u\n",
            resp.pmodes[0].r.width, resp.pmodes[0].r.height);
    return 0;
}
