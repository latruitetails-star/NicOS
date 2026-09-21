#include "paging.h"
#include "ring3.h"
#include "gdt.h"
#include "idt.h"
#include "block.h"
extern int block_init(void);
extern int block_get_driver(void);
extern int block_read_sector(uint32_t lba, void *buffer);

#include <stdint.h>
#include "input.h"
#include "pci.h"
#include "graphics.h"
#include "window.h"

extern int xhci_keyboard_init(void);
extern int xhci_keyboard_read_scancode(void);


void kernel_debug(const char *s)
{
    while (*s) {
        __asm__ volatile (
            "outb %0, %1"
            :
            : "a"(*s), "Nd"((uint16_t)0x402)
        );
        s++;
    }
}

void *memset(void *dst, int value, uint64_t size)
{
    uint8_t *p = (uint8_t *)dst;

    for (uint64_t i = 0; i < size; i++)
        p[i] = (uint8_t)value;

    return dst;
}

extern int fs_init(void);
extern int fs_touch(const char *name);
extern int fs_write_file(
    const char *name,
    const char *data,
    uint32_t size
);
extern int fs_read_file(
    const char *name,
    char *buffer,
    uint32_t buffer_size
);

extern int fs_mkdir(const char *name);
extern uint32_t fs_list(
    char names[][56],
    uint8_t *types,
    uint32_t max_entries
);


extern int ata_read_sector(uint32_t lba, void *buffer);
extern int ata_write_sector(uint32_t lba, const void *buffer);



extern void heap_init(void);
extern void *kmalloc(uint64_t size);








 





 




 

#define PAGE_SIZE 4096ULL
#define PMM_MAX_MEMORY (4ULL * 1024ULL * 1024ULL * 1024ULL)
#define PMM_MAX_PAGES (PMM_MAX_MEMORY / PAGE_SIZE)
#define PMM_BITMAP_SIZE (PMM_MAX_PAGES / 8)

#define EFI_CONVENTIONAL_MEMORY 7

typedef struct {
    uint32_t type;
    uint32_t reserved;
    uint64_t physical_start;
    uint64_t virtual_start;
    uint64_t number_of_pages;
    uint64_t attribute;
} EFI_MEMORY_DESCRIPTOR;

static uint8_t pmm_bitmap[PMM_BITMAP_SIZE];

uint64_t pmm_free_pages;
static uint64_t pmm_total_memory;


static void pmm_set_used(uint64_t page)
{
    if (page >= PMM_MAX_PAGES)
        return;

    pmm_bitmap[page >> 3] |=
        (uint8_t)(1 << (page & 7));
}


static void pmm_set_free(uint64_t page)
{
    if (page >= PMM_MAX_PAGES)
        return;

    pmm_bitmap[page >> 3] &=
        (uint8_t)~(1 << (page & 7));
}


static int pmm_is_free(uint64_t page)
{
    if (page >= PMM_MAX_PAGES)
        return 0;

    return !(pmm_bitmap[page >> 3] &
             (uint8_t)(1 << (page & 7)));
}


static void pmm_init(BootInfo *boot)
{
    

 
    for (uint64_t i = 0; i < PMM_BITMAP_SIZE; i++)
        pmm_bitmap[i] = 0xFF;

    pmm_free_pages = 0;
    pmm_total_memory = 0;

    if (!boot->memory_map ||
        !boot->memory_map_size ||
        !boot->memory_descriptor_size)
        return;

    uint64_t offset = 0;

    while (offset + boot->memory_descriptor_size <=
           boot->memory_map_size) {

        EFI_MEMORY_DESCRIPTOR *desc =
            (EFI_MEMORY_DESCRIPTOR *)
            ((uint8_t *)boot->memory_map + offset);

        


 
        if (desc->type == EFI_CONVENTIONAL_MEMORY) {

            uint64_t start_address =
                desc->physical_start;

            uint64_t pages =
                desc->number_of_pages;

            

 
            if (start_address < 0x100000) {

                uint64_t skip =
                    (0x100000 - start_address +
                     PAGE_SIZE - 1) / PAGE_SIZE;

                if (skip >= pages) {
                    offset += boot->memory_descriptor_size;
                    continue;
                }

                start_address += skip * PAGE_SIZE;
                pages -= skip;
            }

            for (uint64_t i = 0; i < pages; i++) {

                uint64_t address =
                    start_address + i * PAGE_SIZE;

                if (address >= PMM_MAX_MEMORY)
                    break;

                uint64_t page =
                    address / PAGE_SIZE;

                if (!pmm_is_free(page)) {
                    pmm_set_free(page);
                    pmm_free_pages++;
                }
            }
        }

        offset += boot->memory_descriptor_size;
    }

    



 
    pmm_total_memory =
        pmm_free_pages * PAGE_SIZE;
}

uint64_t pmm_alloc_page(void)
{
    for (uint64_t byte = 0;
         byte < PMM_BITMAP_SIZE;
         byte++) {

        if (pmm_bitmap[byte] == 0xFF)
            continue;

        for (uint32_t bit = 0; bit < 8; bit++) {

            if (!(pmm_bitmap[byte] &
                  (uint8_t)(1 << bit))) {

                uint64_t page =
                    byte * 8 + bit;

                pmm_set_used(page);

                if (pmm_free_pages)
                    pmm_free_pages--;

                return page * PAGE_SIZE;
            }
        }
    }

    return 0;
}







 
uint64_t pmm_alloc_contiguous(uint64_t page_count)
{
    if (page_count == 0)
        return 0;

    uint64_t run_start = 0;
    uint64_t run_length = 0;

    for (uint64_t page = 0;
         page < PMM_BITMAP_SIZE * 8ULL;
         page++) {

        if (!pmm_is_free(page)) {
            run_length = 0;
            continue;
        }

        if (run_length == 0)
            run_start = page;

        run_length++;

        if (run_length == page_count) {

            for (uint64_t i = 0; i < page_count; i++)
                pmm_set_used(run_start + i);

            if (pmm_free_pages >= page_count)
                pmm_free_pages -= page_count;
            else
                pmm_free_pages = 0;

            return run_start * PAGE_SIZE;
        }
    }

    return 0;
}

void pmm_free_page(uint64_t address)
{
    if (address < 0x100000)
        return;

    if (address >= PMM_MAX_MEMORY)
        return;

    if (address & (PAGE_SIZE - 1))
        return;

    uint64_t page = address / PAGE_SIZE;

    if (!pmm_is_free(page)) {
        pmm_set_free(page);
        pmm_free_pages++;
    }
}


static uint64_t pmm_get_total_memory(void)
{
    return pmm_total_memory;
}



static void terminal_newline(BootInfo *boot);
void terminal_char(BootInfo *boot, char c);
void syscall_set_boot_info(BootInfo *boot);
static void terminal_string(BootInfo *boot, const char *s);

static void terminal_hex(BootInfo *boot, uint64_t value)
{
    const char *hex = "0123456789ABCDEF";

    terminal_string(boot, "0x");

    for (int i = 15; i >= 0; i--)
        terminal_char(boot, hex[(value >> (i * 4)) & 0xF]);
}

static uint64_t test_page = 0;

