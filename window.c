#include <stdint.h>

#include "graphics.h"
#include "window.h"

#define MAX_WINDOWS 16

typedef struct {
    uint8_t used;

    uint32_t x;
    uint32_t y;
    uint32_t width;
    uint32_t height;
} Window;

static BootInfo *window_boot;
static Window windows[MAX_WINDOWS];

void window_init(void *boot_info)
{
    window_boot = (BootInfo *)boot_info;

    for (uint32_t i = 0; i < MAX_WINDOWS; i++)
        windows[i].used = 0;
}

int window_create(
    uint32_t x,
    uint32_t y,
    uint32_t width,
    uint32_t height
)
{
    if (!window_boot)
        return -1;

    if (width < 80 || height < 50)
        return -1;

    if (x >= window_boot->width || y >= window_boot->height)
        return -1;

    if (x + width > window_boot->width)
        width = window_boot->width - x;

    if (y + height > window_boot->height)
        height = window_boot->height - y;

    for (uint32_t i = 0; i < MAX_WINDOWS; i++) {
        if (windows[i].used)
            continue;

        windows[i].used = 1;

        windows[i].x = x;
        windows[i].y = y;
        windows[i].width = width;
        windows[i].height = height;

        

 
        gfx_rect(
            x,
            y,
            width,
            height,
            0x0028323E
        );

        

 
        gfx_rect(
            x,
            y,
            width,
            36,
            0x00334A5C
        );

        

 
        if (width >= 90) {
            gfx_rect(
                x + width - 78,
                y + 10,
                12,
                12,
                0x00607080
            );

            gfx_rect(
                x + width - 56,
                y + 10,
                12,
                12,
                0x00607080
            );

            gfx_rect(
                x + width - 34,
                y + 10,
                12,
                12,
                0x00A05050
            );
        }

        return (int)i;
    }

    return -1;
}

int window_draw_text(
    int window_id,
    uint32_t x,
    uint32_t y,
    const char *text,
    uint32_t scale,
    uint32_t color
)
{
    if (!window_boot || !text)
        return -1;

    if (window_id < 0 || window_id >= MAX_WINDOWS)
        return -1;

    Window *window = &windows[window_id];

    if (!window->used)
        return -1;

    if (scale == 0 || scale > 16)
        return -1;

    uint32_t px = window->x + x;
    uint32_t py = window->y + y;

    

 
    if (px >= window->x + window->width)
        return -1;

    if (py >= window->y + window->height)
        return -1;

    for (const char *p = text; *p; p++) {

        if (*p == '\n') {
            px = window->x + x;
            py += 9 * scale;
            continue;
        }

        if (px + 5 * scale >= window->x + window->width)
            break;

        if (py + 7 * scale >= window->y + window->height)
            break;

        draw_glyph(
            window_boot,
            px,
            py,
            *p,
            scale,
            color
        );

        px += 6 * scale;
    }

    return 0;
}
