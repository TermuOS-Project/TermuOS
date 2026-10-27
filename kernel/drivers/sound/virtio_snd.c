#include "virtio_snd.h"
#include "../driver.h"
#include "../net/pci.h"
#include "../../lib/printf.h"
#include "../../mm/vmm.h"
#include <stdint.h>
#include <stddef.h>

#define VIRTIO_VENDOR     0x1AF4
#define VIRTIO_SND_DEVICE 0x1059

#define PCI_CAP_ID_VNDR           0x09
#define VIRTIO_PCI_CAP_COMMON_CFG 1
#define VIRTIO_PCI_CAP_NOTIFY_CFG 2
#define VIRTIO_PCI_CAP_ISR_CFG    3
#define VIRTIO_PCI_CAP_DEVICE_CFG 4

#define VIRTIO_STATUS_ACKNOWLEDGE 1
#define VIRTIO_STATUS_DRIVER      2
#define VIRTIO_STATUS_DRIVER_OK   4
#define VIRTIO_STATUS_FEATURES_OK 8

#define CFG_DFSEL    0x08
#define CFG_DFEATURE 0x0c
#define CFG_STATUS   0x14
#define CFG_QSEL     0x16
#define CFG_QSIZE    0x18
#define CFG_QENABLE  0x1c
#define CFG_QNOTIFY  0x1e
#define CFG_QDESC    0x20
#define CFG_QDRIVER  0x28
#define CFG_QDEVICE  0x30

#define Q_CONTROL 0
#define Q_EVENT   1
#define Q_TX      2

#define VRING_DESC_F_NEXT  1
#define VRING_DESC_F_WRITE 2

#define VIRTIO_SND_R_PCM_INFO       0x0100u
#define VIRTIO_SND_R_PCM_SET_PARAMS 0x0101u
#define VIRTIO_SND_R_PCM_PREPARE    0x0102u
#define VIRTIO_SND_R_PCM_START      0x0104u
#define VIRTIO_SND_S_OK             0x80000000u

#define VIRTIO_SND_PCM_FMT_S16    5
#define VIRTIO_SND_PCM_RATE_44100 6
#define VIRTIO_SND_PCM_RATE_48000 7

#define PHYS_MASK 0x000FFFFFFFFFF000ULL
#define Q_SLICE   (16u * 1024u)
#define CTRL_OUT_OFF (120u * 1024u)
#define CTRL_IN_OFF  (120u * 1024u + 512u)

struct virtio_snd_hdr {
    uint32_t code;
} __attribute__((packed));

struct virtio_snd_query_info {
    struct virtio_snd_hdr hdr;
    uint32_t start_id;
    uint32_t count;
    uint32_t size;
} __attribute__((packed));

struct virtio_snd_info {
    uint32_t hda_fn_nid;
} __attribute__((packed));

struct virtio_snd_pcm_info {
    struct virtio_snd_info hdr;
    uint32_t features;
    uint64_t formats;
    uint64_t rates;
    uint8_t direction;
    uint8_t channels_min;
    uint8_t channels_max;
    uint8_t padding[5];
} __attribute__((packed));

struct virtio_snd_pcm_set_params {
    struct virtio_snd_hdr hdr;
    uint32_t stream_id;
    uint32_t buffer_bytes;
    uint32_t period_bytes;
    uint32_t channels;
    uint8_t format;
    uint8_t rate;
    uint8_t padding[10];
} __attribute__((packed));

struct virtio_snd_pcm_hdr {
    struct virtio_snd_hdr hdr;
    uint32_t stream_id;
} __attribute__((packed));

struct vring_desc {
    uint64_t addr;
    uint32_t len;
    uint16_t flags;
    uint16_t next;
} __attribute__((packed));

static uint8_t qmem[128 * 1024] __attribute__((aligned(4096)));

static volatile uint8_t *g_common;
static volatile uint8_t *g_notify;
static uint32_t g_notify_mult = 1;

static uint8_t *g_ctrl_mem;
static uint16_t g_ctrl_qsz;
static uint32_t g_ctrl_avail_off;
static uint32_t g_ctrl_used_off;

static inline uint8_t pci_read8(uint8_t bus, uint8_t slot, uint8_t func, uint8_t off)
{
    uint32_t v = pci_read(bus, slot, func, off & 0xfc);
    return (uint8_t)(v >> (8 * (off & 3)));
}

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