static void command_alloc(BootInfo *boot)
{
    uint64_t page = pmm_alloc_page();

    if (page == 0) {
        terminal_string(boot, "alloc: out of memory");
        terminal_newline(boot);
        return;
    }

    test_page = page;

    terminal_string(boot, "allocated page: ");
    terminal_hex(boot, page);
    terminal_newline(boot);
}

static void command_free(BootInfo *boot)
{
    if (test_page == 0) {
        terminal_string(boot, "free: no allocated page");
        terminal_newline(boot);
        return;
    }

    pmm_free_page(test_page);

    terminal_string(boot, "freed page: ");
    terminal_hex(boot, test_page);
    terminal_newline(boot);

    test_page = 0;
}

static uint64_t pmm_get_free_memory(void)
{
    return pmm_free_pages * PAGE_SIZE;
}




 

static void put_pixel(
    BootInfo *boot,
    uint32_t x,
    uint32_t y,
    uint32_t color
)
{
    if (x >= boot->width || y >= boot->height)
        return;

    volatile uint32_t *fb =
        (volatile uint32_t *)boot->framebuffer;

    fb[y * boot->pixels_per_scanline + x] = color;
}


void draw_hline(
    BootInfo *boot,
    uint32_t x,
    uint32_t y,
    uint32_t width,
    uint32_t color
);

void draw_rect(
    BootInfo *boot,
    uint32_t x,
    uint32_t y,
    uint32_t width,
    uint32_t height,
    uint32_t color
)
{
    if (x >= boot->width || y >= boot->height)
        return;

    if (x + width > boot->width)
        width = boot->width - x;

    if (y + height > boot->height)
        height = boot->height - y;

    for (uint32_t yy = 0; yy < height; yy++)
        draw_hline(
            boot,
            x,
            y + yy,
            width,
            color
        );
}


void draw_hline(
    BootInfo *boot,
    uint32_t x,
    uint32_t y,
    uint32_t width,
    uint32_t color
)
{
    if (y >= boot->height || x >= boot->width)
        return;

    if (x + width > boot->width)
        width = boot->width - x;

    volatile uint32_t *fb =
        (volatile uint32_t *)boot->framebuffer;

    for (uint32_t i = 0; i < width; i++)
        fb[y * boot->pixels_per_scanline + x + i] = color;
}


static void clear_screen(
    BootInfo *boot,
    uint32_t color
)
{
    volatile uint32_t *fb =
        (volatile uint32_t *)boot->framebuffer;

    uint64_t total =
        (uint64_t)boot->height *
        boot->pixels_per_scanline;

    for (uint64_t i = 0; i < total; i++)
        fb[i] = color;
}








 

static const uint8_t letters[62][7] = {

     
    {0b01110,0b10001,0b10001,0b11111,0b10001,0b10001,0b10001},

     
    {0b11110,0b10001,0b10001,0b11110,0b10001,0b10001,0b11110},

     
    {0b01110,0b10001,0b10000,0b10000,0b10000,0b10001,0b01110},

     
    {0b11110,0b10001,0b10001,0b10001,0b10001,0b10001,0b11110},

     
    {0b11111,0b10000,0b10000,0b11110,0b10000,0b10000,0b11111},

     
    {0b11111,0b10000,0b10000,0b11110,0b10000,0b10000,0b10000},

     
    {0b01110,0b10001,0b10000,0b10111,0b10001,0b10001,0b01110},

     
    {0b10001,0b10001,0b10001,0b11111,0b10001,0b10001,0b10001},

     
    {0b11111,0b00100,0b00100,0b00100,0b00100,0b00100,0b11111},

     
    {0b00111,0b00010,0b00010,0b00010,0b00010,0b10010,0b01100},

     
    {0b10001,0b10010,0b10100,0b11000,0b10100,0b10010,0b10001},

     
    {0b10000,0b10000,0b10000,0b10000,0b10000,0b10000,0b11111},

     
    {0b10001,0b11011,0b10101,0b10101,0b10001,0b10001,0b10001},

     
    {0b10001,0b11001,0b10101,0b10011,0b10001,0b10001,0b10001},

     
    {0b01110,0b10001,0b10001,0b10001,0b10001,0b10001,0b01110},

     
    {0b11110,0b10001,0b10001,0b11110,0b10000,0b10000,0b10000},

     
    {0b01110,0b10001,0b10001,0b10001,0b10101,0b10010,0b01101},

     
    {0b11110,0b10001,0b10001,0b11110,0b10100,0b10010,0b10001},

     
    {0b01111,0b10000,0b10000,0b01110,0b00001,0b00001,0b11110},

     
    {0b11111,0b00100,0b00100,0b00100,0b00100,0b00100,0b00100},

     
    {0b10001,0b10001,0b10001,0b10001,0b10001,0b10001,0b01110},

     
    {0b10001,0b10001,0b10001,0b10001,0b10001,0b01010,0b00100},

     
    {0b10001,0b10001,0b10001,0b10101,0b10101,0b11011,0b10001},

     
    {0b10001,0b10001,0b01010,0b00100,0b01010,0b10001,0b10001},

     
    {0b10001,0b10001,0b01010,0b00100,0b00100,0b00100,0b00100},

     
    {0b11111,0b00001,0b00010,0b00100,0b01000,0b10000,0b11111},

     
    {0b00000,0b00000,0b01110,0b00001,0b01111,0b10001,0b01111},

     
    {0b10000,0b10000,0b10110,0b11001,0b10001,0b10001,0b11110},

     
    {0b00000,0b00000,0b01110,0b10001,0b10000,0b10001,0b01110},

     
    {0b00001,0b00001,0b01101,0b10011,0b10001,0b10001,0b01111},

     
    {0b00000,0b00000,0b01110,0b10001,0b11111,0b10000,0b01110},

     
    {0b00110,0b01001,0b01000,0b11100,0b01000,0b01000,0b01000},

     
    {0b00000,0b00000,0b01111,0b10001,0b01111,0b00001,0b01110},

     
    {0b10000,0b10000,0b10110,0b11001,0b10001,0b10001,0b10001},

     
    {0b00100,0b00000,0b01100,0b00100,0b00100,0b00100,0b01110},

     
    {0b00010,0b00000,0b00110,0b00010,0b00010,0b10010,0b01100},

     
    {0b10000,0b10000,0b10010,0b10100,0b11000,0b10100,0b10010},

     
    {0b01100,0b00100,0b00100,0b00100,0b00100,0b00100,0b01110},

     
    {0b00000,0b00000,0b11010,0b10101,0b10101,0b10101,0b10101},

     
    {0b00000,0b00000,0b10110,0b11001,0b10001,0b10001,0b10001},

     
    {0b00000,0b00000,0b01110,0b10001,0b10001,0b10001,0b01110},

     
    {0b00000,0b00000,0b11110,0b10001,0b11110,0b10000,0b10000},

     
    {0b00000,0b00000,0b01111,0b10001,0b01111,0b00001,0b00001},

     
    {0b00000,0b00000,0b10110,0b11001,0b10000,0b10000,0b10000},

     
    {0b00000,0b00000,0b01111,0b10000,0b01110,0b00001,0b11110},

     
    {0b01000,0b01000,0b11110,0b01000,0b01000,0b01001,0b00110},

     
    {0b00000,0b00000,0b10001,0b10001,0b10001,0b10011,0b01101},

     
    {0b00000,0b00000,0b10001,0b10001,0b10001,0b01010,0b00100},

     
    {0b00000,0b00000,0b10001,0b10101,0b10101,0b10101,0b01010},

     
    {0b00000,0b00000,0b10001,0b01010,0b00100,0b01010,0b10001},

     
    {0b00000,0b00000,0b10001,0b10001,0b01111,0b00001,0b01110},

     
    {0b00000,0b00000,0b11111,0b00010,0b00100,0b01000,0b11111},

     
    {0b01110,0b10001,0b10011,0b10101,0b11001,0b10001,0b01110},

     
    {0b00100,0b01100,0b00100,0b00100,0b00100,0b00100,0b01110},

     
    {0b01110,0b10001,0b00001,0b00010,0b00100,0b01000,0b11111},

     
    {0b11110,0b00001,0b00001,0b01110,0b00001,0b00001,0b11110},

     
    {0b00010,0b00110,0b01010,0b10010,0b11111,0b00010,0b00010},

     
    {0b11111,0b10000,0b10000,0b11110,0b00001,0b00001,0b11110},

     
    {0b01110,0b10000,0b10000,0b11110,0b10001,0b10001,0b01110},

     
    {0b11111,0b00001,0b00010,0b00100,0b01000,0b01000,0b01000},

     
    {0b01110,0b10001,0b10001,0b01110,0b10001,0b10001,0b01110},

     
    {0b01110,0b10001,0b10001,0b01111,0b00001,0b00001,0b01110}
};




 

