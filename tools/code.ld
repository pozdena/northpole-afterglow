/* One flat, position-independent blob: the entry stub at byte 0, then code and
   constants. A code resource has nowhere to put writable data, so .data and .bss
   must come out empty (the Makefile checks). */
ENTRY(_start)
SECTIONS
{
    . = BASE;
    .text : {
        *(.entry)
        *(.text .text.*)
        *(.rodata .rodata.*)
    }
    .data : { *(.data .data.*) }
    .bss : { *(.bss .bss.*) *(COMMON) }
    /DISCARD/ : { *(.comment) *(.note*) *(.eh_frame*) }
}
