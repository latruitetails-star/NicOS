#ifndef NICOS_AHCI_H
#define NICOS_AHCI_H

#include <stdint.h>

int ahci_init(void);

int ahci_read_sector(
    uint32_t lba,
    void *buffer
);

int ahci_write_sector(
    uint32_t lba,
    const void *buffer
);

#endif