void draw_glyph(
    BootInfo *boot,
    uint32_t x,
    uint32_t y,
    char c,
    uint32_t scale,
    uint32_t color
)
{
    uint8_t glyph[7] = {0, 0, 0, 0, 0, 0, 0};

    int index = -1;

    if (c >= 'A' && c <= 'Z')
        index = c - 'A';

    else if (c >= 'a' && c <= 'z')
        index = 26 + (c - 'a');

    else if (c >= '0' && c <= '9')
        index = 52 + (c - '0');

    if (index >= 0) {
        for (uint32_t row = 0; row < 7; row++)
            glyph[row] = letters[index][row];
    }
    else {
        switch (c) {

        case '.':
            glyph[5] = 0b00100;
            glyph[6] = 0b00100;
            break;

        case ',':
            glyph[5] = 0b00100;
            glyph[6] = 0b01000;
            break;

        case ':':
            glyph[1] = 0b00100;
            glyph[4] = 0b00100;
            break;

        case ';':
            glyph[1] = 0b00100;
            glyph[4] = 0b00100;
            glyph[6] = 0b01000;
            break;

        case '!':
            glyph[0] = 0b00100;
            glyph[1] = 0b00100;
            glyph[2] = 0b00100;
            glyph[3] = 0b00100;
            glyph[5] = 0b00100;
            break;

        case '?':
            glyph[0] = 0b01110;
            glyph[1] = 0b10001;
            glyph[2] = 0b00010;
            glyph[3] = 0b00100;
            glyph[5] = 0b00100;
            break;

        case '-':
            glyph[3] = 0b11111;
            break;

        case '_':
            glyph[6] = 0b11111;
            break;

        case '=':
            glyph[2] = 0b11111;
            glyph[4] = 0b11111;
            break;

        case '+':
            glyph[2] = 0b00100;
            glyph[3] = 0b11111;
            glyph[4] = 0b00100;
            break;

        case '*':
            glyph[1] = 0b10101;
            glyph[2] = 0b01110;
            glyph[3] = 0b11111;
            glyph[4] = 0b01110;
            glyph[5] = 0b10101;
            break;

        case '/':
            glyph[0] = 0b00001;
            glyph[1] = 0b00010;
            glyph[2] = 0b00100;
            glyph[3] = 0b01000;
            glyph[4] = 0b10000;
            break;

        case '\\':
            glyph[0] = 0b10000;
            glyph[1] = 0b01000;
            glyph[2] = 0b00100;
            glyph[3] = 0b00010;
            glyph[4] = 0b00001;
            break;

        case '|':
            glyph[0] = 0b00100;
            glyph[1] = 0b00100;
            glyph[2] = 0b00100;
            glyph[3] = 0b00100;
            glyph[4] = 0b00100;
            glyph[5] = 0b00100;
            break;

        case '<':
            glyph[1] = 0b00010;
            glyph[2] = 0b00100;
            glyph[3] = 0b01000;
            glyph[4] = 0b00100;
            glyph[5] = 0b00010;
            break;

        case '>':
            glyph[1] = 0b01000;
            glyph[2] = 0b00100;
            glyph[3] = 0b00010;
            glyph[4] = 0b00100;
            glyph[5] = 0b01000;
            break;

        case '[':
            glyph[0] = 0b01110;
            glyph[1] = 0b01000;
            glyph[2] = 0b01000;
            glyph[3] = 0b01000;
            glyph[4] = 0b01000;
            glyph[5] = 0b01000;
            glyph[6] = 0b01110;
            break;

        case ']':
            glyph[0] = 0b01110;
            glyph[1] = 0b00010;
            glyph[2] = 0b00010;
            glyph[3] = 0b00010;
            glyph[4] = 0b00010;
            glyph[5] = 0b00010;
            glyph[6] = 0b01110;
            break;

        case '(':
            glyph[0] = 0b00010;
            glyph[1] = 0b00100;
            glyph[2] = 0b01000;
            glyph[3] = 0b01000;
            glyph[4] = 0b01000;
            glyph[5] = 0b00100;
            glyph[6] = 0b00010;
            break;

        case ')':
            glyph[0] = 0b01000;
            glyph[1] = 0b00100;
            glyph[2] = 0b00010;
            glyph[3] = 0b00010;
            glyph[4] = 0b00010;
            glyph[5] = 0b00100;
            glyph[6] = 0b01000;
            break;

        case '{':
            glyph[0] = 0b00110;
            glyph[1] = 0b00100;
            glyph[2] = 0b00100;
            glyph[3] = 0b01000;
            glyph[4] = 0b00100;
            glyph[5] = 0b00100;
            glyph[6] = 0b00110;
            break;

        case '}':
            glyph[0] = 0b01100;
            glyph[1] = 0b00100;
            glyph[2] = 0b00100;
            glyph[3] = 0b00010;
            glyph[4] = 0b00100;
            glyph[5] = 0b00100;
            glyph[6] = 0b01100;
            break;

        case '\'':
            glyph[0] = 0b00100;
            glyph[1] = 0b00100;
            break;

        case '"':
            glyph[0] = 0b01010;
            glyph[1] = 0b01010;
            break;

        case '`':
            glyph[0] = 0b01000;
            glyph[1] = 0b00100;
            break;

        case '~':
            glyph[2] = 0b01001;
            glyph[3] = 0b10110;
            break;

        case '^':
            glyph[1] = 0b00100;
            glyph[2] = 0b01010;
            break;

        case '#':
            glyph[1] = 0b01010;
            glyph[2] = 0b11111;
            glyph[3] = 0b01010;
            glyph[4] = 0b11111;
            glyph[5] = 0b01010;
            break;

        case '$':
            glyph[0] = 0b00100;
            glyph[1] = 0b01110;
            glyph[2] = 0b10100;
            glyph[3] = 0b01110;
            glyph[4] = 0b00101;
            glyph[5] = 0b11100;
            glyph[6] = 0b00100;
            break;

        case '%':
            glyph[0] = 0b11001;
            glyph[1] = 0b11010;
            glyph[2] = 0b00100;
            glyph[3] = 0b01000;
            glyph[4] = 0b10110;
            glyph[5] = 0b00110;
            break;

        case '&':
            glyph[1] = 0b01100;
            glyph[2] = 0b10010;
            glyph[3] = 0b01100;
            glyph[4] = 0b10101;
            glyph[5] = 0b10010;
            glyph[6] = 0b01101;
            break;

        case '@':
            glyph[1] = 0b01110;
            glyph[2] = 0b10001;
            glyph[3] = 0b10111;
            glyph[4] = 0b10100;
            glyph[5] = 0b10001;
            glyph[6] = 0b01110;
            break;

        default:
            return;
        }
    }

    for (uint32_t row = 0; row < 7; row++) {
        for (uint32_t col = 0; col < 5; col++) {

            if (!(glyph[row] & (1 << (4 - col))))
                continue;

            for (uint32_t sy = 0; sy < scale; sy++) {
                for (uint32_t sx = 0; sx < scale; sx++) {

                    put_pixel(
                        boot,
                        x + col * scale + sx,
                        y + row * scale + sy,
                        color
                    );
                }
            }
        }
    }
}



 










 
static char scancode_to_ascii(
    uint8_t scancode,
    uint8_t *shift
)
{
    static const char normal[128] = {
        [0x02] = '1',
        [0x03] = '2',
        [0x04] = '3',
        [0x05] = '4',
        [0x06] = '5',
        [0x07] = '6',
        [0x08] = '7',
        [0x09] = '8',
        [0x0A] = '9',
        [0x0B] = '0',

        [0x0C] = '-',
        [0x0D] = '=',
        [0x0E] = '\b',

        [0x0F] = '\t',

        [0x10] = 'q',
        [0x11] = 'w',
        [0x12] = 'e',
        [0x13] = 'r',
        [0x14] = 't',
        [0x15] = 'y',
        [0x16] = 'u',
        [0x17] = 'i',
        [0x18] = 'o',
        [0x19] = 'p',

        [0x1A] = '[',
        [0x1B] = ']',

        [0x1C] = '\n',

        [0x1E] = 'a',
        [0x1F] = 's',
        [0x20] = 'd',
        [0x21] = 'f',
        [0x22] = 'g',
        [0x23] = 'h',
        [0x24] = 'j',
        [0x25] = 'k',
        [0x26] = 'l',

        [0x27] = ';',
        [0x28] = '\'',
        [0x29] = '`',

        [0x2B] = '\\',

        [0x2C] = 'z',
        [0x2D] = 'x',
        [0x2E] = 'c',
        [0x2F] = 'v',
        [0x30] = 'b',
        [0x31] = 'n',
        [0x32] = 'm',

        [0x33] = ',',
        [0x34] = '.',
        [0x35] = '/',

        [0x39] = ' '
    };

    static const char shifted[128] = {
        [0x02] = '!',
        [0x03] = '@',
        [0x04] = '#',
        [0x05] = '$',
        [0x06] = '%',
        [0x07] = '^',
        [0x08] = '&',
        [0x09] = '*',
        [0x0A] = '(',
        [0x0B] = ')',

        [0x0C] = '_',
        [0x0D] = '+',

        [0x10] = 'Q',
        [0x11] = 'W',
        [0x12] = 'E',
        [0x13] = 'R',
        [0x14] = 'T',
        [0x15] = 'Y',
        [0x16] = 'U',
        [0x17] = 'I',
        [0x18] = 'O',
        [0x19] = 'P',

        [0x1A] = '{',
        [0x1B] = '}',

        [0x1E] = 'A',
        [0x1F] = 'S',
        [0x20] = 'D',
        [0x21] = 'F',
        [0x22] = 'G',
        [0x23] = 'H',
        [0x24] = 'J',
        [0x25] = 'K',
        [0x26] = 'L',

        [0x27] = ':',
        [0x28] = '"',
        [0x29] = '~',

        [0x2B] = '|',

        [0x2C] = 'Z',
        [0x2D] = 'X',
        [0x2E] = 'C',
        [0x2F] = 'V',
        [0x30] = 'B',
        [0x31] = 'N',
        [0x32] = 'M',

        [0x33] = '<',
        [0x34] = '>',
        [0x35] = '?',

        [0x39] = ' '
    };

    if (scancode == 0x2A || scancode == 0x36) {
        *shift = 1;
        return 0;
    }

    if (scancode == 0xAA || scancode == 0xB6) {
        *shift = 0;
        return 0;
    }

    if (scancode & 0x80)
        return 0;

    if (scancode == 0x1C)
        return '\n';

    if (scancode == 0x0E)
        return '\b';

    if (scancode == 0x0F)
        return '\t';

    if (scancode >= 128)
        return 0;

    if (*shift)
        return shifted[scancode];

    return normal[scancode];
}



 

