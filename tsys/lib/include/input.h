#pragma once

#include <stdint.h>

struct mouse_state {
    int32_t x;
    int32_t y;
    uint8_t buttons;
    int8_t dx;
    int8_t dy;
};

int kbd_haschar(void);
int kbd_getchar(void);

int mouse_get_state(struct mouse_state *out);
