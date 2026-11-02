#include "Kernel.h"

static device_obj_t *ata_device = NULL;

static int ata_dispatch_read(device_obj_t *dev, irp_t *irp)
{
    (void)dev;
    uint32_t lba    = irp->offset;        /* offset = LBA */
    uint8_t  count  = (uint8_t)(irp->length / 512);
    if (count == 0) count = 1;

    int ok = ata_read28(lba, count, irp->buffer);
    irp->status            = ok ? IRP_STATUS_SUCCESS : IRP_STATUS_ERROR;
    irp->bytes_transferred = ok ? (count * 512) : 0;
    return ok ? 0 : -1;
}

static int ata_dispatch_write(device_obj_t *dev, irp_t *irp)
{
    (void)dev;
    uint32_t lba   = irp->offset;
    uint8_t  count = (uint8_t)(irp->length / 512);
    if (count == 0) count = 1;

    int ok = ata_write28(lba, count, irp->buffer);
    irp->status            = ok ? IRP_STATUS_SUCCESS : IRP_STATUS_ERROR;
    irp->bytes_transferred = ok ? (count * 512) : 0;
    return ok ? 0 : -1;
}

void ata_ioman_register(void)
{
    driver_obj_t *drv = ioman_create_driver("atadisk");
    drv->dispatch[IRP_MJ_READ]  = ata_dispatch_read;
    drv->dispatch[IRP_MJ_WRITE] = ata_dispatch_write;
    ata_device = ioman_create_device(drv, "ata0", NULL);
}
