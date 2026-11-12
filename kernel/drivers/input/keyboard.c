#include "Kernel.h"

#define KBD_DATA 0x60
#define KBD_STATUS 0x64

static const char sc_normal[128] = {
    0,
    0,
    '1',
    '2',
    '3',
    '4',
    '5',
    '6',
    '7',
    '8',
    '9',
    '0',
    '-',
    '=',
    '\b',
    '\t',
    'q',
    'w',
    'e',
    'r',
    't',
    'y',
    'u',
    'i',
    'o',
    'p',
    '[',
    ']',
    '\n',
    0,
    'a',
    's',
    'd',
    'f',
    'g',
    'h',
    'j',
    'k',
    'l',
    ';',
    '\'',
    '`',
    0,
    '\\',
    'z',
    'x',
    'c',
    'v',
    'b',
    'n',
    'm',
    ',',
    '.',
    '/',
    0,
    '*',
    0,
    ' ',
    0,
    0,
    0,
    0,
    0,
    0,
    0,
    0,
    0,
    0,
    0,
    0,
    0,
    '7',
    '8',
    '9',
    '-',
    '4',
    '5',
    '6',
    '+',
    '1',
    '2',
    '3',
    '0',
    '.',
    0,
    0,
    0,
    0,
    0,
    0,
    0,
    0,
    0,
    0,
    0,
    0,
    0,
    0,
    0,
    0,
    0,
    0,
    0,
    0,
    0,
    0,
    0,
    0,
    0,
    0,
    0,
    0,
    0,
    0,
    0,
    0,
    0,
    0,
    0,
    0,
    0,
    0,
    0,
    0,
    0,
    0,
    0,
    0,
};

static const char sc_shifted[128] = {
    0,
    0,
    '!',
    '@',
    '#',
    '$',
    '%',
    '^',
    '&',
    '*',
    '(',
    ')',
    '_',
    '+',
    '\b',
    '\t',
    'Q',
    'W',
    'E',
    'R',
    'T',
    'Y',
    'U',
    'I',
    'O',
    'P',
    '{',
    '}',
    '\n',
    0,
    'A',
    'S',
    'D',
    'F',
    'G',
    'H',
    'J',
    'K',
    'L',
    ':',
    '"',
    '~',
    0,
    '|',
    'Z',
    'X',
    'C',
    'V',
    'B',
    'N',
    'M',
    '<',
    '>',
    '?',
    0,
    '*',
    0,
    ' ',
    0,
    0,
    0,
    0,
    0,
    0,
    0,
    0,
    0,
    0,
    0,
    0,
    0,
    '7',
    '8',
    '9',
    '-',
    '4',
    '5',
    '6',
    '+',
    '1',
    '2',
    '3',
    '0',
    '.',
    0,
    0,
    0,
    0,
    0,
    0,
    0,
    0,
    0,
    0,
    0,
    0,
    0,
    0,
    0,
    0,
    0,
    0,
    0,
    0,
    0,
    0,
    0,
    0,
    0,
    0,
    0,
    0,
    0,
    0,
    0,
    0,
    0,
    0,
    0,
    0,
    0,
    0,
    0,
    0,
    0,
    0,
    0,
    0,
};

static int shift = 0;
static int caps = 0;
static int ctrl = 0;

static inline uint8_t inb(uint16_t port)
{
    uint8_t v;
    __asm__ volatile("inb %1,%0" : "=a"(v) : "Nd"(port));
    return v;
}

static inline void outb(uint16_t port, uint8_t val) __attribute__((unused));
static inline void outb(uint16_t port, uint8_t val)
{
    __asm__ volatile("outb %0,%1" ::"a"(val), "Nd"(port));
}

#define KBD_BUF_SIZE 64
static volatile char kbd_buf[KBD_BUF_SIZE];
static volatile int  kbd_head = 0;
static volatile int  kbd_tail = 0;

static void kbd_push(char c)
{
    int next = (kbd_head + 1) % KBD_BUF_SIZE;
    if (next == kbd_tail)
        return;
    kbd_buf[kbd_head] = c;
    kbd_head = next;
}

static void keyboard_irq(registers_t *r)
{
    (void)r;

    uint8_t status = inb(KBD_STATUS);
    if (!(status & 0x01))
        return;
    if (status & 0x20)
        return;

    uint8_t sc = inb(KBD_DATA);

    if (sc & 0x80)
    {
        uint8_t rel = sc & 0x7f;
        if (rel == 0x2a || rel == 0x36)
            shift = 0;
        if (rel == 0x1d)
            ctrl = 0;
        return;
    }

    if (sc == 0x2a || sc == 0x36) { shift = 1; return; }
    if (sc == 0x1d)               { ctrl = 1; return; }
    if (sc == 0x3a)               { caps = !caps; return; }
    if (sc == 0x38)               return; /* Alt */
    if (sc >= 128)                return;

    /* Esc */
    if (sc == 0x01) {
        kbd_push(27);
        return;
    }

    int upper = shift ^ caps;
    char c = upper ? sc_shifted[sc] : sc_normal[sc];
    if (!c)
        return;

    /* Ctrl+A .. Ctrl+Z -> 1..26 */
    if (ctrl) {
        if (c >= 'a' && c <= 'z')
            c = (char)(c - 'a' + 1);
        else if (c >= 'A' && c <= 'Z')
            c = (char)(c - 'A' + 1);
        /* Ctrl+C -> 3 */
    }

    kbd_push(c);
}

void keyboard_init(void)
{
    /* flush */
    while (inb(KBD_STATUS) & 0x01)
        inb(KBD_DATA);

    /* enable keyboard interface */
    outb(KBD_STATUS, 0xAE);
    for (volatile int i = 0; i < 10000; i++)
        ;

    /* enable IRQ1 in controller config (keep mouse bits alone if already set) */
    outb(KBD_STATUS, 0x20);
    while (!(inb(KBD_STATUS) & 0x01))
        ;
    uint8_t cfg = inb(KBD_DATA);
    cfg |= 0x01;          /* keyboard IRQ */
    cfg &= ~0x10;         /* keyboard clock on */
    outb(KBD_STATUS, 0x60);
    while (inb(KBD_STATUS) & 0x02)
        ;
    outb(KBD_DATA, cfg);

    while (inb(KBD_STATUS) & 0x01)
        inb(KBD_DATA);

    kbd_head = kbd_tail = 0;
    idt_register_irq(1, keyboard_irq);  /* IRQ1 = keyboard */
}

int keyboard_haschar(void)
{
    return kbd_head != kbd_tail;
}

char keyboard_getchar(void)
{
    while (kbd_head == kbd_tail)
    {
        uint8_t status = inb(KBD_STATUS);
        if ((status & 0x01) && !(status & 0x20))
        {
            __asm__ volatile("sti");
        }
        __asm__ volatile("hlt"); // wait for interrupt
    }

    char c = kbd_buf[kbd_tail];
    kbd_tail = (kbd_tail + 1) % KBD_BUF_SIZE;
    return c;
}
