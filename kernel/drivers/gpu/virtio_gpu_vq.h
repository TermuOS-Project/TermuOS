#pragma once
#include <stdint.h>

int virtio_gpu_vq_init_and_get_display(volatile uint8_t *common,
                                       volatile uint8_t *notify,
                                       uint32_t notify_mult);
int virtio_gpu_submit(void *out, uint32_t out_len, void *in, uint32_t in_len);
