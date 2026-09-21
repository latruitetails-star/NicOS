#include <stdint.h>
extern void kernel_debug(const char *s);
#include "ahci.h"
extern void kernel_debug(const char *s);

#define PCI_CONFIG_ADDRESS 0xCF8
#define PCI_CONFIG_DATA    0xCFC

#define AHCI_CLASS          0x01
#define AHCI_SUBCLASS       0x06
#define AHCI_PROGIF         0x01

#define HBA_CAP             0x00
#define HBA_GHC             0x04
#define HBA_IS              0x08
#define HBA_PI              0x0C

#define HBA_GHC_AE          (1U << 31)

#define PORT_CLB            0x00
#define PORT_FB             0x08
#define PORT_IS             0x10
#define PORT_IE             0x14
#define PORT_CMD            0x18
#define PORT_TFD            0x20
#define PORT_SIG            0x24
#define PORT_SSTS           0x28
#define PORT_SCTL           0x2C
#define PORT_SERR           0x30
#define PORT_CI             0x38

#define PORT_CMD_ST         (1U << 0)
#define PORT_CMD_FRE        (1U << 4)
#define PORT_CMD_FR         (1U << 14)
#define PORT_CMD_CR         (1U << 15)

#define PORT_TFD_BSY        (1U << 7)
#define PORT_TFD_DRQ        (1U << 3)

#define PORT_IS_TFES        (1U << 30)

#define SATA_SIG_ATA        0x00000101

#define AHCI_TIMEOUT        1000000

#define AHCI_SLOTS         32
#define AHCI_SECTOR_SIZE   512

struct HBACommandHeader {
    uint16_t flags;
    uint16_t prdt_length;
    uint32_t prdbc;
    uint32_t ctba;
    uint32_t ctbau;
    uint32_t reserved[4];
} __attribute__((packed));

struct HBAPRDTEntry {
    uint32_t dba;
    uint32_t dbau;
    uint32_t reserved;
    uint32_t dbc;
} __attribute__((packed));

struct HBACommandTable {
    uint8_t cfis[64];
    uint8_t acmd[16];
    uint8_t reserved[48];
    struct HBAPRDTEntry prdt[1];
} __attribute__((packed));

static uintptr_t ahci_base = 0;
static uintptr_t ahci_port = 0;
static int ahci_ready = 0;
static int ahci_slot = 0;

static struct HBACommandHeader command_list[AHCI_SLOTS]
    __attribute__((aligned(1024)));

static uint8_t fis_area[256]
    __attribute__((aligned(256)));

static struct HBACommandTable command_table
    __attribute__((aligned(128)));

static uint32_t pci_read32(
    uint8_t bus,
    uint8_t device,
    uint8_t function,
    uint8_t offset
)
{
    uint32_t address =
        0x80000000U |
        ((uint32_t)bus << 16) |
        ((uint32_t)device << 11) |
        ((uint32_t)function << 8) |
        (offset & 0xFC);

    __asm__ volatile (
        "outl %0, %1"
        :
        : "a"(address),
          "Nd"((uint16_t)PCI_CONFIG_ADDRESS)
    );

    uint32_t value;

    __asm__ volatile (
        "inl %1, %0"
        : "=a"(value)
        : "Nd"((uint16_t)PCI_CONFIG_DATA)
    );

    return value;
}

static void pci_write32(
    uint8_t bus,
    uint8_t device,
    uint8_t function,
    uint8_t offset,
    uint32_t value
)
{
    uint32_t address =
        0x80000000U |
        ((uint32_t)bus << 16) |
        ((uint32_t)device << 11) |
        ((uint32_t)function << 8) |
        (offset & 0xFC);

    __asm__ volatile (
        "outl %0, %1"
        :
        : "a"(address),
          "Nd"((uint16_t)PCI_CONFIG_ADDRESS)
    );

    __asm__ volatile (
        "outl %0, %1"
        :
        : "a"(value),
          "Nd"((uint16_t)PCI_CONFIG_DATA)
    );
}

static int ahci_find_controller(
    uint8_t *out_bus,
    uint8_t *out_device,
    uint8_t *out_function,
    uint32_t *out_bar5
)
{
    for (uint16_t bus = 0; bus < 256; bus++) {

        for (uint8_t device = 0; device < 32; device++) {

            for (uint8_t function = 0;
                 function < 8;
                 function++) {

                uint32_t id =
                    pci_read32(
                        (uint8_t)bus,
                        device,
                        function,
                        0x00
                    );

                if ((id & 0xFFFFU) == 0xFFFFU)
                    continue;

                uint32_t class_reg =
                    pci_read32(
                        (uint8_t)bus,
                        device,
                        function,
                        0x08
                    );

                uint8_t class_code =
                    (uint8_t)(class_reg >> 24);

                uint8_t subclass =
                    (uint8_t)(class_reg >> 16);

                uint8_t prog_if =
                    (uint8_t)(class_reg >> 8);

                if (class_code != AHCI_CLASS ||
                    subclass != AHCI_SUBCLASS ||
                    prog_if != AHCI_PROGIF)
                    continue;

                uint32_t bar5 =
                    pci_read32(
                        (uint8_t)bus,
                        device,
                        function,
                        0x24
                    );

                *out_bus = (uint8_t)bus;
                *out_device = device;
                *out_function = function;
                *out_bar5 = bar5 & 0xFFFFFFF0U;

                return 1;
            }
        }
    }

    return 0;
}

