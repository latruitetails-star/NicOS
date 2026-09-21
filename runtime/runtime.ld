ENTRY(_start)

SECTIONS
{
    . = 0x0;

    .text ALIGN(16) :
    {
        KEEP(*(.text.entry))
        *(.text .text.*)
    }

    .rodata ALIGN(16) :
    {
        *(.rodata .rodata.*)
    }

    .data ALIGN(16) :
    {
        *(.data .data.*)
    }

    .bss ALIGN(16) :
    {
        *(.bss .bss.*)
        *(COMMON)
    }
}
