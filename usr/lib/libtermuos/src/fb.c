#include <fb.h>
#include <syscall.h>

int fb_info(struct fb_info *out)
{
    return (int)__syscall1(SYS_FB_INFO, (long)out);
}

int fb_clear(uint32_t colour)
{
    return (int)__syscall1(SYS_FB_CLEAR, (long)colour);
}

int fb_fill_rect(uint32_t x, uint32_t y, uint32_t w, uint32_t h, uint32_t colour)
{
    return (int)__syscall5(SYS_FB_FILL_RECT,
                           (long)x, (long)y, (long)w, (long)h, (long)colour);
}

int fb_putpixel(uint32_t x, uint32_t y, uint32_t colour)
{
    return (int)__syscall3(SYS_FB_PUTPIXEL, (long)x, (long)y, (long)colour);
}

int fb_getpixel(uint32_t x, uint32_t y)
{
    return (int)__syscall2(SYS_FB_GETPIXEL, (long)x, (long)y);
}