static void ahci_zero(
    void *ptr,
    uint32_t size
)
{
    uint8_t *p = (uint8_t *)ptr;

    for (uint32_t i = 0; i < size; i++)
        p[i] = 0;
}

static int ahci_wait_clear(
    volatile uint32_t *reg,
    uint32_t mask
)
{
    uint32_t timeout = AHCI_TIMEOUT;

    while (timeout--) {

        if (!(*reg & mask))
            return 1;

        __asm__ volatile ("pause");
    }

    return 0;
}

static int ahci_wait_set(
    volatile uint32_t *reg,
    uint32_t mask
)
{
    uint32_t timeout = AHCI_TIMEOUT;

    while (timeout--) {
        if (*reg & mask)
            return 1;

        __asm__ volatile ("pause");
    }

    return 0;
}

static int ahci_port_start(void)
{
    volatile uint32_t *cmd =
        (volatile uint32_t *)(ahci_port + PORT_CMD);

    *cmd &= ~PORT_CMD_ST;

    if (!ahci_wait_clear(cmd, PORT_CMD_CR))
        return 0;

    *cmd |= PORT_CMD_FRE;

    if (!ahci_wait_set(cmd, PORT_CMD_FR))
        return 0;

    *cmd |= PORT_CMD_ST;

    if (!ahci_wait_set(cmd, PORT_CMD_CR))
        return 0;

    return 1;
}

static void ahci_port_stop(void)
{
    volatile uint32_t *cmd =
        (volatile uint32_t *)(ahci_port + PORT_CMD);

    *cmd &= ~PORT_CMD_ST;

    ahci_wait_clear(cmd, PORT_CMD_CR);

    *cmd &= ~PORT_CMD_FRE;

    ahci_wait_clear(cmd, PORT_CMD_FR);
}

static int ahci_find_port(
    uint32_t pi
)
{
    for (uint32_t i = 0; i < 32; i++) {

        if (!(pi & (1U << i)))
            continue;

        volatile uint32_t *ssts =
            (volatile uint32_t *)
            (ahci_base + 0x100 + i * 0x80 + PORT_SSTS);

        uint32_t value = *ssts;

        kernel_debug("[AHCI] PORT=");
        {
            char d[3];
            d[0] = '0' + (i / 10);
            d[1] = '0' + (i % 10);
            d[2] = 0;
            kernel_debug(d);
        }

        kernel_debug(" SSTS=");
        {
            char h[] = "0123456789ABCDEF";
            char out[9];
            for (int j = 0; j < 8; j++)
                out[j] = h[(value >> (28 - j * 4)) & 0xF];
            out[8] = 0;
            kernel_debug(out);
        }
        kernel_debug("\r\n");

        uint8_t det =
            (uint8_t)(value & 0x0F);

        uint8_t ipm =
            (uint8_t)((value >> 8) & 0x0F);

        if (det == 3 && ipm == 1) {

            volatile uint32_t *sig =
                (volatile uint32_t *)
                (ahci_base + 0x100 + i * 0x80 + PORT_SIG);

            if (*sig == SATA_SIG_ATA) {

                ahci_port =
                    ahci_base + 0x100 + i * 0x80;

                kernel_debug("[AHCI] SELECT PORT\\r\\n");

                return 1;
            }
        }
    }

    return 0;
}

