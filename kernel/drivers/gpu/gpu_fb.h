#pragma once
#include <stdint.h>

#ifdef __cplusplus
extern "C" {
#endif

int gpu_fb_init(void);
int gpu_fb_present(void);
void *gpu_fb_ptr(void);
uint32_t gpu_fb_width(void);
uint32_t gpu_fb_height(void);

#ifdef __cplusplus
}
#endif
