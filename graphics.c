#include <stdint.h>
#include "graphics.h"

#define PAGE_SIZE 4096ULL

extern uint64_t pmm_alloc_contiguous(uint64_t page_count);

static uint32_t *gfx_backbuffer = 0;
static uint64_t gfx_backbuffer_size = 0;

static uint32_t gfx_width = 0;
static uint32_t gfx_height = 0;
static uint32_t gfx_pitch = 0;

static int gfx_ready = 0;

int gfx_init(BootInfo *boot)
{
    if (!boot)
        return 0;

    if (!boot->framebuffer ||
        boot->width == 0 ||
        boot->height == 0)
        return 0;

    uint64_t pixels =
        (uint64_t)boot->width * boot->height;

    uint64_t size = pixels * sizeof(uint32_t);

    uint64_t pages =
        (size + PAGE_SIZE - 1) / PAGE_SIZE;

    uint64_t physical =
        pmm_alloc_contiguous(pages);

    if (!physical)
        return 0;

    gfx_backbuffer = (uint32_t *)(uintptr_t)physical;
    gfx_backbuffer_size = pages * PAGE_SIZE;

    gfx_width = boot->width;
    gfx_height = boot->height;
    gfx_pitch = boot->pixels_per_scanline;

    gfx_ready = 1;

    



 
    volatile uint32_t *fb =
        (volatile uint32_t *)boot->framebuffer;

    for (uint32_t y = 0; y < gfx_height; y++) {
        for (uint32_t x = 0; x < gfx_width; x++) {
            gfx_backbuffer[
                (uint64_t)y * gfx_width + x
            ] =
                fb[
                    (uint64_t)y * gfx_pitch + x
                ];
        }
    }

    return 1;
}

void gfx_clear(uint32_t color)
{
    if (!gfx_ready)
        return;

    uint64_t count =
        (uint64_t)gfx_width * gfx_height;

    for (uint64_t i = 0; i < count; i++)
        gfx_backbuffer[i] = color;
}

void gfx_put_pixel(
    uint32_t x,
    uint32_t y,
    uint32_t color
)
{
    if (!gfx_ready)
        return;

    if (x >= gfx_width || y >= gfx_height)
        return;

    gfx_backbuffer[
        (uint64_t)y * gfx_width + x
    ] = color;
}

void gfx_hline(
    uint32_t x,
    uint32_t y,
    uint32_t width,
    uint32_t color
)
{
    if (!gfx_ready)
        return;

    if (y >= gfx_height || x >= gfx_width)
        return;

    if (x + width > gfx_width)
        width = gfx_width - x;

    uint32_t *dst =
        gfx_backbuffer +
        (uint64_t)y * gfx_width +
        x;

    for (uint32_t i = 0; i < width; i++)
        dst[i] = color;
}

void gfx_rect(
    uint32_t x,
    uint32_t y,
    uint32_t width,
    uint32_t height,
    uint32_t color
)
{
    if (!gfx_ready)
        return;

    if (x >= gfx_width || y >= gfx_height)
        return;

    if (x + width > gfx_width)
        width = gfx_width - x;

    if (y + height > gfx_height)
        height = gfx_height - y;

    for (uint32_t yy = 0; yy < height; yy++)
        gfx_hline(
            x,
            y + yy,
            width,
            color
        );
}

void gfx_present(BootInfo *boot)
{
    if (!gfx_ready || !boot || !boot->framebuffer)
        return;

    volatile uint32_t *fb =
        (volatile uint32_t *)boot->framebuffer;

    for (uint32_t y = 0; y < gfx_height; y++) {

        uint32_t *src =
            gfx_backbuffer +
            (uint64_t)y * gfx_width;

        volatile uint32_t *dst =
            fb +
            (uint64_t)y * boot->pixels_per_scanline;

        for (uint32_t x = 0; x < gfx_width; x++)
            dst[x] = src[x];
    }
}
