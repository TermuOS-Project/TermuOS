#include "Kernel.h"

// 8259 PIC ports
#define PIC1_CMD 0x20
#define PIC1_DATA 0x21
#define PIC2_CMD 0xA0
#define PIC2_DATA 0xA1
#define PIC_EOI 0x20

// Remap IRQs to vectors 32-47 (above CPU exceptions 0-31)
#define PIC1_OFFSET 0x20 // IRQ 0-7  -> vectors 32-39
#define PIC2_OFFSET 0x28 // IRQ 8-15 -> vectors 40-47

static inline void outb(uint16_t port, uint8_t val)
{
    __asm__ volatile("outb %0, %1" ::"a"(val), "Nd"(port));
}

static inline uint8_t inb(uint16_t port)
{
    uint8_t val;
    __asm__ volatile("inb %1, %0" : "=a"(val) : "Nd"(port));
    return val;
}

static inline void io_wait(void)
{
    outb(0x80, 0); // write to unused port = small delay
}

void pic_init(void)
{
    // Save masks
    uint8_t mask1 = inb(PIC1_DATA);
    uint8_t mask2 = inb(PIC2_DATA);

    // ICW1: start init sequence, expect ICW4
    outb(PIC1_CMD, 0x11);
    io_wait();
    outb(PIC2_CMD, 0x11);
    io_wait();

    // ICW2: set vector offsets
    outb(PIC1_DATA, PIC1_OFFSET);
    io_wait();
    outb(PIC2_DATA, PIC2_OFFSET);
    io_wait();

    // ICW3: tell PICs about each other
    outb(PIC1_DATA, 0x04);
    io_wait(); // PIC1: slave on IRQ2
    outb(PIC2_DATA, 0x02);
    io_wait(); // PIC2: cascade identity

    // ICW4: 8086 mode
    outb(PIC1_DATA, 0x01);
    io_wait();
    outb(PIC2_DATA, 0x01);
    io_wait();

    // Restore masks (mask everything; drivers unmask their own IRQ)
    outb(PIC1_DATA, mask1);
    outb(PIC2_DATA, mask2);
}

void pic_eoi(uint8_t irq)
{
    if (irq >= 8)
        outb(PIC2_CMD, PIC_EOI);
    outb(PIC1_CMD, PIC_EOI);
}

void pic_mask(uint8_t irq)
{
    uint16_t port = (irq < 8) ? PIC1_DATA : PIC2_DATA;
    uint8_t bit = irq % 8;
    outb(port, inb(port) | (1 << bit));
}

void pic_unmask(uint8_t irq)
{
    uint16_t port = (irq < 8) ? PIC1_DATA : PIC2_DATA;
    uint8_t bit = irq % 8;
    outb(port, inb(port) & ~(1 << bit));
}