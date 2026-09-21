#ifndef RING3_H
#define RING3_H

#include <stdint.h>

uint64_t ring3_enter(uint64_t entry, uint64_t stack);
void ring3_exit(uint64_t status) __attribute__((noreturn));

#endif
