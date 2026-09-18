#include "driver.h"
#include "../lib/printf.h"

#define MAX_DRIVERS 64

static const driver_t *table[MAX_DRIVERS];
static int count;
static driver_info_t infos[DRIVER_MAX];
static int info_count;

static void copy_name(char *dst, const char *src)
{
    int i = 0;
    if (!src)
    {
        dst[0] = 0;
        return;
    }
    while (src[i] && i < DRIVER_NAME_MAX - 1)
    {
        dst[i] = src[i];
        i++;
    }
    dst[i] = 0;
}

void driver_register(const driver_t *d)
{
    if (!d || !d->name || !d->init || count >= MAX_DRIVERS)
        return;

    table[count] = d;

    if (info_count < DRIVER_MAX)
    {
        copy_name(infos[info_count].name, d->name);
        infos[info_count].priority = d->priority;
        infos[info_count].status = 1;
        info_count++;
    }

    count++;
}

void drivers_init(void)
{
    for (int p = 0; p < 256; p++)
    {
        for (int i = 0; i < count; i++)
        {
            if (table[i]->priority != p)
                continue;

            kprintf("driver: %s...\n", table[i]->name);
            int rc = table[i]->init();

            if (i < info_count)
            {
                infos[i].status = rc;
            }

            if (rc < 0)
                kprintf("driver: %s failed (%d)\n", table[i]->name, rc);
            else
                kprintf("driver: %s ok\n", table[i]->name);
        }
    }
}

static void set_status(const char *name, int rc)
{
    for (int j = 0; j < info_count; j++)
    {
        const char *a = infos[j].name, *b = name;
        while (*a && *a == *b)
        {
            a++;
            b++;
        }
        if (*a == 0 && *b == 0)
        {
            infos[j].status = rc;
            return;
        }
    }
}

int drivers_list(driver_info_t *out, int max)
{
    if (!out || max <= 0)
        return 0;

    int n = info_count < max ? info_count : max;
    for (int i = 0; i < n; i++)
        out[i] = infos[i];
    return n;
}

extern void rtc_driver_register(void);
extern void virtio_net_register();
extern void virtio_gpu_driver_register(void);

void drivers_register_all(void)
{
    rtc_driver_register();
    virtio_gpu_driver_register();
    virtio_net_register();
}