#define FONT_SCALE 3
#define CHAR_WIDTH (6 * FONT_SCALE)
#define CHAR_HEIGHT (8 * FONT_SCALE)

#define MAX_COMMAND 128

static char command[MAX_COMMAND];
static uint32_t command_length = 0;

static uint32_t cursor_x;
static uint32_t cursor_y;

#define TERMINAL_X 314
#define TERMINAL_Y 166
#define TERMINAL_BG 0x0028323E

static void terminal_clear(BootInfo *boot)
{
    uint32_t width =
        boot->width > TERMINAL_X + 30
            ? boot->width - TERMINAL_X - 30
            : 0;

    uint32_t height =
        boot->height > TERMINAL_Y + 30
            ? boot->height - TERMINAL_Y - 30
            : 0;

    draw_rect(
        boot,
        TERMINAL_X,
        TERMINAL_Y,
        width,
        height,
        TERMINAL_BG
    );

    cursor_x = TERMINAL_X;
    cursor_y = TERMINAL_Y;
}

static void terminal_newline(BootInfo *boot)
{
    cursor_x = TERMINAL_X;
    cursor_y += CHAR_HEIGHT;

    uint32_t width =
        boot->width > TERMINAL_X + 30
            ? boot->width - TERMINAL_X - 30
            : 0;

    uint32_t height =
        boot->height > TERMINAL_Y + 30
            ? boot->height - TERMINAL_Y - 30
            : 0;

    uint32_t bottom = TERMINAL_Y + height;

    if (cursor_y + CHAR_HEIGHT >= bottom) {

        volatile uint32_t *fb =
            (volatile uint32_t *)boot->framebuffer;

        


 
        for (uint32_t y = TERMINAL_Y;
             y + CHAR_HEIGHT < bottom;
             y++) {

            uint32_t src_y = y + CHAR_HEIGHT;

            for (uint32_t x = 0;
                 x < width;
                 x++) {

                fb[y * boot->pixels_per_scanline
                   + TERMINAL_X + x] =
                    fb[src_y * boot->pixels_per_scanline
                   + TERMINAL_X + x];
            }
        }

        

 
        uint32_t clear_y = bottom - CHAR_HEIGHT;

        for (uint32_t y = clear_y;
             y < bottom;
             y++) {

            for (uint32_t x = 0;
                 x < width;
                 x++) {

                fb[y * boot->pixels_per_scanline
                   + TERMINAL_X + x] =
                    TERMINAL_BG;
            }
        }

        cursor_y = bottom - CHAR_HEIGHT;
    }
}


