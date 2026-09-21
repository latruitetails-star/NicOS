#include <stdint.h>

#define ATA_DATA        0x1F0
#define ATA_ERROR       0x1F1
#define ATA_SECCOUNT    0x1F2
#define ATA_LBA0        0x1F3
#define ATA_LBA1        0x1F4
#define ATA_LBA2        0x1F5
#define ATA_DRIVE       0x1F6
#define ATA_STATUS      0x1F7
#define ATA_COMMAND     0x1F7

#define ATA_CMD_READ    0x20
#define ATA_CMD_WRITE   0x30

#define ATA_SR_ERR      0x01
#define ATA_SR_DRQ      0x08
#define ATA_SR_BSY      0x80

static inline uint8_t inb(uint16_t port)
{
    uint8_t value;

    __asm__ volatile (
        "inb %1, %0"
        : "=a"(value)
        : "Nd"(port)
    );

    return value;
}

static inline void outb(uint16_t port, uint8_t value)
{
    __asm__ volatile (
        "outb %0, %1"
        :
        : "a"(value), "Nd"(port)
    );
}

static inline uint16_t inw(uint16_t port)
{
    uint16_t value;

    __asm__ volatile (
        "inw %1, %0"
        : "=a"(value)
        : "Nd"(port)
    );

    return value;
}

static inline void outw(uint16_t port, uint16_t value)
{
    __asm__ volatile (
        "outw %0, %1"
        :
        : "a"(value), "Nd"(port)
    );
}

static void ata_delay(void)
{
    inb(ATA_STATUS);
    inb(ATA_STATUS);
    inb(ATA_STATUS);
    inb(ATA_STATUS);
}

static int ata_wait_ready(void)
{
    uint32_t timeout = 1000000;

    while (timeout--) {
        uint8_t status = inb(ATA_STATUS);

        if (status == 0xFF)
            return 0;

        if (status & ATA_SR_ERR)
            return 0;

        if (!(status & ATA_SR_BSY))
            return 1;

        __asm__ volatile ("pause");
    }

    return 0;
}

static int ata_wait_drq(void)
{
    uint32_t timeout = 1000000;

    while (timeout--) {
        uint8_t status = inb(ATA_STATUS);

        if (status == 0xFF)
            return 0;

        if (status & ATA_SR_ERR)
            return 0;

        if (status & ATA_SR_DRQ)
            return 1;

        if (!(status & ATA_SR_BSY))
            return 0;

        __asm__ volatile ("pause");
    }

    return 0;
}

uint8_t ata_debug_status(void)
{
    return inb(ATA_STATUS);
}

int ata_read_sector(uint32_t lba, void *buffer)
{
    if (!buffer)
        return 0;

    uint16_t *dst = (uint16_t *)buffer;

    if (!ata_wait_ready())
        return 0;

    outb(
        ATA_DRIVE,
        0xE0 | ((lba >> 24) & 0x0F)
    );

    outb(ATA_SECCOUNT, 1);
    outb(ATA_LBA0, (uint8_t)lba);
    outb(ATA_LBA1, (uint8_t)(lba >> 8));
    outb(ATA_LBA2, (uint8_t)(lba >> 16));

    outb(ATA_COMMAND, ATA_CMD_READ);

    ata_delay();

    if (!ata_wait_drq())
        return 0;

    for (uint32_t i = 0; i < 256; i++)
        dst[i] = inw(ATA_DATA);

    return 1;
}

int ata_write_sector(uint32_t lba, const void *buffer)
{
    if (!buffer)
        return 0;

    const uint16_t *src = (const uint16_t *)buffer;

    if (!ata_wait_ready())
        return 0;

    outb(
        ATA_DRIVE,
        0xE0 | ((lba >> 24) & 0x0F)
    );

    outb(ATA_SECCOUNT, 1);
    outb(ATA_LBA0, (uint8_t)lba);
    outb(ATA_LBA1, (uint8_t)(lba >> 8));
    outb(ATA_LBA2, (uint8_t)(lba >> 16));

    outb(ATA_COMMAND, ATA_CMD_WRITE);

    ata_delay();

    if (!ata_wait_drq())
        return 0;

    for (uint32_t i = 0; i < 256; i++)
        outw(ATA_DATA, src[i]);

    ata_delay();

    return ata_wait_ready();
}
