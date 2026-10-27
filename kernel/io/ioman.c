#include "ioman.h"
#include "../ob/object.h"
#include "../mm/heap.h"
#include "../lib/printf.h"
#include <stdint.h>
#include <stddef.h>

/* Static pools */
#define MAX_DRIVERS 16
#define MAX_DEVICES 32

static driver_obj_t driver_pool[MAX_DRIVERS];
static uint8_t driver_used[MAX_DRIVERS];
static device_obj_t device_pool[MAX_DEVICES];
static uint8_t device_used[MAX_DEVICES];

static void strcpy_s(char *dst, const char *src, int max)
{
    int i = 0;
    while (src[i] && i < max - 1)
    {
        dst[i] = src[i];
        i++;
    }
    dst[i] = '\0';
}

static int strcmp_s(const char *a, const char *b) __attribute__((unused));
static int strcmp_s(const char *a, const char *b)
{
    while (*a && *b && *a == *b)
    {
        a++;
        b++;
    }
    return *a - *b;
}

/* Initialization */
void ioman_init(void)
{
    for (int i = 0; i < MAX_DRIVERS; i++)
        driver_used[i] = 0;
    for (int i = 0; i < MAX_DEVICES; i++)
        device_used[i] = 0;

    kprintf("ioman: I/O Manager ready\n");
}

/* Driver helpers */
driver_obj_t *ioman_create_driver(const char *name)
{
    int slot = -1;
    for (int i = 0; i < MAX_DRIVERS; i++)
        if (!driver_used[i])
        {
            slot = i;
            break;
        }
    if (slot < 0)
    {
        kprintf("ioman: driver pool full\n");
        return NULL;
    }

    driver_obj_t *d = &driver_pool[slot];
    driver_used[slot] = 1;
    strcpy_s(d->name, name, 32);
    for (int i = 0; i < IRP_MJ_MAX; i++)
        d->dispatch[i] = NULL;

    /* Register the driver in the namespace under \Driver\<name>. */
    char path[48];
    const char *pre = "\\Driver\\";
    int pi = 0;
    while (pre[pi])
    {
        path[pi] = pre[pi];
        pi++;
    }
    int ni = 0;
    while (name[ni] && pi < 47)
    {
        path[pi++] = name[ni++];
    }
    path[pi] = '\0';

    d->ob_header = ob_create(&ObTypeDriver, name, d);
    ob_insert(path, d->ob_header);

    kprintf("ioman: driver '%s' registered\n", name);
    return d;
}

/* Device helpers */
device_obj_t *ioman_create_device(driver_obj_t *driver, const char *name, void *extension)
{
    int slot = -1;
    for (int i = 0; i < MAX_DEVICES; i++)
        if (!device_used[i])
        {
            slot = i;
            break;
        }
    if (slot < 0)
    {
        kprintf("ioman: device pool full\n");
        return NULL;
    }

    device_obj_t *dev = &device_pool[slot];
    device_used[slot] = 1;
    strcpy_s(dev->name, name, 32);
    dev->driver = driver;
    dev->extension = extension;

    /* Register the device in the namespace under \Device\<name>. */
    char path[48];
    const char *pre = "\\Device\\";
    int pi = 0;
    while (pre[pi])
    {
        path[pi] = pre[pi];
        pi++;
    }
    int ni = 0;
    while (name[ni] && pi < 47)
    {
        path[pi++] = name[ni++];
    }
    path[pi] = '\0';

    dev->ob_header = ob_create(&ObTypeDevice, name, dev);
    ob_insert(path, dev->ob_header);

    kprintf("ioman: device '\\Device\\%s' created\n", name);
    return dev;
}

/* IRP dispatch */
int ioman_send_irp(device_obj_t *dev, irp_t *irp)
{
    if (!dev || !dev->driver)
        return -1;

    irp->device = dev;
    irp->status = IRP_STATUS_PENDING;
    irp->bytes_transferred = 0;

    irp_handler_t handler = dev->driver->dispatch[irp->major];
    if (!handler)
    {
        kprintf("ioman: no handler for major %d on '%s'\n", irp->major, dev->name);
        irp->status = IRP_STATUS_ERROR;
        return -1;
    }

    int ret = handler(dev, irp);

    /* Call the completion callback if one was provided. */
    if (irp->completion)
        irp->completion(irp, irp->completion_ctx);

    return ret;
}

/* Device lookup */
device_obj_t *ioman_get_device(const char *name)
{
    char path[48];
    const char *pre = "\\Device\\";
    int pi = 0;
    while (pre[pi])
    {
        path[pi] = pre[pi];
        pi++;
    }
    int ni = 0;
    while (name[ni] && pi < 47)
    {
        path[pi++] = name[ni++];
    }
    path[pi] = '\0';

    object_header_t *obj = ob_lookup(path);
    if (!obj)
        return NULL;
    return (device_obj_t *)obj->body;
}

/* IRP helpers */
irp_t irp_read(void *buf, uint32_t len, uint32_t offset, irp_completion_t cb, void *ctx)
{
    irp_t irp;
    irp.major = IRP_MJ_READ;
    irp.buffer = buf;
    irp.length = len;
    irp.offset = offset;
    irp.ioctl_code = 0;
    irp.bytes_transferred = 0;
    irp.status = IRP_STATUS_PENDING;
    irp.device = NULL;
    irp.completion = cb;
    irp.completion_ctx = ctx;
    return irp;
}

irp_t irp_write(const void *buf, uint32_t len, uint32_t offset, irp_completion_t cb, void *ctx)
{
    irp_t irp;
    irp.major = IRP_MJ_WRITE;
    irp.buffer = (void *)buf;
    irp.length = len;
    irp.offset = offset;
    irp.ioctl_code = 0;
    irp.bytes_transferred = 0;
    irp.status = IRP_STATUS_PENDING;
    irp.device = NULL;
    irp.completion = cb;
    irp.completion_ctx = ctx;
    return irp;
}
