#include "fb.h"
#include "../gpu/gpu_fb.h"

static struct limine_framebuffer *_fb = 0;
static int _use_gpu = 0;

void fb_init(struct limine_framebuffer *fb)
{
    _fb = fb;
}

void fb_set_gpu_backend(int on)
{
    _use_gpu = on ? 1 : 0;
}

int fb_gpu_active(void)
{
    return _use_gpu;
}

struct limine_framebuffer *fb_get(void)
{
    return _fb;
}

uint64_t fb_width(void)
{
    if (_use_gpu)
        return gpu_fb_width();
    return _fb ? _fb->width : 0;
}

uint64_t fb_height(void)
{
    if (_use_gpu)
        return gpu_fb_height();
    return _fb ? _fb->height : 0;
}

uint64_t fb_pitch(void)
{
    if (_use_gpu)
        return (uint64_t)gpu_fb_width() * 4;
    return _fb ? _fb->pitch : 0;
}

uint32_t fb_bpp(void)
{
    if (_use_gpu)
        return 32;
    return _fb ? (uint32_t)_fb->bpp : 0;
}

void fb_present(void)
{
    if (_use_gpu)
        (void)gpu_fb_present();
}

static uint32_t *gpu_row(uint64_t y)
{
    uint32_t *base = (uint32_t *)gpu_fb_ptr();
    return base + y * (uint64_t)gpu_fb_width();
}

uint32_t fb_colour(uint8_t r, uint8_t g, uint8_t b)
{
    if (_use_gpu)
        return (0xFFu << 24) | ((uint32_t)r << 16) | ((uint32_t)g << 8) | (uint32_t)b;
    if (!_fb)
        return 0;
    return ((uint32_t)r << _fb->red_mask_shift) |
           ((uint32_t)g << _fb->green_mask_shift) |
           ((uint32_t)b << _fb->blue_mask_shift);
}

void fb_putpixel(uint64_t x, uint64_t y, uint32_t colour)
{
    if (_use_gpu) {
        if (x >= gpu_fb_width() || y >= gpu_fb_height())
            return;
        gpu_row(y)[x] = colour;
        return;
    }
    if (!_fb || x >= _fb->width || y >= _fb->height)
        return;
    uint8_t *base = (uint8_t *)_fb->address;
    uint32_t *row = (uint32_t *)(base + y * _fb->pitch);
    row[x] = colour;
}

void fb_clear(uint32_t colour)
{
    if (_use_gpu) {
        uint32_t w = gpu_fb_width(), h = gpu_fb_height();
        for (uint64_t y = 0; y < h; y++) {
            uint32_t *row = gpu_row(y);
            for (uint64_t x = 0; x < w; x++)
                row[x] = colour;
        }
        return;
    }
    if (!_fb)
        return;
    uint8_t *base = (uint8_t *)_fb->address;
    for (uint64_t y = 0; y < _fb->height; y++)
    {
        uint32_t *row = (uint32_t *)(base + y * _fb->pitch);
        for (uint64_t x = 0; x < _fb->width; x++)
            row[x] = colour;
    }
}

void fb_fill_rect(uint64_t x, uint64_t y, uint64_t w, uint64_t h, uint32_t colour) 
{
    uint64_t fw = fb_width(), fh = fb_height();
    if (x >= fw || y >= fh)
        return;
    if (x + w > fw)
        w = fw - x;
    if (y + h > fh)
        h = fh - y;

    if (_use_gpu) {
        for (uint64_t row = 0; row < h; row++) {
            uint32_t *line = gpu_row(y + row);
            for (uint64_t col = 0; col < w; col++)
                line[x + col] = colour;
        }
        return;
    }
    if (!_fb)
        return;
    uint8_t *base = (uint8_t *)_fb->address;
    for (uint64_t row = 0; row < h; row++) {
        uint32_t *line = (uint32_t *)(base + (y + row) * _fb->pitch);
        for (uint64_t col = 0; col < w; col++)
            line[x + col] = colour;
    }
}

void fb_draw_hline(uint64_t x, uint64_t y, uint64_t len, uint32_t colour)
{
    fb_fill_rect(x, y, len, 1, colour);
}

void fb_draw_vline(uint64_t x, uint64_t y, uint64_t len, uint32_t colour)
{
    fb_fill_rect(x, y, 1, len, colour);
}

void fb_draw_rect(uint64_t x, uint64_t y, uint64_t w, uint64_t h, uint32_t colour)
{
    if (w == 0 || h == 0)
        return;
    fb_draw_hline(x, y, w, colour);
    fb_draw_hline(x, y + h - 1, w, colour);
    fb_draw_vline(x, y, h, colour);
    fb_draw_vline(x + w - 1, y, h, colour);
}

uint32_t fb_getpixel(uint64_t x, uint64_t y)
{
    if (_use_gpu) {
        if (x >= gpu_fb_width() || y >= gpu_fb_width())
            return 0;
        return gpu_row(y)[x];
    }
    if (!_fb || x >= _fb->width || y >= _fb->height)
        return 0;
    uint8_t *base = (uint8_t *)_fb->address;
    uint32_t *row = (uint32_t *)(base + y * _fb->pitch);
    return row[x];
}