static volatile uint8_t *map_bar(uint32_t bar_raw)
{
    if (bar_raw & 1)
        return 0;
    uint64_t phys = (uint64_t)(bar_raw & ~0xfu);
    if (!phys)
        return 0;
    return (volatile uint8_t *)PHYS_TO_VIRT(phys);
}

static uint64_t clean_phys(void *p)
{
    return VIRT_TO_PHYS(p) & PHYS_MASK;
}

static int setup_queue(uint16_t qidx, uint8_t *mem)
{
    mmio_w16(g_common, CFG_QSEL, qidx);
    uint16_t qsz = mmio_r16(g_common, CFG_QSIZE);
    if (qsz == 0) {
        kprintf("virtio-snd: queue %u size 0\n", qidx);
        return -1;
    }
    if (qsz > 64)
        qsz = 64;
    mmio_w16(g_common, CFG_QSIZE, qsz);

    uint32_t desc_bytes = 16u * qsz;
    uint32_t avail_off = (desc_bytes + 15u) & ~15u;
    uint32_t avail_bytes = 6u + 2u * qsz;
    uint32_t used_off = (avail_off + avail_bytes + 4095u) & ~4095u;

    for (uint32_t i = 0; i < Q_SLICE; i++)
        mem[i] = 0;

    uint64_t desc_p = clean_phys(mem);
    uint64_t avail_p = clean_phys(mem + avail_off);
    uint64_t used_p = clean_phys(mem + used_off);

    mmio_w64(g_common, CFG_QDESC, desc_p);
    mmio_w64(g_common, CFG_QDRIVER, avail_p);
    mmio_w64(g_common, CFG_QDEVICE, used_p);
    mmio_w16(g_common, 0x1a, 0xffff);
    mmio_w16(g_common, CFG_QENABLE, 1);

    kprintf("virtio-snd: q%u qsz=%u desc=%x avail=%x used=%x en=%u\n",
            qidx, qsz,
            (uint32_t)desc_p, (uint32_t)avail_p, (uint32_t)used_p,
            mmio_r16(g_common, CFG_QENABLE));

    if (qidx == Q_CONTROL) {
        g_ctrl_mem = mem;
        g_ctrl_qsz = qsz;
        g_ctrl_avail_off = avail_off;
        g_ctrl_used_off = used_off;
    }
    return 0;
}

static int negotiate_features(void)
{
    mmio_w32(g_common, CFG_DFSEL, 0);
    mmio_w32(g_common, CFG_DFEATURE, 0);
    mmio_w32(g_common, CFG_DFSEL, 1);
    mmio_w32(g_common, CFG_DFEATURE, 0);

    uint8_t st = mmio_r8(g_common, CFG_STATUS);
    mmio_w8(g_common, CFG_STATUS, st | VIRTIO_STATUS_FEATURES_OK);
    if (!(mmio_r8(g_common, CFG_STATUS) & VIRTIO_STATUS_FEATURES_OK)) {
        kprintf("virtio-snd: FEATURES_OK rejected\n");
        return -1;
    }
    kprintf("virtio-snd: FEATURES_OK\n");
    return 0;
}

static int ctrl_submit(const void *req, uint32_t req_len, void *resp, uint32_t resp_len)
{
    if (!g_common || !g_ctrl_mem || req_len > 256 || resp_len > 512)
        return -1;

    uint8_t *out = qmem + CTRL_OUT_OFF;
    uint8_t *in = qmem + CTRL_IN_OFF;

    for (uint32_t i = 0; i < req_len; i++)
        out[i] = ((const uint8_t *)req)[i];
    for (uint32_t i = 0; i < resp_len; i++)
        in[i] = 0;

    struct vring_desc *desc = (struct vring_desc *)g_ctrl_mem;
    desc[0].addr = clean_phys(out);
    desc[0].len = req_len;
    desc[0].flags = VRING_DESC_F_NEXT;
    desc[0].next = 1;
    desc[1].addr = clean_phys(in);
    desc[1].len = resp_len;
    desc[1].flags = VRING_DESC_F_WRITE;
    desc[1].next = 0;

    volatile uint16_t *avail_idx =
        (volatile uint16_t *)(g_ctrl_mem + g_ctrl_avail_off + 2);
    volatile uint16_t *avail_ring =
        (volatile uint16_t *)(g_ctrl_mem + g_ctrl_avail_off + 4);
    volatile uint16_t *used_idx =
        (volatile uint16_t *)(g_ctrl_mem + g_ctrl_used_off + 2);

    uint16_t before = *used_idx;
    uint16_t aidx = *avail_idx;
    avail_ring[aidx % g_ctrl_qsz] = 0;
    __sync_synchronize();
    *avail_idx = (uint16_t)(aidx + 1);
    __sync_synchronize();

    mmio_w16(g_common, CFG_QSEL, Q_CONTROL);
    uint16_t noff = mmio_r16(g_common, CFG_QNOTIFY);
    uint32_t mult = g_notify_mult ? g_notify_mult : 1;
    volatile uint8_t *naddr = g_notify + (uint32_t)noff * mult;
    *(volatile uint16_t *)naddr = Q_CONTROL;

    for (volatile uint32_t i = 0; i < 50000000u; i++)
        if (*used_idx != before)
            break;

    if (*used_idx == before) {
        kprintf("virtio-snd: ctrl timeout\n");
        return -1;
    }

    if (resp && resp_len) {
        for (uint32_t i = 0; i < resp_len; i++)
            ((uint8_t *)resp)[i] = in[i];
    }
    return (int)(*(uint32_t *)in);
}

