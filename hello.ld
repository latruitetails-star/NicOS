ENTRY(_start)

SECTIONS
{
    . = 0;

    .text : ALIGN(16)
    {
        *(.text*)
    }

    .rodata : ALIGN(16)
    {
        *(.rodata*)
    }
}
