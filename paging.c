#include <stdint.h>
#include "paging.h"

#define PAGE_TABLE_ENTRIES 512
#define PAGE_SIZE          0x1000ULL

#define PAGE_PRESENT  (1ULL << 0)
#define PAGE_WRITE    (1ULL << 1)
#define PAGE_USER     (1ULL << 2)
#define PAGE_HUGE     (1ULL << 7)













 

#define USER_BASE 0x0000010000000000ULL

static uint64_t pml4[PAGE_TABLE_ENTRIES]
    __attribute__((aligned(4096)));

static uint64_t pdpt_low[PAGE_TABLE_ENTRIES]
    __attribute__((aligned(4096)));

static uint64_t pdpt_high[PAGE_TABLE_ENTRIES]
    __attribute__((aligned(4096)));










 
static uint64_t pdpt_user[PAGE_TABLE_ENTRIES]
    __attribute__((aligned(4096)));

static uint64_t pd_user[PAGE_TABLE_ENTRIES]
    __attribute__((aligned(4096)));

static uint64_t pt_user[PAGE_TABLE_ENTRIES]
    __attribute__((aligned(4096)));

extern uint64_t pmm_alloc_page(void);
extern void pmm_free_page(uint64_t address);

static void paging_zero(void)
{
    for (uint64_t i = 0; i < PAGE_TABLE_ENTRIES; i++) {
        pml4[i] = 0;
        pdpt_low[i] = 0;
        pdpt_high[i] = 0;
        pdpt_user[i] = 0;
        pd_user[i] = 0;
        pt_user[i] = 0;
    }
}

void paging_init(void)
{
    paging_zero();

    

 
    pml4[0] =
        ((uint64_t)pdpt_low) |
        PAGE_PRESENT |
        PAGE_WRITE;

    

 
    pml4[1] =
        ((uint64_t)pdpt_high) |
        PAGE_PRESENT |
        PAGE_WRITE;

    

 
    for (uint64_t i = 0; i < PAGE_TABLE_ENTRIES; i++) {
        uint64_t address = i * 0x40000000ULL;

        pdpt_low[i] =
            address |
            PAGE_PRESENT |
            PAGE_WRITE |
            PAGE_HUGE;
    }

    

 
    for (uint64_t i = 0; i < PAGE_TABLE_ENTRIES; i++) {
        uint64_t address =
            0x8000000000ULL +
            (i * 0x40000000ULL);

        pdpt_high[i] =
            address |
            PAGE_PRESENT |
            PAGE_WRITE |
            PAGE_HUGE;
    }

    







 
    pml4[2] =
        ((uint64_t)pdpt_user) |
        PAGE_PRESENT |
        PAGE_WRITE |
        PAGE_USER;

    pdpt_user[0] =
        ((uint64_t)pd_user) |
        PAGE_PRESENT |
        PAGE_WRITE |
        PAGE_USER;

    pd_user[0] =
        ((uint64_t)pt_user) |
        PAGE_PRESENT |
        PAGE_WRITE |
        PAGE_USER;

    


 

    __asm__ volatile (
        "mov %0, %%cr3"
        :
        : "r"((uint64_t)pml4)
        : "memory"
    );
}









 
int paging_map_user_page(
    uint64_t virtual_address,
    uint64_t physical_address
)
{
    if (virtual_address < USER_BASE)
        return 0;

    if (virtual_address >= USER_BASE + 0x200000ULL)
        return 0;

    if (virtual_address & (PAGE_SIZE - 1))
        return 0;

    if (physical_address & (PAGE_SIZE - 1))
        return 0;

    uint64_t index =
        (virtual_address - USER_BASE) / PAGE_SIZE;

    if (index >= PAGE_TABLE_ENTRIES)
        return 0;

    pt_user[index] =
        physical_address |
        PAGE_PRESENT |
        PAGE_WRITE |
        PAGE_USER;

    


 
    __asm__ volatile (
        "mov %%cr3, %%rax\n"
        "mov %%rax, %%cr3\n"
        :
        :
        : "rax", "memory"
    );

    return 1;
}





 
uint64_t paging_alloc_user_page(uint64_t virtual_address)
{
    uint64_t physical_address = pmm_alloc_page();

    if (physical_address == 0)
        return 0;

    if (!paging_map_user_page(
            virtual_address,
            physical_address)) {
        pmm_free_page(physical_address);
        return 0;
    }

    return virtual_address;
}