static int pcm_query(void)
{
    struct virtio_snd_query_info q;
    uint8_t resp[sizeof(struct virtio_snd_hdr) + sizeof(struct virtio_snd_pcm_info)];

    q.hdr.code = VIRTIO_SND_R_PCM_INFO;
    q.start_id = 0;
    q.count = 1;
    q.size = (uint32_t)sizeof(struct virtio_snd_pcm_info);

    int rc = ctrl_submit(&q, sizeof q, resp, sizeof resp);
    kprintf("virtio-snd: PCM_INFO resp=0x%x\n", (unsigned)rc);
    if ((uint32_t)rc != VIRTIO_SND_S_OK)
        return -1;

    struct virtio_snd_pcm_info *info =
        (struct virtio_snd_pcm_info *)(resp + sizeof(struct virtio_snd_hdr));
    kprintf("virtio-snd: ch %u-%u dir=%u fmt=%x rate=%x\n",
            info->channels_min, info->channels_max, info->direction,
            (unsigned)info->formats, (unsigned)info->rates);
    return 0;
}

static int pcm_set_params(uint32_t stream, uint32_t channels, uint8_t fmt, uint8_t rate)
{
    struct virtio_snd_pcm_set_params p;
    struct virtio_snd_hdr resp;
    unsigned i;

    for (i = 0; i < sizeof p; i++)
        ((uint8_t *)&p)[i] = 0;

    p.hdr.code = VIRTIO_SND_R_PCM_SET_PARAMS;
    p.stream_id = stream;
    p.buffer_bytes = 8192;
    p.period_bytes = 2048;
    p.channels = channels;
    p.format = fmt;
    p.rate = rate;

    int rc = ctrl_submit(&p, sizeof p, &resp, sizeof resp);
    kprintf("virtio-snd: SET_PARAMS resp=0x%x\n", (unsigned)rc);
    return (uint32_t)rc == VIRTIO_SND_S_OK ? 0 : -1;
}

static int pcm_simple(uint32_t code, uint32_t stream)
{
    struct virtio_snd_pcm_hdr h;
    struct virtio_snd_hdr resp;

    h.hdr.code = code;
    h.stream_id = stream;
    int rc = ctrl_submit(&h, sizeof h, &resp, sizeof resp);
    kprintf("virtio-snd: cmd 0x%x resp=0x%x\n", code, (unsigned)rc);
    return (uint32_t)rc == VIRTIO_SND_S_OK ? 0 : -1;
}