void terminal_char(
    BootInfo *boot,
    char c
)
{
    if (c == '\n') {
        terminal_newline(boot);
        return;
    }

    if (c == '\b') {

        if (cursor_x >= CHAR_WIDTH) {
            cursor_x -= CHAR_WIDTH;

            for (uint32_t y = 0;
                 y < CHAR_HEIGHT;
                 y++) {

                for (uint32_t x = 0;
                     x < CHAR_WIDTH;
                     x++) {

                    put_pixel(
                        boot,
                        cursor_x + x,
                        cursor_y + y,
                        0x00101820
                    );
                }
            }
        }

        return;
    }

    draw_glyph(
        boot,
        cursor_x,
        cursor_y,
        c,
        FONT_SCALE,
        0x00FFFFFF
    );

    cursor_x += CHAR_WIDTH;

    uint32_t right =
        boot->width > 30
            ? boot->width - 30
            : boot->width;

    if (cursor_x + CHAR_WIDTH >= right)
        terminal_newline(boot);
}


static void terminal_string(
    BootInfo *boot,
    const char *s
)
{
    while (*s)
        terminal_char(boot, *s++);
}


static int command_equals(
    const char *a,
    const char *b
)
{
    while (*a && *b) {
        if (*a != *b)
            return 0;

        a++;
        b++;
    }

    return *a == 0 && *b == 0;
}


static void number_to_string(
    uint64_t value,
    char *buffer
)
{
    char temp[32];
    uint32_t i = 0;
    uint32_t j = 0;

    if (value == 0) {
        buffer[0] = '0';
        buffer[1] = 0;
        return;
    }

    while (value > 0) {
        temp[i++] = '0' + (value % 10);
        value /= 10;
    }

    while (i > 0)
        buffer[j++] = temp[--i];

    buffer[j] = 0;
}


static void terminal_number(
    BootInfo *boot,
    uint64_t value
)
{
    char buffer[32];

    number_to_string(value, buffer);
    terminal_string(boot, buffer);
}


static void fastfetch(
    BootInfo *boot
)
{
    terminal_string(boot, "        _   _ _  ____");
    terminal_newline(boot);

    terminal_string(boot, "       | \\ | (_)/ ___|");
    terminal_newline(boot);

    terminal_string(boot, "       |  \\| | | |");
    terminal_newline(boot);

    terminal_string(boot, "       | |\\  | | |___");
    terminal_newline(boot);

    terminal_string(boot, "       |_| \\_|_|\\____|");
    terminal_newline(boot);

    terminal_newline(boot);

    terminal_string(boot, "OS       NicOS");
    terminal_newline(boot);

    terminal_string(boot, "Kernel   0.1");
    terminal_newline(boot);

    terminal_string(boot, "Arch     x86_64");
    terminal_newline(boot);

    terminal_string(boot, "Boot     UEFI");
    terminal_newline(boot);

    terminal_string(boot, "Screen   ");
    terminal_number(boot, boot->width);
    terminal_string(boot, "x");
    terminal_number(boot, boot->height);
    terminal_newline(boot);

    terminal_string(boot, "ACPI     ");

    if (boot->rsdp)
        terminal_string(boot, "detected");
    else
        terminal_string(boot, "not found");

    terminal_newline(boot);

    terminal_string(boot, "Memory map ");

    if (boot->memory_map) {
        terminal_string(boot, "available");
    } else {
        terminal_string(boot, "not found");
    }

    terminal_newline(boot);
}



static void command_heap(BootInfo *boot)
{
    void *a = kmalloc(64);
    void *b = kmalloc(256);
    void *c = kmalloc(1024);

    if (!a || !b || !c) {
        terminal_string(boot, "heap: allocation failed");
        terminal_newline(boot);
        return;
    }

    terminal_string(boot, "heap: OK");
    terminal_newline(boot);

    terminal_string(boot, "64 bytes  -> ");
    terminal_hex(boot, (uint64_t)(uintptr_t)a);
    terminal_newline(boot);

    terminal_string(boot, "256 bytes -> ");
    terminal_hex(boot, (uint64_t)(uintptr_t)b);
    terminal_newline(boot);

    terminal_string(boot, "1024 bytes -> ");
    terminal_hex(boot, (uint64_t)(uintptr_t)c);
    terminal_newline(boot);
}


#define STORAGE_TEST_SECTOR 65535

static void command_storage(BootInfo *boot)
{
    static uint8_t write_buffer[512] __attribute__((aligned(2)));
    static uint8_t read_buffer[512] __attribute__((aligned(2)));

    for (uint32_t i = 0; i < 512; i++) {
        write_buffer[i] = 0;
        read_buffer[i] = 0;
    }

    const char *message = "NICOS_STORAGE_TEST";

    for (uint32_t i = 0; message[i] && i < 511; i++)
        write_buffer[i] = (uint8_t)message[i];

    terminal_string(boot, "storage: writing test sector...");
    terminal_newline(boot);

    if (!block_write_sector(STORAGE_TEST_SECTOR, write_buffer)) {
        terminal_string(boot, "storage: write failed");
        terminal_newline(boot);
        return;
    }

    terminal_string(boot, "storage: write OK");
    terminal_newline(boot);

    if (!block_read_sector(STORAGE_TEST_SECTOR, read_buffer)) {
        terminal_string(boot, "storage: read failed");
        terminal_newline(boot);
        return;
    }

    terminal_string(boot, "storage: read OK");
    terminal_newline(boot);

    int match = 1;

    for (uint32_t i = 0; i < 18; i++) {
        if (read_buffer[i] != write_buffer[i]) {
            match = 0;
            break;
        }
    }

    if (match) {
        terminal_string(boot, "storage: DATA VERIFIED");
    } else {
        terminal_string(boot, "storage: DATA CORRUPTED");
    }

    terminal_newline(boot);
}


static void command_ls(BootInfo *boot)
{
    char names[8][56];
    uint8_t types[8];

    uint32_t count =
        fs_list(names, types, 8);

    if (count == 0) {
        terminal_string(boot, "(empty)");
        terminal_newline(boot);
        return;
    }

    for (uint32_t i = 0; i < count; i++) {

        terminal_string(boot, names[i]);

        if (types[i] == 2)
            terminal_string(boot, "/");

        terminal_newline(boot);
    }
}

static void command_touch(
    BootInfo *boot,
    const char *name
)
{
    if (!name || !name[0]) {
        terminal_string(
            boot,
            "usage: touch <name>"
        );
        terminal_newline(boot);
        return;
    }

    if (fs_touch(name)) {
        terminal_string(boot, "touch: OK");
    } else {
        terminal_string(boot, "touch: failed");
    }

    terminal_newline(boot);
}

static void command_mkdir(
    BootInfo *boot,
    const char *name
)
{
    if (!name || !name[0]) {
        terminal_string(
            boot,
            "usage: mkdir <name>"
        );
        terminal_newline(boot);
        return;
    }

    int result = fs_mkdir(name);

    if (result == 0) {
        terminal_string(boot, "mkdir: OK");
    } else {
        terminal_string(boot, "mkdir: failed");
        terminal_string(boot, " (");
        terminal_number(boot, (uint64_t)(-result));
        terminal_string(boot, ")");
    }

    terminal_newline(boot);
}

