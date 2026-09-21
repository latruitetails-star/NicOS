#include <stdint.h>
#include "pci.h"

uint32_t pci_read32(
    uint8_t bus,
    uint8_t device,
    uint8_t function,
    uint8_t offset
)
{
    uint32_t address =
        (1U << 31) |
        ((uint32_t)bus << 16) |
        ((uint32_t)device << 11) |
        ((uint32_t)function << 8) |
        (offset & 0xFC);

    __asm__ volatile (
        "outl %0, %1"
        :
        : "a"(address), "Nd"((uint16_t)0xCF8)
    );

    uint32_t value;

    __asm__ volatile (
        "inl %1, %0"
        : "=a"(value)
        : "Nd"((uint16_t)0xCFC)
    );

    return value;
}

uint16_t pci_vendor(
    uint8_t bus,
    uint8_t device,
    uint8_t function
)
{
    return (uint16_t)(
        pci_read32(bus, device, function, 0x00)
        & 0xFFFF
    );
}

uint16_t pci_device_id(
    uint8_t bus,
    uint8_t device,
    uint8_t function
)
{
    return (uint16_t)(
        pci_read32(bus, device, function, 0x00) >> 16
    );
}

uint32_t pci_class_reg(
    uint8_t bus,
    uint8_t device,
    uint8_t function
)
{
    return pci_read32(bus, device, function, 0x08);
}

uint64_t pci_bar(
    uint8_t bus,
    uint8_t device,
    uint8_t function,
    uint8_t index
)
{
    if (index >= 6)
        return 0;

    uint8_t offset = 0x10 + index * 4;

    uint32_t low =
        pci_read32(bus, device, function, offset);

    if (low == 0 || low == 0xFFFFFFFF)
        return 0;

    


 
    if (low & 1)
        return (uint64_t)(low & 0xFFFFFFFC);

    uint32_t type = (low >> 1) & 3;

    

 
    if (type == 2 && index < 5) {
        uint32_t high =
            pci_read32(bus, device, function, offset + 4);

        return ((uint64_t)high << 32) |
               (uint64_t)(low & 0xFFFFFFF0);
    }

    

 
    return (uint64_t)(low & 0xFFFFFFF0);
}

static void pci_read_device(
    uint8_t bus,
    uint8_t device,
    uint8_t function,
    PCI_Device *out
)
{
    uint32_t class_reg =
        pci_class_reg(bus, device, function);

    out->bus = bus;
    out->device = device;
    out->function = function;

    out->vendor_id =
        pci_vendor(bus, device, function);

    out->device_id =
        pci_device_id(bus, device, function);

    out->class_code =
        (uint8_t)(class_reg >> 24);

    out->subclass =
        (uint8_t)(class_reg >> 16);

    out->prog_if =
        (uint8_t)(class_reg >> 8);

    for (uint8_t i = 0; i < 6; i++)
        out->bar[i] = pci_bar(
            bus,
            device,
            function,
            i
        );
}

void pci_scan(pci_enum_callback_t callback)
{
    if (!callback)
        return;

    for (uint32_t bus = 0; bus < 256; bus++) {

        if ((bus & 31) == 0) {
            char c = '0' + (char)(bus / 32);

            __asm__ volatile (
                "outb %0, %1"
                :
                : "a"(c), "Nd"((uint16_t)0x402)
            );
        }

        for (uint32_t device = 0; device < 32; device++) {

            


 
            uint16_t vendor0 =
                pci_vendor(
                    (uint8_t)bus,
                    (uint8_t)device,
                    0
                );

            if (vendor0 == 0xFFFF)
                continue;

            uint32_t header =
                pci_read32(
                    (uint8_t)bus,
                    (uint8_t)device,
                    0,
                    0x0C
                );

            uint8_t header_type =
                (uint8_t)(header >> 16);

            uint8_t functions =
                (header_type & 0x80) ? 8 : 1;

            for (uint32_t function = 0;
                 function < functions;
                 function++) {

                uint16_t vendor =
                    pci_vendor(
                        (uint8_t)bus,
                        (uint8_t)device,
                        (uint8_t)function
                    );

                if (vendor == 0xFFFF)
                    continue;

                PCI_Device dev;

                pci_read_device(
                    (uint8_t)bus,
                    (uint8_t)device,
                    (uint8_t)function,
                    &dev
                );

                if (callback(&dev))
                    return;
            }
        }
    }
}

static PCI_Device *xhci_result;

static int pci_xhci_callback(PCI_Device *dev)
{
    if (dev->class_code == 0x0C &&
        dev->subclass == 0x03 &&
        dev->prog_if == 0x30) {

        if (xhci_result)
            *xhci_result = *dev;

        return 1;
    }

    return 0;
}

int pci_find_xhci(PCI_Device *out)
{
    if (!out)
        return 0;

    xhci_result = out;
    pci_scan(pci_xhci_callback);
    xhci_result = 0;

    return out->vendor_id != 0;
}


static void pci_debug_out8(uint8_t value)
{
    __asm__ volatile (
        "outb %0, %1"
        :
        : "a"((uint8_t)value),
          "Nd"((uint16_t)0x402)
    );
}

static void pci_debug_text(const char *s)
{
    while (*s)
        pci_debug_out8((uint8_t)*s++);
}

static void pci_debug_hex8(uint8_t v)
{
    const char *h = "0123456789ABCDEF";

    pci_debug_out8((uint8_t)h[(v >> 4) & 0x0F]);
    pci_debug_out8((uint8_t)h[v & 0x0F]);
}

static void pci_debug_hex16(uint16_t v)
{
    pci_debug_hex8((uint8_t)(v >> 8));
    pci_debug_hex8((uint8_t)v);
}

static int pci_debug_callback(PCI_Device *dev)
{
    if (dev->vendor_id != 0x8086 || dev->device_id != 0x100E)
        return 0;

    pci_debug_text("[E1000] FOUND ");

    pci_debug_hex8(dev->bus);
    pci_debug_out8(':');
    pci_debug_hex8(dev->device);
    pci_debug_out8('.');
    pci_debug_hex8(dev->function);

    pci_debug_text(" VID=");
    pci_debug_hex16(dev->vendor_id);

    pci_debug_text(" DID=");
    pci_debug_hex16(dev->device_id);

    pci_debug_text("\r\n");

    for (uint8_t i = 0; i < 6; i++)
    {
        pci_debug_text("[E1000] BAR");
        pci_debug_hex8(i);
        pci_debug_text(" = ");

        uint64_t bar = dev->bar[i];

        pci_debug_hex16((uint16_t)(bar >> 48));
        pci_debug_hex16((uint16_t)(bar >> 32));
        pci_debug_hex16((uint16_t)(bar >> 16));
        pci_debug_hex16((uint16_t)bar);

        pci_debug_text("\r\n");
    }

    return 0;
}

void pci_debug_scan(void)
{
    pci_scan(pci_debug_callback);
}