static int virtio_snd_init(void)
{
    pci_device_t dev;
    if (pci_find(VIRTIO_VENDOR, VIRTIO_SND_DEVICE, &dev) != 0) {
        kprintf("virtio-snd: not found\n");
        return -1;
    }
    kprintf("virtio-snd: pci %u:%u.0\n", dev.bus, dev.slot);

    uint32_t cmd = pci_read(dev.bus, dev.slot, dev.func, 0x04);
    cmd |= (1u << 1) | (1u << 2);
    pci_write(dev.bus, dev.slot, dev.func, 0x04, cmd);

    uint8_t common_bar = 0xff, notify_bar = 0xff;
    uint32_t common_off = 0, notify_off = 0;
    uint32_t notify_mult = 4;

    uint8_t cap = pci_read8(dev.bus, dev.slot, dev.func, 0x34);
    while (cap) {
        uint8_t id = pci_read8(dev.bus, dev.slot, dev.func, cap);
        if (id == PCI_CAP_ID_VNDR) {
            uint8_t cfg_type =
                pci_read8(dev.bus, dev.slot, dev.func, (uint8_t)(cap + 3));
            uint8_t bar =
                pci_read8(dev.bus, dev.slot, dev.func, (uint8_t)(cap + 4));
            uint32_t off =
                pci_read(dev.bus, dev.slot, dev.func, (uint8_t)((cap + 8) & 0xfc));

            if (cfg_type == VIRTIO_PCI_CAP_COMMON_CFG) {
                common_bar = bar;
                common_off = off;
                kprintf("virtio-snd: cap common bar=%u off=0x%x\n", bar, off);
            } else if (cfg_type == VIRTIO_PCI_CAP_NOTIFY_CFG) {
                notify_bar = bar;
                notify_off = off;
                uint32_t mult = pci_read(dev.bus, dev.slot, dev.func,
                                         (uint8_t)((cap + 16) & 0xfc));
                if (mult)
                    notify_mult = mult;
                kprintf("virtio-snd: cap notify bar=%u off=0x%x mult=%u\n",
                        bar, off, notify_mult);
            } else if (cfg_type == VIRTIO_PCI_CAP_ISR_CFG) {
                kprintf("virtio-snd: cap isr\n");
            } else if (cfg_type == VIRTIO_PCI_CAP_DEVICE_CFG) {
                kprintf("virtio-snd: cap device\n");
            } else {
                kprintf("virtio-snd: cap other type=%u\n", cfg_type);
            }
        }
        cap = pci_read8(dev.bus, dev.slot, dev.func, (uint8_t)(cap + 1));
    }

    if (common_bar >= 6 || notify_bar >= 6) {
        kprintf("virtio-snd: missing common/notify\n");
        return -1;
    }

    volatile uint8_t *cbase = map_bar(dev.bar[common_bar]);
    volatile uint8_t *nbase = map_bar(dev.bar[notify_bar]);
    if (!cbase || !nbase) {
        kprintf("virtio-snd: bar map failed\n");
        return -1;
    }

    g_common = cbase + common_off;
    g_notify = nbase + notify_off;
    g_notify_mult = notify_mult ? notify_mult : 1;

    mmio_w8(g_common, CFG_STATUS, 0);
    mmio_w8(g_common, CFG_STATUS, VIRTIO_STATUS_ACKNOWLEDGE);
    mmio_w8(g_common, CFG_STATUS,
            VIRTIO_STATUS_ACKNOWLEDGE | VIRTIO_STATUS_DRIVER);
    kprintf("virtio-snd: phase1 ok (ACK|DRIVER)\n");

    if (negotiate_features() != 0)
        return -1;

    mmio_w16(g_common, 0x10, 0xffff);

    if (setup_queue(Q_CONTROL, qmem + 0 * Q_SLICE) != 0)
        return -1;
    if (setup_queue(Q_EVENT, qmem + 1 * Q_SLICE) != 0)
        return -1;
    if (setup_queue(Q_TX, qmem + 2 * Q_SLICE) != 0)
        return -1;

    uint8_t st = mmio_r8(g_common, CFG_STATUS);
    mmio_w8(g_common, CFG_STATUS, st | VIRTIO_STATUS_DRIVER_OK);
    kprintf("virtio-snd: phase2 ok (DRIVER_OK) status=0x%x\n",
            mmio_r8(g_common, CFG_STATUS));

    if (pcm_query() != 0)
        return -1;

    if (pcm_set_params(0, 2, VIRTIO_SND_PCM_FMT_S16, VIRTIO_SND_PCM_RATE_48000) != 0) {
        kprintf("virtio-snd: retry SET_PARAMS mono/44100\n");
        if (pcm_set_params(0, 1, VIRTIO_SND_PCM_FMT_S16, VIRTIO_SND_PCM_RATE_44100) != 0)
            return -1;
    }

    if (pcm_simple(VIRTIO_SND_R_PCM_PREPARE, 0) != 0)
        return -1;
    if (pcm_simple(VIRTIO_SND_R_PCM_START, 0) != 0)
        return -1;

    kprintf("virtio-snd: phase3 ok (PCM started)\n");
    return 0;
}

void virtio_snd_driver_register(void)
{
    static const driver_t d = {
        .name = "virtio-snd",
        .init = virtio_snd_init,
        .priority = 55,
    };
    driver_register(&d);
}
