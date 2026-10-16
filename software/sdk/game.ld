/*
 * Linker script for DualSlide game files.
 *
 * A game is linked at address zero as one block, then moved anywhere in RAM
 * by the firmware, which adds the load address to every absolute address the
 * relocations (kept with --emit-relocs) point at.
 *
 *   load image   code, read-only data, initialised data: copied from the file
 *   zero area    .bss: cleared by the firmware after the image
 *
 * There is no start-up code: the firmware calls DualSlideGame_Entry() after
 * copying the image and clearing the zero area.
 */

ENTRY(DualSlideGame_Entry)

SECTIONS
{
    . = 0;

    .text :
    {
        *(.text .text.*)
        *(.glue_7 .glue_7t .vfp11_veneer .v4_bx)
    }

    .rodata : ALIGN(8)
    {
        *(.rodata .rodata.*)
    }

    .ARM.extab : ALIGN(4)
    {
        *(.ARM.extab* .gnu.linkonce.armextab.*)
    }

    .ARM.exidx : ALIGN(4)
    {
        *(.ARM.exidx*)
    }

    .init_array : ALIGN(4)
    {
        KEEP(*(.preinit_array .init_array .init_array.*))
    }

    .data : ALIGN(8)
    {
        *(.data .data.*)
        . = ALIGN(8);
    }

    .bss (NOLOAD) : ALIGN(8)
    {
        *(.bss .bss.*)
        *(COMMON)
        . = ALIGN(8);
    }

    /DISCARD/ :
    {
        *(.comment)
        *(.ARM.attributes)
        *(.fini_array .fini_array.*)
    }
}
