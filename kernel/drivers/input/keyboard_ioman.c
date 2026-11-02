#include "Kernel.h"

static int kbd_dispatch_read(device_obj_t *dev, irp_t *irp)
{
  (void)dev;
  if (!irp->buffer || irp->length < 1)
  {
    irp->status = IRP_STATUS_ERROR;
    return -1;
  }

  char *buf = (char *)irp->buffer;
  uint32_t i = 0;
  while (i < irp->length)
  {
    buf[i++] = keyboard_getchar(); // blocking
    // stop at newline for line-buffered reads
    if (buf[i-1] == '\n') break;
  }

  irp->bytes_transferred = i;
  irp->status = IRP_STATUS_SUCCESS;
  return 0;
}

void keyboard_ioman_register(void)
{
  driver_obj_t *drv = ioman_create_driver("ps2kbd");
  drv->dispatch[IRP_MJ_READ] = kbd_dispatch_read;
  ioman_create_device(drv, "kbd", NULL);
}
