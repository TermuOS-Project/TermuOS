#include "Kernel.h"

#define PCI_CONFIG_ADDR 0xCF8
#define PCI_CONFIG_DATA 0xCFC

static inline void outl(uint16_t port, uint32_t val)
{
    __asm__ volatile("outl %0,%1" ::"a"(val), "Nd"(port));
}

static inline uint32_t inl(uint16_t port)
{
    uint32_t v;
    __asm__ volatile("inl %1,%0" : "=a"(v) : "Nd"(port));
    return v;
}

uint32_t pci_read(uint8_t bus, uint8_t slot, uint8_t func, uint8_t offset)
{
    uint32_t addr = (1u << 31) | ((uint32_t)bus << 16) | ((uint32_t)slot << 11) | ((uint32_t)func << 8) | (offset & 0xfc);
    outl(PCI_CONFIG_ADDR, addr);
    return inl(PCI_CONFIG_DATA);
}

void pci_write(uint8_t bus, uint8_t slot, uint8_t func, uint8_t offset, uint32_t val)
{
    uint32_t addr = (1u << 31) | ((uint32_t)bus << 16) | ((uint32_t)slot << 11) | ((uint32_t)func << 8) | (offset & 0xfc);
    outl(PCI_CONFIG_ADDR, addr);
    outl(PCI_CONFIG_DATA, val);
}

void pci_enable_busmaster(pci_device_t *dev)
{
    uint32_t cmd = pci_read(dev->bus, dev->slot, dev->func, 0x04);
    cmd |= (1 << 2); // bus master
    cmd |= (1 << 0); // IO space
    pci_write(dev->bus, dev->slot, dev->func, 0x04, cmd);
}

int pci_find(uint16_t vendor, uint16_t device, pci_device_t *out)
{
    for (int bus = 0; bus < 256; bus++)
    {
        for (int slot = 0; slot < 32; slot++)
        {
            uint32_t id = pci_read(bus, slot, 0, 0x00);
            if ((id & 0xffff) == vendor && (id >> 16) == device)
            {
                out->vendor = vendor;
                out->device = device;
                out->bus = bus;
                out->slot = slot;
                out->func = 0;

                // Read BARs
                for (int i = 0; i < 6; i++)
                    out->bar[i] = pci_read(bus, slot, 0, 0x10 + i * 4);

                // Read IRQ
                out->irq = pci_read(bus, slot, 0, 0x3c) & 0xff;

                return 0;
            }
        }
    }
    return -1;
}

void pci_init(void)
{
}