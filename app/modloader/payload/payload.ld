/*
 * Linker script for the Milestone 1 modloader payload (ET_EXEC).
 * Load address must match PAYLOAD_LOAD_VADDR in modloader.c.
 */
OUTPUT_FORMAT("elf32-littlearm", "elf32-littlearm", "elf32-littlearm")
OUTPUT_ARCH(arm)

ENTRY(payload_entry)

SECTIONS
{
    /* 0x7f108000: below KERNEL_BASE and within Thumb BL range of kernel exports */
    . = 0x7f108000;

    .text : {
        KEEP(*(.text.entry))
        *(.text*)
        *(.rodata*)
    }

    /* End of RX region; used by modloader W^X (M4). */
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
    }
}
