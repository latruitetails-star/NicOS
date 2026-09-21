ENTRY(kmain)

PHDRS
{
    kernel PT_LOAD FLAGS(7);
}

SECTIONS
{
    . = 0x100000;

    .text : ALIGN(16)
    {
        *(.text*)
    } :kernel

    .rodata : ALIGN(16)
    {
        *(.rodata*)
    } :kernel

    .data : ALIGN(16)
    {
        *(.data*)
    } :kernel

    .bss : ALIGN(16)
    {
        *(COMMON)
        *(.bss*)
    } :kernel
}