static void command_write(
    BootInfo *boot,
    const char *args
)
{
    if (!args || !args[0]) {
        terminal_string(
            boot,
            "usage: write <name> <text>"
        );
        terminal_newline(boot);
        return;
    }

    while (*args == ' ')
        args++;

    const char *name = args;

    while (*args && *args != ' ')
        args++;

    if (!*args) {
        terminal_string(
            boot,
            "usage: write <name> <text>"
        );
        terminal_newline(boot);
        return;
    }

    char filename[56];
    uint32_t i = 0;

    while (name[i] &&
           name[i] != ' ' &&
           i < 56 - 1) {
        filename[i] = name[i];
        i++;
    }

    filename[i] = 0;

    while (*args == ' ')
        args++;

    uint32_t size = 0;

    while (args[size])
        size++;

    if (size == 0) {
        terminal_string(
            boot,
            "write: empty"
        );
        terminal_newline(boot);
        return;
    }

    if (fs_write_file(
            filename,
            args,
            size)) {

        terminal_string(
            boot,
            "write: OK"
        );
    } else {
        terminal_string(
            boot,
            "write: failed"
        );
    }

    terminal_newline(boot);
}

static void command_cat(
    BootInfo *boot,
    const char *name
)
{
    static char buffer[4096];

    if (!name || !name[0]) {
        terminal_string(
            boot,
            "usage: cat <name>"
        );
        terminal_newline(boot);
        return;
    }

    int result =
        fs_read_file(
            name,
            buffer,
            sizeof(buffer)
        );

    if (result < 0) {
        terminal_string(
            boot,
            "cat: file not found"
        );
        terminal_newline(boot);
        return;
    }

    terminal_string(
        boot,
        buffer
    );

    terminal_newline(boot);
}

extern int fs_file_exists(
    const char *name,
    uint32_t *inode_number
);


static void command_run(BootInfo *boot, const char *name)
{
    int runtime_mode = 0;

    if (name &&
        name[0] == '-' &&
        name[1] == 'r' &&
        name[2] == ' ') {

        runtime_mode = 1;
        name += 3;
    }

    #define USER_PAGE_SIZE 0x1000ULL
    #define USER_STACK_TOP (USER_BASE + USER_SIZE - 16)

    uint32_t inode_number;

    if (!name || !name[0]) {
        terminal_string(boot, "usage: r [-r ]<app>");
        terminal_newline(boot);
        return;
    }

    if (!fs_file_exists(name, &inode_number)) {
        terminal_string(boot, "app not found: ");
        terminal_string(boot, name);
        terminal_newline(boot);
        return;
    }

    


 
    for (uint64_t address = USER_BASE;
         address < USER_BASE + USER_SIZE;
         address += USER_PAGE_SIZE) {

        if (!paging_alloc_user_page(address)) {
            terminal_string(boot, "loader: page allocation failed");
            terminal_newline(boot);
            return;
        }
    }

    


 
    int size = fs_read_file(
        name,
        (char *)USER_BASE,
        (uint32_t)(USER_SIZE - USER_PAGE_SIZE)
    );

    if (size < 0) {
        terminal_string(boot, "loader: cannot read app");
        terminal_newline(boot);
        return;
    }

    terminal_string(boot, "starting ");
    terminal_string(boot, name);
    terminal_string(boot, " (");
    terminal_number(boot, (uint64_t)size);
    terminal_string(boot, " bytes");

    if (runtime_mode) {
        terminal_string(boot, ", runtime");
    }

    terminal_string(boot, ")");
    terminal_newline(boot);

    kernel_debug("[RINGTRACE] R0: before_ring3\r\n");

    extern uint64_t ring3_enter(uint64_t entry, uint64_t stack);

    uint64_t exit_code =
        ring3_enter(USER_BASE, USER_STACK_TOP);

    kernel_debug("[RINGTRACE] R0: after_ring3\r\n");

    terminal_string(boot, "loader: app returned with code ");
    terminal_number(boot, exit_code);
    terminal_newline(boot);
}

static void execute_command(
    BootInfo *boot
)
{
    terminal_newline(boot);

    if (command_equals(command, "help")) {

        terminal_string(boot, "NicOS commands");
        terminal_newline(boot);

        terminal_string(boot, "help");
        terminal_newline(boot);

        terminal_string(boot, "clear");
        terminal_newline(boot);

        terminal_string(boot, "echo");
        terminal_newline(boot);

        terminal_string(boot, "alloc  allocate a physical page");
        terminal_newline(boot);

        terminal_string(boot, "free   free the allocated page");
        terminal_newline(boot);

        terminal_string(boot, "about");
        terminal_newline(boot);

        terminal_string(boot, "fastfetch");
        terminal_newline(boot);

        terminal_string(boot, "mem");
        terminal_newline(boot);

        terminal_string(boot, "heap   test kernel heap");
        terminal_newline(boot);
        
        terminal_string(boot, "ls");
        terminal_newline(boot);

        terminal_string(boot, "touch <name>");
        terminal_newline(boot);

        terminal_string(boot, "mkdir <name>");
        terminal_newline(boot);
        terminal_string(boot, "write <name> <text>");
        terminal_newline(boot);
        terminal_string(boot, "cat <name>");
        terminal_newline(boot);
        terminal_string(boot, "r <app>");
        terminal_newline(boot);
        terminal_newline(boot);
        
        terminal_string(boot, "storage test disk");
        terminal_newline(boot);
    }

    else if (command_equals(command, "clear")) {
        terminal_clear(boot);
    }

    else if (command_equals(command, "mem")) {

        uint64_t total =
            pmm_get_total_memory();

        uint64_t free =
            pmm_get_free_memory();

        terminal_string(boot, "Total memory: ");
        terminal_number(
            boot,
            total / (1024 * 1024)
        );
        terminal_string(boot, " MiB");
        terminal_newline(boot);

        terminal_string(boot, "Free memory: ");
        terminal_number(
            boot,
            free / (1024 * 1024)
        );
        terminal_string(boot, " MiB");
        terminal_newline(boot);

        terminal_string(boot, "Used memory: ");
        terminal_number(
            boot,
            (total > free ? total - free : 0)
            / (1024 * 1024)
        );
        terminal_string(boot, " MiB");
        terminal_newline(boot);
    }

    else if (command_equals(command, "storage")) {

        command_storage(boot);
    }

    else if (command_equals(command, "ls")) {

        command_ls(boot);
    }

    else if (command[0] == 't' &&
             command[1] == 'o' &&
             command[2] == 'u' &&
             command[3] == 'c' &&
             command[4] == 'h' &&
             command[5] == ' ') {

        command_touch(boot, &command[6]);
    }

    else if (command[0] == 'm' &&
             command[1] == 'k' &&
             command[2] == 'd' &&
             command[3] == 'i' &&
             command[4] == 'r' &&
             command[5] == ' ') {

        command_mkdir(boot, &command[6]);
    }

    else if (command_equals(command, "touch")) {

        command_touch(boot, 0);
    }

    else if (command_equals(command, "mkdir")) {

        command_mkdir(boot, 0);
    }

    else if (command[0] == 'w' &&
             command[1] == 'r' &&
             command[2] == 'i' &&
             command[3] == 't' &&
             command[4] == 'e' &&
             command[5] == ' ') {
        command_write(boot, &command[6]);
    }
    else if (command[0] == 'c' &&
             command[1] == 'a' &&
             command[2] == 't' &&
             command[3] == ' ') {
        command_cat(boot, &command[4]);
    }
    else if (command_equals(command, "heap")) {

        command_heap(boot);
    }

    else if (command_equals(command, "fastfetch")) {

        fastfetch(boot);
    }

    else if (command_equals(command, "alloc"))
    {
        command_alloc(boot);
    }
    else if (command_equals(command, "free"))
    {
        command_free(boot);
    }
    else if (command_equals(command, "about")) {

        terminal_string(boot, "NicOS x86_64");
        terminal_newline(boot);

        terminal_string(boot, "UEFI kernel");
        terminal_newline(boot);

        terminal_string(boot, "Boot services exited");
        terminal_newline(boot);
    }

    else if (command[0] == 'r' && command[1] == ' ') {
        command_run(boot, &command[2]);
    }

    else if (command_length > 0) {

        terminal_string(boot, "Unknown command");
        terminal_newline(boot);
    }

    command_length = 0;
    command[0] = 0;

    terminal_string(boot, "> ");
}




 

