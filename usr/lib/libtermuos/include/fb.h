#pragma once

#include <stdint.h>

struct fb_info {
    uint64_t width;
    uint64_t height;
    uint64_t pitch;
    uint32_t bpp;
};

int fb_info(struct fb_info *out);
int fb_clear(uint32_t colour);
int fb_fill_rect(uint32_t x, uint32_t y, uint32_t w, uint32_t h, uint32_t colour);
int fb_putpixel(uint32_t x, uint32_t y, uint32_t colour);
int fb_getpixel(uint32_t x, uint32_t y);