int ahci_init(void)
{
    uint8_t bus;
    uint8_t device;
    uint8_t function;
    uint32_t bar5;

    if (!ahci_find_controller(
            &bus,
            &device,
            &function,
            &bar5))
        return 0;

    if (bar5 == 0)
        return 0;

    ahci_base = (uintptr_t)bar5;

    

 
    uint32_t command =
        pci_read32(
            bus,
            device,
            function,
            0x04
        );

    command |= (1U << 1);
    command |= (1U << 2);

    pci_write32(
        bus,
        device,
        function,
        0x04,
        command
    );

    volatile uint32_t *ghc =
        (volatile uint32_t *)
        (ahci_base + HBA_GHC);

    *ghc |= HBA_GHC_AE;

    volatile uint32_t *pi =
        (volatile uint32_t *)
        (ahci_base + HBA_PI);

    if (!ahci_find_port(*pi))
        return 0;

    ahci_port_stop();

    ahci_zero(
        command_list,
        sizeof(command_list)
    );

    ahci_zero(
        fis_area,
        sizeof(fis_area)
    );

    ahci_zero(
        &command_table,
        sizeof(command_table)
    );

    volatile uint32_t *clb =
        (volatile uint32_t *)
        (ahci_port + PORT_CLB);

    volatile uint32_t *fb =
        (volatile uint32_t *)
        (ahci_port + PORT_FB);

    *clb = (uint32_t)(uintptr_t)command_list;
    *(clb + 1) =
        (uint32_t)((uintptr_t)command_list >> 32);

    *fb = (uint32_t)(uintptr_t)fis_area;
    *(fb + 1) =
        (uint32_t)((uintptr_t)fis_area >> 32);

    kernel_debug("[AHCI] CLB=");
    {
        char h[] = "0123456789ABCDEF";
        char o[9];
        uint32_t v = (uint32_t)(uintptr_t)command_list;
        for (int i = 0; i < 8; i++)
            o[i] = h[(v >> (28 - i * 4)) & 0xF];
        o[8] = 0;
        kernel_debug(o);
    }

    kernel_debug(" FB=");
    {
        char h[] = "0123456789ABCDEF";
        char o[9];
        uint32_t v = (uint32_t)(uintptr_t)fis_area;
        for (int i = 0; i < 8; i++)
            o[i] = h[(v >> (28 - i * 4)) & 0xF];
        o[8] = 0;
        kernel_debug(o);
    }

    kernel_debug(" CT=");
    {
        char h[] = "0123456789ABCDEF";
        char o[9];
        uint32_t v = (uint32_t)(uintptr_t)&command_table;
        for (int i = 0; i < 8; i++)
            o[i] = h[(v >> (28 - i * 4)) & 0xF];
        o[8] = 0;
        kernel_debug(o);
    }

    kernel_debug("\r\n");

    volatile uint32_t *is =
        (volatile uint32_t *)
        (ahci_port + PORT_IS);

    *is = 0xFFFFFFFFU;

    volatile uint32_t *serr =
        (volatile uint32_t *)
        (ahci_port + PORT_SERR);

    *serr = 0xFFFFFFFFU;

    command_list[0].ctba =
        (uint32_t)(uintptr_t)&command_table;

    command_list[0].ctbau =
        (uint32_t)(
            (uintptr_t)&command_table >> 32
        );

    ahci_slot = 0;

    if (!ahci_port_start())
        return 0;

    ahci_ready = 1;

    return 1;
}

