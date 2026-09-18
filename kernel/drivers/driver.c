#include "driver.h"
#include "../lib/printf.h"

#define MAX_DRIVERS 64

static const driver_t *table[MAX_DRIVERS];
static int count;

void driver_register(const driver_t *d)
{
    if (!d || !d->name || !d->init || count >= MAX_DRIVERS)
        return;
    table[count++] = d;
}

void drivers_init(void)
{
    /* simple insertion order by priority */
    for (int p = 0; p < 256; p++)
    {
        for (int i = 0; i < count; i++)
        {
            if (table[i]->priority != p)
                continue;
            kprintf("driver: %s...\n", table[i]->name);
            int rc = table[i]->init();
            if (rc < 0)
                kprintf("driver: %s failed (%d)\n", table[i]->name, rc);
            else
                kprintf("driver: %s ok\n", table[i]->name);
        }
    }
}

extern void rtc_driver_register(void);
extern void virtio_net_register();

void drivers_register_all(void)
{
    rtc_driver_register();
    virtio_net_register();
}
