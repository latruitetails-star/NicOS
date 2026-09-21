#include <stdint.h>

#define PAGE_SIZE 4096ULL

extern uint64_t pmm_alloc_page(void);
extern void pmm_free_page(uint64_t address);

static uint64_t heap_current = 0;
static uint64_t heap_end = 0;

void heap_init(void)
{
    heap_current = 0;
    heap_end = 0;
}

static int heap_grow(void)
{
    uint64_t page = pmm_alloc_page();

    if (page == 0)
        return 0;

    





 

    if (heap_current == 0) {
        heap_current = page;
        heap_end = page + PAGE_SIZE;
    } else {
        if (page != heap_end)
            return 0;

        heap_end += PAGE_SIZE;
    }

    return 1;
}

void *kmalloc(uint64_t size)
{
    if (size == 0)
        return 0;

    

 
    size = (size + 7) & ~7ULL;

    if (heap_current == 0 || heap_current + size > heap_end) {
        if (!heap_grow())
            return 0;
    }

    void *result = (void *)(uintptr_t)heap_current;
    heap_current += size;

    return result;
}