static int ahci_transfer(
    uint32_t lba,
    void *buffer,
    int write
)
{
    if (!ahci_ready || !buffer)
        return 0;

    if (((uintptr_t)buffer >> 32) != 0)
        return 0;

    volatile uint32_t *is =
        (volatile uint32_t *)
        (ahci_port + PORT_IS);

    volatile uint32_t *tfd =
        (volatile uint32_t *)
        (ahci_port + PORT_TFD);

    volatile uint32_t *ci =
        (volatile uint32_t *)
        (ahci_port + PORT_CI);

    *is = 0xFFFFFFFFU;

    if (!ahci_wait_clear(
            tfd,
            PORT_TFD_BSY | PORT_TFD_DRQ))
        return 0;

    struct HBACommandHeader *header =
        &command_list[ahci_slot];

    ahci_zero(
        header,
        sizeof(*header)
    );

    ahci_zero(
        &command_table,
        sizeof(command_table)
    );

    header->flags =
        5 | (write ? (1U << 6) : 0);

    header->prdt_length = 1;

    header->ctba =
        (uint32_t)(uintptr_t)&command_table;

    header->ctbau =
        (uint32_t)(
            (uintptr_t)&command_table >> 32
        );

    command_table.prdt[0].dba =
        (uint32_t)(uintptr_t)buffer;

    command_table.prdt[0].dbau =
        (uint32_t)(
            (uintptr_t)buffer >> 32
        );

    command_table.prdt[0].dbc =
        (AHCI_SECTOR_SIZE - 1) |
        (1U << 31);

    kernel_debug("[AHCI] BUF=");
    {
        char h[] = "0123456789ABCDEF";
        char o[9];
        uint32_t v = (uint32_t)(uintptr_t)buffer;
        for (int i = 0; i < 8; i++)
            o[i] = h[(v >> (28 - i * 4)) & 0xF];
        o[8] = 0;
        kernel_debug(o);
    }

    kernel_debug(" PRDT=");
    {
        char h[] = "0123456789ABCDEF";
        char o[9];
        uint32_t v = command_table.prdt[0].dba;
        for (int i = 0; i < 8; i++)
            o[i] = h[(v >> (28 - i * 4)) & 0xF];
        o[8] = 0;
        kernel_debug(o);
    }

    kernel_debug(" CTBA=");
    {
        char h[] = "0123456789ABCDEF";
        char o[9];
        uint32_t v = header->ctba;
        for (int i = 0; i < 8; i++)
            o[i] = h[(v >> (28 - i * 4)) & 0xF];
        o[8] = 0;
        kernel_debug(o);
    }

    kernel_debug("\r\n");

    uint8_t *fis =
        command_table.cfis;

    fis[0] = 0x27;
    fis[1] = 1 << 7;
    fis[2] = write ? 0x35 : 0x25;
    fis[3] = 0;

    fis[4] = (uint8_t)lba;
    fis[5] = (uint8_t)(lba >> 8);
    fis[6] = (uint8_t)(lba >> 16);
    fis[7] = 0x40;

    fis[8] = (uint8_t)(lba >> 24);
    fis[9] = 0;
    fis[10] = 0;
    fis[11] = 0;

    fis[12] = 1;
    fis[13] = 0;

    kernel_debug("[AHCI] BEFORE CI LBA=");

    {
        char d[12];
        int p = 0;
        uint32_t x = lba;

        if (x == 0)
            d[p++] = '0';
        else {
            char t[12];
            int n = 0;

            while (x) {
                t[n++] = '0' + (x % 10);
                x /= 10;
            }

            while (n)
                d[p++] = t[--n];
        }

        d[p] = 0;
        kernel_debug(d);
        kernel_debug("\r\n");
    }

    kernel_debug("[AHCI] CI BEFORE=");

    {
        char hex[] = "0123456789ABCDEF";
        char out[9];
        uint32_t v = *ci;

        for (int i = 0; i < 8; i++)
            out[i] = hex[(v >> (28 - i * 4)) & 0xF];

        out[8] = 0;
        kernel_debug(out);
        kernel_debug("\r\n");
    }

    *ci = 1U << ahci_slot;

    kernel_debug("[AHCI] CI AFTER=");

    {
        char hex[] = "0123456789ABCDEF";
        char out[9];
        uint32_t v = *ci;

        for (int i = 0; i < 8; i++)
            out[i] = hex[(v >> (28 - i * 4)) & 0xF];

        out[8] = 0;
        kernel_debug(out);
        kernel_debug("\r\n");
    }

    uint32_t timeout = AHCI_TIMEOUT;

    while (timeout--) {

        uint32_t status = *is;

        if (status & PORT_IS_TFES)
            return 0;

        if (!(*ci & (1U << ahci_slot))) {
            kernel_debug("[AHCI] DONE LBA=");

            char d[12];
            char t[12];
            char hex[] = "0123456789ABCDEF";
            int p = 0;
            int n = 0;
            uint32_t x = lba;

            if (x == 0) {
                d[p++] = '0';
            } else {
                while (x) {
                    t[n++] = '0' + (x % 10);
                    x /= 10;
                }

                while (n)
                    d[p++] = t[--n];
            }

            d[p] = 0;
            kernel_debug(d);

            kernel_debug(" BUF=");

            x = (uint32_t)(uintptr_t)buffer;

            for (int i = 7; i >= 0; i--) {
                char c[2];
                c[0] = hex[(x >> (i * 4)) & 0xF];
                c[1] = 0;
                kernel_debug(c);
            }

            kernel_debug(" PRDBC=");

            x = command_list[ahci_slot].prdbc;
            p = 0;
            n = 0;

            if (x == 0) {
                d[p++] = '0';
            } else {
                while (x) {
                    t[n++] = '0' + (x % 10);
                    x /= 10;
                }

                while (n)
                    d[p++] = t[--n];
            }

            d[p] = 0;
            kernel_debug(d);

            kernel_debug(" DATA=");

            {
                char h[] = "0123456789ABCDEF";
                uint8_t *b = (uint8_t *)buffer;

                for (int j = 0; j < 4; j++) {
                    char o[3];
                    o[0] = h[(b[j] >> 4) & 0xF];
                    o[1] = h[b[j] & 0xF];
                    o[2] = 0;
                    kernel_debug(o);

                    if (j != 3)
                        kernel_debug(" ");
                }
            }

            kernel_debug("\\r\\n");

            return 1;
        }

        __asm__ volatile ("pause");
    }

    return 0;
}

int ahci_read_sector(
    uint32_t lba,
    void *buffer
)
{
    return ahci_transfer(
        lba,
        buffer,
        0
    );
}

int ahci_write_sector(
    uint32_t lba,
    const void *buffer
)
{
    return ahci_transfer(
        lba,
        (void *)buffer,
        1
    );
}
