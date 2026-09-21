#include <stdint.h>
#include "gdt.h"

struct GDTEntry {
    uint16_t limit_low;
    uint16_t base_low;
    uint8_t  base_middle;
    uint8_t  access;
    uint8_t  granularity;
    uint8_t  base_high;
} __attribute__((packed));

struct GDTPointer {
    uint16_t limit;
    uint64_t base;
} __attribute__((packed));

struct TSS {
    uint32_t reserved0;

    uint64_t rsp0;
    uint64_t rsp1;
    uint64_t rsp2;

    uint64_t reserved1;

    uint64_t ist1;
    uint64_t ist2;
    uint64_t ist3;
    uint64_t ist4;
    uint64_t ist5;
    uint64_t ist6;
    uint64_t ist7;

    uint64_t reserved2;
    uint16_t reserved3;
    uint16_t iomap_base;
} __attribute__((packed));

static struct GDTEntry gdt[7]
    __attribute__((aligned(8)));

static struct TSS tss
    __attribute__((aligned(8)));

static uint8_t kernel_stack[16384]
    __attribute__((aligned(16)));

static struct GDTPointer gdt_pointer;

static void gdt_set_entry(
    int index,
    uint64_t base,
    uint32_t limit,
    uint8_t access,
    uint8_t granularity
)
{
    gdt[index].base_low =
        (uint16_t)(base & 0xFFFF);

    gdt[index].base_middle =
        (uint8_t)((base >> 16) & 0xFF);

    gdt[index].base_high =
        (uint8_t)((base >> 24) & 0xFF);

    gdt[index].limit_low =
        (uint16_t)(limit & 0xFFFF);

    gdt[index].access = access;

    gdt[index].granularity =
        (uint8_t)((limit >> 16) & 0x0F);

    gdt[index].granularity |=
        granularity & 0xF0;
}

void gdt_set_kernel_stack(uint64_t rsp0)
{
    tss.rsp0 = rsp0;
}

void gdt_init(void)
{
    for (int i = 0; i < 6; i++) {
        gdt[i].limit_low = 0;
        gdt[i].base_low = 0;
        gdt[i].base_middle = 0;
        gdt[i].access = 0;
        gdt[i].granularity = 0;
        gdt[i].base_high = 0;
    }

    tss = (struct TSS){0};
    tss.rsp0 = (uint64_t)(kernel_stack + sizeof(kernel_stack));
    tss.iomap_base = sizeof(struct TSS);

    

 

    

 
    gdt_set_entry(
        1,
        0,
        0xFFFFF,
        0x9A,
        0xA0
    );

    

 
    gdt_set_entry(
        2,
        0,
        0xFFFFF,
        0x92,
        0xC0
    );

    



 
    gdt_set_entry(
        3,
        0,
        0xFFFFF,
        0xFA,
        0xA0
    );

    



 
    gdt_set_entry(
        4,
        0,
        0xFFFFF,
        0xF2,
        0xC0
    );

    




 
    uint64_t base = (uint64_t)&tss;
    uint32_t limit = sizeof(struct TSS) - 1;

    gdt_set_entry(
        5,
        base,
        limit,
        0x89,
        0x00
    );

    

 
    gdt[6].limit_low =
        (uint16_t)((base >> 32) & 0xFFFF);

    gdt[6].base_low =
        (uint16_t)((base >> 48) & 0xFFFF);

    gdt[6].base_middle = 0;
    gdt[6].access = 0;
    gdt[6].granularity = 0;
    gdt[6].base_high = 0;

    gdt_pointer.limit =
        sizeof(gdt) - 1;

    gdt_pointer.base =
        (uint64_t)&gdt;

    __asm__ volatile (
        "lgdt %0\n"

        "pushq $0x08\n"
        "lea 1f(%%rip), %%rax\n"
        "pushq %%rax\n"
        "lretq\n"

        "1:\n"

        "mov $0x10, %%ax\n"
        "mov %%ax, %%ds\n"
        "mov %%ax, %%es\n"
        "mov %%ax, %%ss\n"
        "mov %%ax, %%fs\n"
        "mov %%ax, %%gs\n"

        :
        : "m"(gdt_pointer)
        : "rax", "memory"
    );

    __asm__ volatile (
        "mov $0x28, %%ax\n"
        "ltr %%ax\n"
        :
        :
        : "rax", "memory"
    );
}
