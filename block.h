#ifndef NICOS_BLOCK_H
#define NICOS_BLOCK_H

#include <stdint.h>

#define BLOCK_SECTOR_SIZE 512

int block_init(void);

int block_read_sector(
    uint32_t lba,
    void *buffer
);

int block_write_sector(
    uint32_t lba,
    const void *buffer
);

#endif
