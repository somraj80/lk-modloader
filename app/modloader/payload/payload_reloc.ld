/*
 * Linker script for Milestone 2 modloader payload (ET_REL).
 * No fixed load address; section layout is resolved at runtime by the loader.
 */
OUTPUT_FORMAT("elf32-littlearm", "elf32-littlearm", "elf32-littlearm")
OUTPUT_ARCH(arm)

SECTIONS
{
    .text : {
        KEEP(*(.text.entry))
        *(.text*)
        *(.rodata*)
    }

    __modloader_rx_end = .;

    .data : {
        *(.data*)
    }

    .bss : {
        *(.bss*)
        *(COMMON)
    } = 0

    /DISCARD/ : {
        *(.ARM.exidx*)
        *(.comment)
        *(.note*)
        *(.llvm_addrsig)
        *(.ARM.attributes)
    }
}
