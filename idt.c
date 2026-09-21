#include <stdint.h>
#include "idt.h"

extern void isr_syscall(void);
extern void isr_gp(void);

struct IDTEntry {
    uint16_t offset_low;
    uint16_t selector;
    uint8_t  ist;
    uint8_t  type_attr;
    uint16_t offset_mid;
    uint32_t offset_high;
    uint32_t reserved;
} __attribute__((packed));

struct IDTPointer {
    uint16_t limit;
    uint64_t base;
} __attribute__((packed));

static struct IDTEntry idt[256]
    __attribute__((aligned(16)));

static struct IDTPointer idt_pointer;

static void idt_set_gate(
    uint8_t vector,
    uint64_t handler,
    uint16_t selector,
    uint8_t type_attr
)
{
    idt[vector].offset_low =
        (uint16_t)(handler & 0xFFFF);

    idt[vector].selector =
        selector;

    idt[vector].ist = 0;

    idt[vector].type_attr =
        type_attr;

    idt[vector].offset_mid =
        (uint16_t)((handler >> 16) & 0xFFFF);

    idt[vector].offset_high =
        (uint32_t)((handler >> 32) & 0xFFFFFFFF);

    idt[vector].reserved = 0;
}

void idt_init(void)
{
    for (uint64_t i = 0; i < 256; i++) {
        idt[i].offset_low = 0;
        idt[i].selector = 0;
        idt[i].ist = 0;
        idt[i].type_attr = 0;
        idt[i].offset_mid = 0;
        idt[i].offset_high = 0;
        idt[i].reserved = 0;
    }

    


 
    idt_set_gate(
        0x80,
        (uint64_t)isr_syscall,
        0x08,
        0xEE
    );

     
    idt_set_gate(
        13,
        (uint64_t)isr_gp,
        0x08,
        0x8E
    );

    idt_pointer.limit =
        sizeof(idt) - 1;

    idt_pointer.base =
        (uint64_t)&idt;

    __asm__ volatile (
        "lidt %0"
        :
        : "m"(idt_pointer)
        : "memory"
    );
}
