/*
 * Copyright (c) 2026 The LK authors
 *
 * Use of this source code is governed by a MIT-style
 * license that can be found in the LICENSE file or at
 * https://opensource.org/licenses/MIT
 */
#pragma once

#include <app/modloader.h>
#include <arch/mmu.h>
#include <kernel/vm.h>
#include <lib/elf_defines.h>
#include <stdbool.h>
#include <stddef.h>
#include <sys/types.h>

#define PAYLOAD_LOAD_VADDR 0x7f108000u
#define MODLOADER_MAX_FETCH_SIZE (256u * 1024u)

#define MODLOADER_MMU_RX  ARCH_MMU_FLAG_PERM_RO
#define MODLOADER_MMU_RW  ARCH_MMU_FLAG_PERM_NO_EXECUTE
#define MODLOADER_MMU_RO  (ARCH_MMU_FLAG_PERM_RO | ARCH_MMU_FLAG_PERM_NO_EXECUTE)

#define R_ARM_THM_PC12 54  /* ATfE/lld R_ARM_THM_PC12 (LDR literal) */
#define R_ARM_CALL 28
#define R_ARM_JUMP24 29
#define R_ARM_THM_JUMP24 30
#define R_ARM_MOVW_ABS_NC 43
#define R_ARM_MOVT_ABS 44
#define R_ARM_THM_MOVW_ABS_NC 47
#define R_ARM_THM_MOVT_ABS 48
#define R_ARM_PREL31 42
#define R_ARM_THM_CALL R_ARM_THM_PC22
#define R_ARM_GLOB_DAT 21
#define R_ARM_JUMP_SLOT 22
#define R_ARM_TARGET2 41

enum modloader_kind {
    MODLOADER_NONE = 0,
    MODLOADER_EXEC,
    MODLOADER_REL,
};

struct modloader_module {
    bool loaded;
    bool wx;
    enum modloader_kind kind;
    vaddr_t base;
    size_t size;
    vaddr_t entry;
};

extern struct modloader_module modloader_mod;

extern const uint8_t modloader_payload[];
extern const uint8_t modloader_payload_end[];
extern const uint8_t modloader_payload_reloc[];
extern const uint8_t modloader_payload_reloc_end[];
extern const uint8_t modloader_payload_sym[];
extern const uint8_t modloader_payload_sym_end[];
extern const uint8_t modloader_payload_import[];
extern const uint8_t modloader_payload_import_end[];

size_t modloader_embedded_size(modloader_image_t image);
const uint8_t *modloader_embedded_bytes(modloader_image_t image);

status_t modloader_load_exec_blob(const uint8_t *blob, size_t blob_size,
                                    vaddr_t entry_vaddr, size_t rx_size);
status_t modloader_load_rel_image(const uint8_t *image, size_t image_size,
                                  uint32_t flags);

status_t modloader_fetch(const char *uri, uint8_t **out_buf, size_t *out_size);

int32_t modloader_reloc_read_addend(uint32_t type, const uint8_t *place);
bool modloader_sym_is_thumb(const struct Elf32_Sym *sym, vaddr_t sym_val);
status_t modloader_apply_arm_reloc(uint32_t type, uint8_t *place, vaddr_t sym_val,
                                   int32_t addend, bool sym_thumb, bool is_rela);

status_t modloader_apply_wx_split(vaddr_t base, size_t map_size, size_t rx_size);
status_t modloader_apply_wx_sections(vaddr_t base, size_t map_size,
                                     const struct Elf32_Shdr *shdrs,
                                     uint16_t shnum, const vaddr_t *sec_addr);
status_t modloader_remap_page(vaddr_t va, uint arch_flags);
status_t modloader_verify_wx(vaddr_t base, size_t map_size);

vaddr_t modloader_lookup_export(const char *name, bool is_func);
status_t modloader_resolve_externals(const struct Elf32_Sym *syms, uint32_t sym_count,
                                     const char *symstr, vaddr_t *resolved);

void modloader_symbols_clear(void);
status_t modloader_symbols_build(const struct Elf32_Sym *syms, uint32_t sym_count,
                                 const char *symstr, vaddr_t load_base,
                                 const vaddr_t *sec_addr, uint16_t shnum);
