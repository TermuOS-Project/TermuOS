#include "Kernel.h"

static inline uint8_t inb(uint16_t port)
{
    uint8_t v;
    __asm__ volatile("inb %1,%0" : "=a"(v) : "Nd"(port));
    return v;
}

typedef struct
{
    uint16_t offset_low;
    uint16_t selector;
    uint8_t ist;
    uint8_t attributes;
    uint16_t offset_mid;
    uint32_t offset_high;
    uint32_t reserved;
} __attribute__((packed)) idt_entry_t;

typedef struct
{
    uint16_t limit;
    uint64_t base;
} __attribute__((packed)) idt_ptr_t;

#define IDT_SIZE 256

static idt_entry_t idt[IDT_SIZE];
static idt_ptr_t idt_ptr;
static irq_handler_t irq_handlers[16] = {0};

extern void *isr_stubs[32];
extern void *irq_stubs[16];

static uint16_t _cs;

static void idt_set_gate(int vec, void *handler, uint8_t attrs)
{
    uint64_t addr = (uint64_t)handler;
    idt[vec].offset_low = addr & 0xffff;
    idt[vec].offset_mid = (addr >> 16) & 0xffff;
    idt[vec].offset_high = (addr >> 32) & 0xffffffff;
    idt[vec].selector = _cs;
    idt[vec].ist = 0;
    idt[vec].attributes = attrs;
    idt[vec].reserved = 0;
}

static const char *exception_names[32] = {
    "Division By Zero",
    "Debug",
    "Non-Maskable Interrupt",
    "Breakpoint",
    "Overflow",
    "Bound Range Exceeded",
    "Invalid Opcode",
    "Device Not Available",
    "Double Fault",
    "Coprocessor Segment Overrun",
    "Invalid TSS",
    "Segment Not Present",
    "Stack-Segment Fault",
    "General Protection Fault",
    "Page Fault",
    "Reserved",
    "x87 Floating-Point",
    "Alignment Check",
    "Machine Check",
    "SIMD Floating-Point",
    "Virtualization",
    "Control Protection",
    "Reserved",
    "Reserved",
    "Reserved",
    "Reserved",
    "Reserved",
    "Reserved",
    "Hypervisor Injection",
    "VMM Communication",
    "Security Exception",
    "Reserved",
};

// Called from isr.asm for both exceptions and IRQs
void exception_handler(registers_t *r)
{
    uint64_t cr2 = 0;
    if (r->vector == 14)
    {
        __asm__ volatile("movq %%cr2, %0" : "=r"(cr2));
        kprintf("   CR2 (fault addr): 0x%x\n", cr2);
    }
    if (r->vector >= 32 && r->vector < 48)
    {
        uint8_t irq = r->vector - 32;
        pic_eoi(irq);
        if (irq_handlers[irq])
            irq_handlers[irq](r);
        return;
    }

    terminal_set_bg(0x99, 0x00, 0x99);
    terminal_set_fg(0xff, 0xff, 0xff);
    terminal_set_size_from_current();

    kprintf("\n  *** KERNEL PANIC ***\n\n");
    kprintf("  Exception #%u: %s\n",
            r->vector,
            r->vector < 32 ? exception_names[r->vector] : "Unknown");
    kprintf("  Error code: 0x%x\n\n", r->error);
    kprintf("  RIP: 0x%x   CS:  0x%x\n", r->rip, r->cs);
    kprintf("  RSP: 0x%x   SS:  0x%x\n", r->rsp, r->ss);
    kprintf("  RFLAGS: 0x%x\n\n", r->rflags);
    kprintf("  RAX: 0x%x  RBX: 0x%x\n", r->rax, r->rbx);
    kprintf("  RCX: 0x%x  RDX: 0x%x\n", r->rcx, r->rdx);
    kprintf("  RSI: 0x%x  RDI: 0x%x\n", r->rsi, r->rdi);
    kprintf("  RBP: 0x%x\n", r->rbp);
    kprintf("  R8:  0x%x  R9:  0x%x\n", r->r8, r->r9);
    kprintf("  R10: 0x%x  R11: 0x%x\n", r->r10, r->r11);
    kprintf("  R12: 0x%x  R13: 0x%x\n", r->r12, r->r13);
    kprintf("  R14: 0x%x  R15: 0x%x\n", r->r14, r->r15);
    kprintf("  CR2: 0x%x\n", cr2);
    kprintf("\n  Press any key to view details, system halted.\n");

    // Wait for keypress so the panic is readable
    while (!(inb(0x64) & 0x01))
    {
    }
    inb(0x60);

    for (;;)
        __asm__ volatile("cli; hlt");
}

void idt_register_irq(uint8_t irq, irq_handler_t handler)
{
    irq_handlers[irq] = handler;

    // if IRQ comes from slave PIC (8-15),
    // unmask cascade IRQ2 on master PIC too
    // (shit took 3 days to figure out 2 lines)
    if (irq >= 8)
        pic_unmask(2);

    pic_unmask(irq);
}

void idt_init(uint16_t cs)
{
    _cs = cs;

    // CPU exceptions (0-31)
    for (int i = 0; i < 32; i++)
        idt_set_gate(i, isr_stubs[i], 0x8e);

    // Hardware IRQs (32-47)
    for (int i = 0; i < 16; i++)
        idt_set_gate(32 + i, irq_stubs[i], 0x8e);

    idt_ptr.limit = sizeof(idt) - 1;
    idt_ptr.base = (uint64_t)&idt;

    __asm__ volatile("lidt %0" : : "m"(idt_ptr));

    // Remap and init PIC, mask all IRQs by default
    pic_init();
    for (int i = 0; i < 16; i++)
        pic_mask(i);

    __asm__ volatile("sti"); // enable interrupts
}
