#ifndef NICOS_GDT_H
#define NICOS_GDT_H

#include <stdint.h>

void gdt_init(void);
void gdt_set_kernel_stack(uint64_t rsp0);

#endif