static uint64_t read_tsc(void)
{
    uint32_t lo;
    uint32_t hi;

    __asm__ volatile (
        "rdtsc"
        : "=a"(lo), "=d"(hi)
    );

    return ((uint64_t)hi << 32) | lo;
}

static void terminal_cursor(
    BootInfo *boot,
    uint8_t visible
)
{
    uint32_t color = visible
        ? 0x00FFFFFF
        : 0x00101820;

    for (uint32_t y = 0; y < CHAR_HEIGHT; y++) {
        for (uint32_t x = 0; x < CHAR_WIDTH - 1; x++) {
            put_pixel(
                boot,
                cursor_x + x,
                cursor_y + y,
                color
            );
        }
    }
}

static void terminal_run(BootInfo *boot)
{
    input_init();

    terminal_clear(boot);

    command_length = 0;

    terminal_string(boot, "NicOS Terminal");
    terminal_newline(boot);

    terminal_string(boot, "> ");

    uint8_t shift = 0;
    uint8_t cursor_visible = 1;

    uint64_t last_blink = read_tsc();

    terminal_cursor(boot, 1);

    for (;;) {

        uint64_t now = read_tsc();

        if (now - last_blink > 100000000ULL) {
            cursor_visible ^= 1;

            terminal_cursor(
                boot,
                cursor_visible
            );

            last_blink = now;
        }

        uint8_t scancode =
            xhci_keyboard_read_scancode();

        if (!scancode)
            continue;

        if (cursor_visible) {
            terminal_cursor(boot, 0);
            cursor_visible = 0;
        }

        char c =
            scancode_to_ascii(
                scancode,
                &shift
            );

        if (!c)
            continue;

        if (c == '\n') {
            execute_command(boot);

            terminal_string(boot, "> ");

            cursor_visible = 1;
            terminal_cursor(boot, 1);
            last_blink = read_tsc();

            continue;
        }

        if (c == '\b') {
            if (command_length > 0) {
                command_length--;
                command[command_length] = 0;

                terminal_char(boot, '\b');
            }

            cursor_visible = 1;
            terminal_cursor(boot, 1);
            last_blink = read_tsc();

            continue;
        }

        if (command_length >= MAX_COMMAND - 1)
            continue;

        command[command_length++] = c;
        command[command_length] = 0;

        terminal_char(boot, c);

        cursor_visible = 1;
        terminal_cursor(boot, 1);
        last_blink = read_tsc();
    }
}





