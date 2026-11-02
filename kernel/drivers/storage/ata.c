#include "Kernel.h"

#define ATA_DATA 0x1F0
#define ATA_SECTOR_CNT 0x1F2
#define ATA_LBA_LO 0x1F3
#define ATA_LBA_MID 0x1F4
#define ATA_LBA_HI 0x1F5
#define ATA_DRIVE 0x1F6
#define ATA_CMD 0x1F7
#define ATA_ALT_STATUS 0x3F6

#define ATA_CMD_READ 0x20
#define ATA_CMD_WRITE 0x30
#define ATA_CMD_FLUSH 0xE7

#define ATA_SR_BSY 0x80
#define ATA_SR_DRQ 0x08
#define ATA_SR_ERR 0x01
#define ATA_SR_DF 0x20

static inline void outb(uint16_t port, uint8_t val)
{
    __asm__ volatile("outb %0,%1" ::"a"(val), "Nd"(port));
}
static inline uint8_t inb(uint16_t port)
{
    uint8_t v;
    __asm__ volatile("inb %1,%0" : "=a"(v) : "Nd"(port));
    return v;
}
static inline uint16_t inw(uint16_t port)
{
    uint16_t v;
    __asm__ volatile("inw %1,%0" : "=a"(v) : "Nd"(port));
    return v;
}
static inline void outw(uint16_t port, uint16_t val)
{
    __asm__ volatile("outw %0,%1" ::"a"(val), "Nd"(port));
}

static void ata_delay(void)
{
    inb(ATA_ALT_STATUS);
    inb(ATA_ALT_STATUS);
    inb(ATA_ALT_STATUS);
    inb(ATA_ALT_STATUS);
}

static int ata_poll(void)
{
    /* Wait up to ~30s for BSY to clear */
    for (int i = 0; i < 300000000; i++)
    {
        uint8_t s = inb(ATA_ALT_STATUS);
        if (s & ATA_SR_ERR)
        {
            kprintf("ata: ERR set (status=0x%x)\n", s);
            return 0;
        }
        if (s & ATA_SR_DF)
        {
            kprintf("ata: DF set (status=0x%x)\n", s);
            return 0;
        }
        if (!(s & ATA_SR_BSY) && (s & ATA_SR_DRQ))
            return 1;
    }
    kprintf("ata: poll timeout (status=0x%x)\n", inb(ATA_ALT_STATUS));
    return 0;
}

static int ata_wait_done(void)
{
    for (int i = 0; i < 300000000; i++)
    {
        uint8_t s = inb(ATA_ALT_STATUS);
        if (s & ATA_SR_ERR)
        {
            kprintf("ata: ERR (status=0x%x)\n", s);
            return 0;
        }
        if (!(s & ATA_SR_BSY))
            return 1;
    }
    kprintf("ata: wait_done timeout\n");
    return 0;
}

bool ata_init(void)
{
    /* Select master drive */
    outb(ATA_DRIVE, 0xA0);
    ata_delay();

    /* Software reset */
    outb(ATA_ALT_STATUS, 0x04);
    for (volatile int i = 0; i < 1000000; i++)
        ;
    outb(ATA_ALT_STATUS, 0x00);

    /* Wait for BSY to clear after reset */
    for (int i = 0; i < 300000000; i++)
    {
        uint8_t s = inb(ATA_ALT_STATUS);
        if (!(s & ATA_SR_BSY))
            break;
    }

    uint8_t status = inb(ATA_ALT_STATUS);
    kprintf("ata: ready (status=0x%x)\n", status);
    return true;
}

static void ata_setup(uint32_t lba, uint8_t count)
{
    outb(ATA_DRIVE, 0xE0 | ((lba >> 24) & 0x0F));
    ata_delay();
    outb(ATA_SECTOR_CNT, count);
    outb(ATA_LBA_LO, (uint8_t)(lba));
    outb(ATA_LBA_MID, (uint8_t)(lba >> 8));
    outb(ATA_LBA_HI, (uint8_t)(lba >> 16));
}

bool ata_read28(uint32_t lba, uint8_t count, void *buffer)
{
    ata_setup(lba, count);
    outb(ATA_CMD, ATA_CMD_READ);
    ata_delay();

    uint16_t *buf = (uint16_t *)buffer;
    for (int s = 0; s < count; s++)
    {
        if (!ata_poll())
        {
            kprintf("ata: read failed lba=%d s=%d\n", lba, s);
            return false;
        }
        for (int i = 0; i < 256; i++)
            buf[i] = inw(ATA_DATA);
        buf += 256;
        ata_delay();
    }
    return true;
}

bool ata_write28(uint32_t lba, uint8_t count, const void *buffer)
{
    const uint16_t *buf = (const uint16_t *)buffer;
    for (int s = 0; s < count; s++)
    {
        /* Issue one sector at a time */
        ata_setup(lba + s, 1);
        outb(ATA_CMD, ATA_CMD_WRITE);
        ata_delay();

        if (!ata_poll())
        {
            kprintf("ata: write failed lba=%d\n", lba + s);
            return false;
        }
        for (int i = 0; i < 256; i++)
            outw(ATA_DATA, buf[i]);
        buf += 256;
        ata_delay();

        /* Flush after each sector */
        outb(ATA_CMD, ATA_CMD_FLUSH);
        if (!ata_wait_done())
        {
            kprintf("ata: flush failed lba=%d\n", lba + s);
            return false;
        }
    }
    return true;
}