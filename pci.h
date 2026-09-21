#ifndef NICOS_PCI_H
#define NICOS_PCI_H

#include <stdint.h>

typedef struct {
    uint8_t bus;
    uint8_t device;
    uint8_t function;

    uint16_t vendor_id;
    uint16_t device_id;

    uint8_t class_code;
    uint8_t subclass;
    uint8_t prog_if;

    uint64_t bar[6];
} PCI_Device;

typedef int (*pci_enum_callback_t)(PCI_Device *dev);

uint32_t pci_read32(uint8_t bus, uint8_t device,
                    uint8_t function, uint8_t offset);

uint16_t pci_vendor(uint8_t bus, uint8_t device,
                    uint8_t function);

uint16_t pci_device_id(uint8_t bus, uint8_t device,
                       uint8_t function);

uint32_t pci_class_reg(uint8_t bus, uint8_t device,
                       uint8_t function);

uint64_t pci_bar(uint8_t bus, uint8_t device,
                 uint8_t function, uint8_t index);

void pci_scan(pci_enum_callback_t callback);

int pci_find_xhci(PCI_Device *out);
void pci_debug_scan(void);

#endif
