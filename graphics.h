#ifndef NICOS_GRAPHICS_H
#define NICOS_GRAPHICS_H

#include <stdint.h>

typedef struct {
    uint64_t framebuffer;
    uint32_t width;
    uint32_t height;
    uint32_t pixels_per_scanline;
    uint32_t pixel_format;

    uint64_t rsdp;

    uint64_t memory_map;
    uint64_t memory_map_size;
    uint64_t memory_descriptor_size;
    uint32_t memory_descriptor_version;
} BootInfo;

int gfx_init(BootInfo *boot);

void gfx_clear(uint32_t color);

void gfx_put_pixel(
    uint32_t x,
    uint32_t y,
    uint32_t color
);

void gfx_hline(
    uint32_t x,
    uint32_t y,
    uint32_t width,
    uint32_t color
);

void gfx_rect(
    uint32_t x,
    uint32_t y,
    uint32_t width,
    uint32_t height,
    uint32_t color
);

void gfx_present(BootInfo *boot);

#endif