void kmain(BootInfo *boot)
{
    syscall_set_boot_info(boot);

    window_init(boot);
    kernel_debug("[KERNEL] kmain ENTER\r\n");
    kernel_debug("[KERNEL] kmain\r\n");

    kernel_debug("[KERNEL] BEFORE paging_init\r\n");
    paging_init();
    gdt_init();
    idt_init();
    kernel_debug("[KERNEL] AFTER paging_init\r\n");
    kernel_debug("[KERNEL] paging OK\\n");

    kernel_debug("[KERNEL] BEFORE pmm_init\r\n");
    pmm_init(boot);
    kernel_debug("[KERNEL] AFTER pmm_init\r\n");

    if (!gfx_init(boot)) {
        kernel_debug("[GFX] backbuffer allocation FAILED\r\n");
    } else {
        kernel_debug("[GFX] backbuffer OK\r\n");
    }
    kernel_debug("[KERNEL] pmm OK\\n");

    kernel_debug("[KERNEL] BEFORE heap_init\r\n");
    heap_init();
    kernel_debug("[KERNEL] AFTER heap_init\r\n");
    kernel_debug("[KERNEL] heap OK\\n");

    kernel_debug("[KERNEL] BEFORE fs_init\r\n");

    if (!block_init()) {
        kernel_debug("[BLOCK] init FAILED\r\n");
    } else {
        kernel_debug("[BLOCK] init OK\r\n");

        if (block_get_driver() == 1)
            kernel_debug("[BLOCK] DRIVER = AHCI\r\n");
        else if (block_get_driver() == 2)
            kernel_debug("[BLOCK] DRIVER = ATA PIO\r\n");
        else
            kernel_debug("[BLOCK] DRIVER = UNKNOWN\r\n");
    }

    if (!fs_init()) {
        kernel_debug("[FS] init FAILED\r\n");
    } else {
        kernel_debug("[FS] init OK\r\n");

        static const unsigned char test_app[] = {
            0xe8, 0x0b, 0x00, 0x00, 0x00, 0x48, 0x89, 0xc7, 0xe8, 0x93, 0x00, 0x00, 0x00, 0xf4, 0xeb, 0xfd, 0x55, 0x48, 0x89, 0xe5, 0x48, 0x83, 0xec, 0x20, 0x48, 0xb8, 0x63, 0x61, 0x74, 0x69, 0x6f, 0x6e, 0x0a, 0x00, 0x48, 0x89, 0x45, 0xed, 0x48, 0xb8, 0x61, 0x70, 0x70, 0x6c, 0x69, 0x63, 0x61, 0x74, 0x48, 0x89, 0x45, 0xe8, 0x48, 0xb8, 0x4e, 0x69, 0x63, 0x4f, 0x53, 0x20, 0x43, 0x20, 0x48, 0x89, 0x45, 0xe0, 0x48, 0x8d, 0x75, 0xe0, 0xbf, 0x01, 0x00, 0x00, 0x00, 0xba, 0x14, 0x00, 0x00, 0x00, 0xe8, 0x0b, 0x00, 0x00, 0x00, 0xb8, 0x2a, 0x00, 0x00, 0x00, 0x48, 0x83, 0xc4, 0x20, 0x5d, 0xc3, 0x55, 0x48, 0x89, 0xe5, 0x48, 0x83, 0xec, 0x20, 0x89, 0x7d, 0xfc, 0x48, 0x89, 0x75, 0xf0, 0x89, 0x55, 0xec, 0x48, 0x63, 0x7d, 0xfc, 0x48, 0x8b, 0x75, 0xf0, 0x48, 0x63, 0x55, 0xec, 0xb8, 0x01, 0x00, 0x00, 0x00, 0xcd, 0x80, 0x48, 0x89, 0x45, 0xe0, 0x48, 0x8b, 0x45, 0xe0, 0x48, 0x83, 0xc4, 0x20, 0x5d, 0xc3, 0x66, 0x66, 0x66, 0x66, 0x2e, 0x0f, 0x1f, 0x84, 0x00, 0x00, 0x00, 0x00, 0x00, 0x55, 0x48, 0x89, 0xe5, 0x48, 0x83, 0xec, 0x04, 0x89, 0x7d, 0xfc, 0x48, 0x63, 0x7d, 0xfc, 0x31, 0xc0, 0xcd, 0x80, 0xf4, 0xeb, 0xfd, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x4e, 0x69, 0x63, 0x4f, 0x53, 0x20, 0x43, 0x20, 0x61, 0x70, 0x70, 0x6c, 0x69, 0x63, 0x61, 0x74, 0x69, 0x6f, 0x6e, 0x0a, 0x00
        };

        if (fs_write_file(
                "test",
                (const char *)test_app,
                sizeof(test_app))) {
            kernel_debug("[FS] test application created\\r\\n");
        } else {
            kernel_debug("[FS] test application creation FAILED\\r\\n");
        }
    }

    {
        static uint8_t test_sector[512];

        uint32_t test_lbas[] = {
            0, 1, 2, 16, 17, 18
        };

        for (uint32_t n = 0; n < 6; n++) {
            uint32_t lba = test_lbas[n];

            if (!block_read_sector(lba, test_sector)) {
                kernel_debug("[TEST] READ FAILED\r\n");
                continue;
            }

            char hex[] = "0123456789ABCDEF";
            char out[3];

            kernel_debug("[TEST] LBA ");

            char digits[11];
            int pos = 0;
            uint32_t v = lba;

            if (v == 0) {
                digits[pos++] = '0';
            } else {
                char tmp[11];
                int t = 0;

                while (v) {
                    tmp[t++] = '0' + (v % 10);
                    v /= 10;
                }

                while (t)
                    digits[pos++] = tmp[--t];
            }

            digits[pos] = 0;
            kernel_debug(digits);
            kernel_debug(": ");

            for (uint32_t i = 0; i < 32; i++) {
                out[0] = hex[(test_sector[i] >> 4) & 0xF];
                out[1] = hex[test_sector[i] & 0xF];
                out[2] = 0;

                kernel_debug(out);
                kernel_debug(" ");
            }

            kernel_debug("\r\n");
        }
    }

    kernel_debug("[KERNEL] AFTER fs_init\r\n");
    kernel_debug("[KERNEL] fs OK\\n");

    kernel_debug("[KERNEL] AFTER clear_screen\r\n");

    

 

    clear_screen(
        boot,
        0x00101820
    );

    

 
    draw_rect(
        boot,
        0,
        0,
        boot->width,
        48,
        0x00202C3A
    );

    

 
    draw_rect(
        boot,
        16,
        14,
        20,
        20,
        0x004080A0
    );

    

 
    draw_rect(
        boot,
        0,
        48,
        220,
        boot->height > 48 ? boot->height - 48 : 0,
        0x00151D26
    );

    

 
    draw_hline(
        boot,
        220,
        48,
        boot->height > 48 ? 1 : 0,
        0x00406070
    );

    

 
    uint32_t scale = 5;
    uint32_t spacing = 3 * scale;

    uint32_t logo_x =
        24;

    uint32_t logo_y =
        9;

    for (uint32_t c = 0; c < 5; c++) {
        draw_glyph(
            boot,
            logo_x + c * (5 * scale + spacing),
            logo_y,
            "NICOS"[c],
            scale,
            0x00FFFFFF
        );
    }

    

 
    draw_rect(
        boot,
        20,
        80,
        180,
        42,
        0x002A5368
    );

    draw_rect(
        boot,
        20,
        136,
        180,
        42,
        0x00202C38
    );

    draw_rect(
        boot,
        20,
        192,
        180,
        42,
        0x00202C38
    );

    

 
    uint32_t content_x = 250;
    uint32_t content_y = 78;

    uint32_t content_width =
        boot->width > content_x + 30
            ? boot->width - content_x - 30
            : 0;

    uint32_t content_height =
        boot->height > content_y + 30
            ? boot->height - content_y - 30
            : 0;

    draw_rect(
        boot,
        content_x,
        content_y,
        content_width,
        content_height,
        0x0018202C
    );

    

 
    uint32_t win_x = 290;
    uint32_t win_y = 120;

    uint32_t win_width =
        boot->width > win_x + 60
            ? boot->width - win_x - 60
            : 0;

    uint32_t win_height =
        boot->height > win_y + 60
            ? boot->height - win_y - 60
            : 0;

    draw_rect(
        boot,
        win_x,
        win_y,
        win_width,
        win_height,
        0x0028323E
    );

    

 
    draw_rect(
        boot,
        win_x,
        win_y,
        win_width,
        36,
        0x00334A5C
    );

    

 
    if (win_width >= 90) {
        draw_rect(
            boot,
            win_x + win_width - 78,
            win_y + 10,
            12,
            12,
            0x00607080
        );

        draw_rect(
            boot,
            win_x + win_width - 56,
            win_y + 10,
            12,
            12,
            0x00607080
        );

        draw_rect(
            boot,
            win_x + win_width - 34,
            win_y + 10,
            12,
            12,
            0x00A05050
        );
    }

    

 
    if (win_width > 100 && win_height > 100) {
        draw_rect(
            boot,
            win_x + 24,
            win_y + 64,
            win_width - 48,
            18,
            0x00384E60
        );

        draw_rect(
            boot,
            win_x + 24,
            win_y + 96,
            win_width - 110,
            18,
            0x00384E60
        );

        draw_rect(
            boot,
            win_x + 24,
            win_y + 128,
            win_width - 170,
            18,
            0x00384E60
        );
    }

    kernel_debug("[KERNEL] LOGO DONE\r\n");

    kernel_debug("[KERNEL] LOGO DONE\r\n");

    kernel_debug("[KERNEL] LOGO DONE\r\n");


    

 

    PCI_Device xhci;

    kernel_debug("[PCI] FULL SCAN\\r\\n");
    pci_debug_scan();
    kernel_debug("[PCI] FULL SCAN DONE\\r\\n");

    kernel_debug("[PCI] BEFORE scan\\r\\n");

    int pci_ok = pci_find_xhci(&xhci);

    kernel_debug("[PCI] AFTER scan\\r\\n");

    if (pci_ok)
        kernel_debug("[PCI] xHCI FOUND\\r\\n");
    else
        kernel_debug("[PCI] xHCI NOT FOUND\\r\\n");

    kernel_debug("[USB] BEFORE xhci_keyboard_init\\r\\n");

    int usb_ok = xhci_keyboard_init();

    kernel_debug("[USB] AFTER xhci_keyboard_init\\r\\n");

    if (usb_ok)
        kernel_debug("[USB] HID KEYBOARD OK\\r\\n");
    else
        kernel_debug("[USB] HID KEYBOARD FAIL\\r\\n");

    terminal_run(boot);
}
