#ifndef NICOS_PAGING_H
#define NICOS_PAGING_H

#include <stdint.h>

#define USER_BASE 0x0000010000000000ULL
#define USER_SIZE 0x200000ULL

void paging_init(void);

int paging_map_user_page(
    uint64_t virtual_address,
    uint64_t physical_address
);

uint64_t paging_alloc_user_page(
    uint64_t virtual_address
);

#endif
