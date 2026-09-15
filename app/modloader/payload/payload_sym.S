/*
 * Copyright (c) 2026 The LK authors
 *
 * Use of this source code is governed by a MIT-style
 * license that can be found in the LICENSE file or at
 * https://opensource.org/licenses/MIT
 */

/*
 * M3/M6 sym payload: imports modloader_export_add (R_ARM_ABS32 @ 0x10);
 * exports payload_get_magic for modloader_lookup_symbol (returns 55).
 */

#define IMPORT_OFF 0x10

.section .text.entry, "ax", %progbits
.global payload_entry
.type payload_entry, %function
.thumb_func
payload_entry:
    movs    r0, #20
    movs    r1, #24
    mov     ip, lr
    ldr     r3, 1f
    blx     r3
    mov     lr, ip
    bx      lr
1:
    .org    IMPORT_OFF
    .word   modloader_export_add
.size payload_entry, .-payload_entry

.section .text, "ax", %progbits
.global payload_get_magic
.type payload_get_magic, %function
.thumb_func
payload_get_magic:
    movs    r0, #55
    bx      lr
.size payload_get_magic, .-payload_get_magic

.extern modloader_export_add
