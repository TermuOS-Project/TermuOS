#include "Kernel.h"

#define KBD_DATA    0x60
#define KBD_STATUS  0x64

static inline uint8_t inb(uint16_t port)
{
    uint8_t v;
    __asm__ volatile("inb %1,%0" : "=a"(v) : "Nd"(port));
    return v;
}

static inline void outb(uint16_t port, uint8_t val)
{
    __asm__ volatile("outb %0,%1" ::"a"(val), "Nd"(port));
}

static void wait_write(void)
{
    for (int i = 0; i < 100000; i++)
        if (!(inb(KBD_STATUS) & 0x02))
            return;
}

static void wait_read(void)
{
    for (int i = 0; i < 100000; i++)
        if (inb(KBD_STATUS) & 0x01)
            return;
}

static void mouse_write(uint8_t val)
{
    wait_write();
    outb(KBD_STATUS, 0xD4);
    wait_write();
    outb(KBD_DATA, val);
}

static uint8_t mouse_read(void)
{
    wait_read();
    return inb(KBD_DATA);
}

// state

static volatile int32_t mx = 100;
static volatile int32_t my = 100;
static volatile uint8_t mbuttons = 0;
static volatile int8_t  last_dx = 0;
static volatile int8_t  last_dy = 0;
static volatile int     mouse_dirty = 0;

static int screen_w = 800;
static int screen_h = 600;

static uint8_t cycle = 0;
static uint8_t packet[3];

static void mouse_irq(registers_t *r)
{
    (void)r;

    while (inb(KBD_STATUS) & 0x01)
    {
        uint8_t status = inb(KBD_STATUS);
        if (!(status & 0x20))
            break; /* keyboard byte – leave it for keyboard IRQ */

        uint8_t data = inb(KBD_DATA);

        if (cycle == 0 && !(data & 0x08))
            continue;

        packet[cycle++] = data;
        if (cycle < 3)
            continue;
        cycle = 0;

        if (packet[0] & 0xC0)
            continue;

        int8_t dx = (int8_t)packet[1];
        int8_t dy = (int8_t)packet[2];

        last_dx = dx;
        last_dy = dy;
        mx += dx;
        my -= dy;

        if (mx < 0) mx = 0;
        if (my < 0) my = 0;
        if (mx >= screen_w) mx = screen_w - 1;
        if (my >= screen_h) my = screen_h - 1;

        mbuttons = packet[0] & 0x07;
        mouse_dirty = 1;
    }
}

void mouse_set_bounds(int width, int height)
{
    if (width > 0) screen_w = width;
    if (height > 0) screen_h = height;
    if (mx >= screen_w) mx = screen_w - 1;
    if (my >= screen_h) my = screen_h - 1;
}

void mouse_get_state(mouse_state_t *out)
{
    if (!out)
        return;
    out->x = mx;
    out->y = my;
    out->buttons = mbuttons;
    out->dx = last_dx;
    out->dy = last_dy;
}

int mouse_is_dirty(void)
{
    if (!mouse_dirty)
        return 0;
    mouse_dirty = 0;
    return 1;
}

void mouse_init(void)
{
    wait_write();
    outb(KBD_STATUS, 0xA8);
    for (volatile int i = 0; i < 10000; i++)
        ;

    wait_write();
    outb(KBD_STATUS, 0x20);
    wait_read();
    uint8_t cfg = inb(KBD_DATA);
    cfg |= 0x02;
    cfg |= 0x01;
    cfg &= ~0x20;
    wait_write();
    outb(KBD_STATUS, 0x60);
    wait_write();
    outb(KBD_DATA, cfg);

    mouse_write(0xF6);
    mouse_read();
    mouse_write(0xF4);
    mouse_read();
    
    while (inb(KBD_STATUS) & 0x01)
        inb(KBD_DATA);

    cycle = 0;
    idt_register_irq(12, mouse_irq);

    kprintf("mouse: PS/2 mouse ready (IRQ12)\n");
}
