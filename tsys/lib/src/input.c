#include <input.h>
#include <syscall.h>

int kbd_haschar(void)
{
    return (int)__syscall0(SYS_KBD_HASCHAR);
}

int kbd_getchar(void)
{
    return (int)__syscall0(SYS_KBD_GETCHAR);
}

int mouse_get_state(struct mouse_state *out)
{
    return (int)__syscall1(SYS_MOUSE_GET_STATE, (long)out);
}

int mouse_set_bounds(int w, int h)
{
    return (int)__syscall2(SYS_MOUSE_SET_BOUNDS, w, h);
}
