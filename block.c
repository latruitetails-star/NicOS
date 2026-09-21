#include "block.h"
#include "ahci.h"

extern int ahci_init(void);
extern int ahci_read_sector(
    uint32_t lba,
    void *buffer
);
extern int ahci_write_sector(
    uint32_t lba,
    const void *buffer
);

extern int ata_read_sector(
    uint32_t lba,
    void *buffer
);
extern int ata_write_sector(
    uint32_t lba,
    const void *buffer
);

static int block_driver = 0;

int block_get_driver(void)
{
    return block_driver;
}




 

int block_init(void)
{
    if (ahci_init()) {
        block_driver = 1;
        return 1;
    }

    


 
    block_driver = 2;

    return 1;
}

int block_read_sector(
    uint32_t lba,
    void *buffer
)
{
    if (block_driver == 1)
        return ahci_read_sector(lba, buffer);

    if (block_driver == 2)
        return ata_read_sector(lba, buffer);

    return 0;
}

int block_write_sector(
    uint32_t lba,
    const void *buffer
)
{
    if (block_driver == 1)
        return ahci_write_sector(lba, buffer);

    if (block_driver == 2)
        return ata_write_sector(lba, buffer);

    return 0;
}
