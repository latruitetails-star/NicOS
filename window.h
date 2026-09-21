#ifndef NICOS_WINDOW_H
#define NICOS_WINDOW_H

#include <stdint.h>

void window_init(void *boot_info);

int window_create(
    uint32_t x,
    uint32_t y,
    uint32_t width,
    uint32_t height
);

int window_draw_text(
    int window_id,
    uint32_t x,
    uint32_t y,
    const char *text,
    uint32_t scale,
    uint32_t color
);

#endif
