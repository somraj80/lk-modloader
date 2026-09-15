/*
 * Copyright (c) 2026 The LK authors
 *
 * Use of this source code is governed by a MIT-style
 * license that can be found in the LICENSE file or at
 * https://opensource.org/licenses/MIT
 */
/* ET_EXEC blob load and ET_REL layout, relocation, entry discovery. */

#include "modloader_priv.h"

#include "payload_meta.h"

#include <arch/ops.h>
#include <kernel/vm.h>
#include <lk/err.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

struct modloader_module modloader_mod;

status_t modloader_load_exec_blob(const uint8_t *blob, size_t blob_size,
                                    vaddr_t entry_vaddr, size_t rx_size) {
    if (!blob || blob_size == 0) {
        return ERR_NOT_VALID;
    }

    if (modloader_mod.loaded) {
        return ERR_ALREADY_EXISTS;
    }

    void *vaddr = (void *)PAYLOAD_LOAD_VADDR;
    size_t map_size = ROUNDUP(blob_size, PAGE_SIZE);

    status_t err = vmm_alloc(vmm_get_kernel_aspace(), "modloader", map_size, &vaddr, 0,
                             VMM_FLAG_VALLOC_SPECIFIC, 0);
    if (err < 0) {
        return err;
    }

    memcpy(vaddr, blob, blob_size);
    arch_sync_cache_range((addr_t)vaddr, blob_size);
    ISB;

    err = modloader_apply_wx_split((vaddr_t)vaddr, map_size, rx_size);
    if (err < 0) {
        vmm_free_region(vmm_get_kernel_aspace(), (vaddr_t)vaddr);
        return err;
    }

    modloader_mod.loaded = true;
    modloader_mod.wx = true;
    modloader_mod.kind = MODLOADER_EXEC;
    modloader_mod.base = (vaddr_t)vaddr;
    modloader_mod.size = map_size;
    modloader_mod.entry = entry_vaddr;
    return NO_ERROR;
}

static vaddr_t modloader_sym_value(const struct Elf32_Sym *sym, vaddr_t load_base,
                                   const vaddr_t *sec_addr, uint16_t shnum) {
    if (sym->st_shndx == SHN_UNDEF) {
        return 0;
    }

    if (sym->st_shndx == SHN_ABS) {
        return sym->st_value;
    }

    if (sym->st_shndx >= SHN_LORESERVE) {
        return 0;
    }

    if (sym->st_shndx >= shnum) {
        return 0;
    }

    if (ELF32_ST_TYPE(sym->st_info) == STT_SECTION) {
        return load_base + sec_addr[sym->st_value];
    }

    vaddr_t addr = load_base + sec_addr[sym->st_shndx] + (sym->st_value & ~1u);
    if (sym->st_value & 1u) {
        addr |= 1;
    }
    return addr;
}

static int32_t modloader_sign_extend(uint32_t val, unsigned bits) {
    uint32_t sign_bit = 1u << (bits - 1);
    return (int32_t)((val ^ sign_bit) - sign_bit);
}

static int32_t modloader_reloc_addend_thumb32_branch(const uint8_t *place) {
    uint16_t hi = *(const uint16_t *)place;
    uint16_t lo = *(const uint16_t *)(place + 2);
    uint16_t imm10 = hi & 0x3ffu;
    uint16_t s = (hi >> 10) & 1u;
    uint16_t imm11 = lo & 0x7ffu;
    uint16_t j2 = (lo >> 11) & 1u;
    uint16_t i2 = (uint16_t)((~(j2 ^ s)) & 1u);
    uint16_t j1 = (lo >> 13) & 1u;
    uint16_t i1 = (uint16_t)((~(j1 ^ s)) & 1u);
    uint32_t val = (s << 24) | (i1 << 23) | (i2 << 22) | (imm10 << 12) | (imm11 << 1);

    return modloader_sign_extend(val, 25);
}

static int32_t modloader_reloc_addend_arm_branch(const uint8_t *place) {
    uint32_t insn = *(const uint32_t *)place;
    bool is_blx = (insn & 0xf0000000u) == 0xf0000000u;
    uint32_t bit_h = is_blx ? ((insn & 0x01000000u) >> 24) : 0;
    uint32_t val = ((insn & 0x00ffffffu) << 2) | (bit_h << 1);

    return modloader_sign_extend(val, 26);
}

static int32_t modloader_reloc_addend_thumb_mov(const uint8_t *place) {
    uint16_t hi = *(const uint16_t *)place;
    uint16_t lo = *(const uint16_t *)(place + 2);
    uint16_t imm8 = lo & 0xffu;
    uint16_t imm3 = (lo >> 12) & 0x7u;
    uint16_t imm4 = hi & 0xfu;
    uint16_t bit_i = (hi >> 10) & 1u;

    return (int32_t)((imm4 << 12) | (bit_i << 11) | (imm3 << 8) | imm8);
}

static int32_t modloader_reloc_addend_arm_mov(const uint8_t *place) {
    uint32_t insn = *(const uint32_t *)place;
    uint32_t imm12 = insn & 0xfffu;
    uint32_t imm4 = (insn >> 16) & 0xfu;

    return (int32_t)((imm4 << 12) | imm12);
}

static void modloader_reloc_encode_thumb32_branch(uint8_t *place, uint32_t result,
                                                  bool use_js) {
    uint16_t *insn = (uint16_t *)place;

    result = (result & 0x01fffffeu) >> 1;
    uint16_t imm10 = (uint16_t)((result >> 11) & 0x3ffu);
    uint16_t s = (uint16_t)((result >> 23) & 1u);
    uint16_t imm11 = (uint16_t)(result & 0x7ffu);
    uint16_t j2 = use_js ? (uint16_t)((result >> 21) & 1u) : s;
    uint16_t i2 = (uint16_t)((~(j2 ^ s)) & 1u);
    uint16_t j1 = use_js ? (uint16_t)((result >> 22) & 1u) : s;
    uint16_t i1 = (uint16_t)((~(j1 ^ s)) & 1u);
    uint16_t res_hi = (s << 10) | imm10;
    uint16_t res_lo = (i1 << 13) | (i2 << 11) | imm11;

    insn[0] = (uint16_t)((insn[0] & 0xf800u) | (res_hi & 0x07ffu));
    insn[1] = (uint16_t)((insn[1] & 0xd000u) | (res_lo & 0x2fffu));
}

static void modloader_reloc_encode_thumb_mov(uint8_t *place, uint32_t value) {
    uint16_t *insn = (uint16_t *)place;
    uint16_t imm8 = (uint16_t)(value & 0xffu);
    uint16_t imm3 = (uint16_t)((value >> 8) & 0x7u);
    uint16_t res_lo = (uint16_t)((imm3 << 12) | imm8);
    uint16_t imm4 = (uint16_t)((value >> 12) & 0xfu);
    uint16_t bit_i = (uint16_t)((value >> 11) & 1u);
    uint16_t res_hi = (uint16_t)((bit_i << 10) | imm4);

    insn[0] = (uint16_t)((insn[0] & 0xfbf0u) | (res_hi & 0x040fu));
    insn[1] = (uint16_t)((insn[1] & 0x8f00u) | (res_lo & 0x70ffu));
}

static void modloader_reloc_encode_arm_mov(uint8_t *place, uint32_t value) {
    uint32_t insn = *(uint32_t *)place;
    uint32_t imm12 = value & 0xfffu;
    uint32_t imm4 = (value >> 12) & 0xfu;

    insn = (insn & 0xfff0f000u) | (imm4 << 16) | imm12;
    *(uint32_t *)place = insn;
}

int32_t modloader_reloc_read_addend(uint32_t type, const uint8_t *place) {
    switch (type) {
        case R_ARM_ABS32:
        case R_ARM_GLOB_DAT:
        case R_ARM_JUMP_SLOT:
        case R_ARM_REL32:
            return *(const int32_t *)place;
        case R_ARM_THM_CALL:
        case R_ARM_THM_JUMP24:
            return modloader_reloc_addend_thumb32_branch(place);
        case R_ARM_CALL:
        case R_ARM_JUMP24:
            return modloader_reloc_addend_arm_branch(place);
        case R_ARM_THM_MOVW_ABS_NC:
        case R_ARM_THM_MOVT_ABS:
            return modloader_reloc_addend_thumb_mov(place);
        case R_ARM_MOVW_ABS_NC:
        case R_ARM_MOVT_ABS:
            return modloader_reloc_addend_arm_mov(place);
        default:
            return 0;
    }
}

bool modloader_sym_is_thumb(const struct Elf32_Sym *sym, vaddr_t sym_val) {
    if (sym_val & 1u) {
        return true;
    }

    uint8_t type = ELF32_ST_TYPE(sym->st_info);
    return type == STT_FUNC;
}

status_t modloader_apply_arm_reloc(uint32_t type, uint8_t *place, vaddr_t sym_val,
                                   int32_t addend, bool sym_thumb, bool is_rela) {
    vaddr_t s = sym_val & ~1u;
    uint32_t t = sym_thumb ? 1u : 0u;
    vaddr_t p = (vaddr_t)place;

    switch (type) {
        case R_ARM_NONE:
            return NO_ERROR;

        case R_ARM_THM_PC12:
            /* LDR literal; section layout is preserved at load time. */
            return NO_ERROR;

        case R_ARM_ABS32:
        case R_ARM_GLOB_DAT:
        case R_ARM_JUMP_SLOT:
        case R_ARM_TARGET2:
            *(uint32_t *)place = (uint32_t)((s + addend) | t);
            return NO_ERROR;

        case R_ARM_RELATIVE:
            if (is_rela) {
                *(uint32_t *)place = (uint32_t)(sym_val + addend);
            } else {
                *(uint32_t *)place = (uint32_t)(sym_val + addend);
            }
            return NO_ERROR;

        case R_ARM_REL32:
            *(uint32_t *)place = (uint32_t)((s + addend) | t) - (uint32_t)place;
            return NO_ERROR;

        case R_ARM_PREL31: {
            int32_t offset = (int32_t)((s + addend) | t) - (int32_t)p;
            uint32_t insn = *(uint32_t *)place;

            insn = (insn & 0x80000000u) | ((uint32_t)offset & 0x7fffffffu);
            *(uint32_t *)place = insn;
            return NO_ERROR;
        }

        case R_ARM_CALL:
        case R_ARM_JUMP24: {
            uint32_t result = (uint32_t)(((s + addend) | t) - p);
            uint32_t insn = *(uint32_t *)place;
            uint32_t imm24 = (result & 0x03fffffcu) >> 2;

            insn = (insn & 0xff000000u) | (imm24 & 0x00ffffffu);
            if (sym_thumb && type == R_ARM_CALL) {
                uint32_t bit_h = (result & 0x2u) >> 1;

                insn = (insn & 0x00ffffffu) | ((0xfa | bit_h) << 24);
            }
            *(uint32_t *)place = insn;
            return NO_ERROR;
        }

        case R_ARM_THM_CALL:
        case R_ARM_THM_JUMP24: {
            if (!sym_thumb && type == R_ARM_THM_CALL) {
                p &= ~3u;
            }

            uint32_t result = (uint32_t)(((s + addend) | t) - p);

            modloader_reloc_encode_thumb32_branch(place, result, true);
            if (!sym_thumb && type == R_ARM_THM_CALL) {
                uint16_t *insn = (uint16_t *)place;

                insn[1] &= ~0x1000u;
            }
            return NO_ERROR;
        }

        case R_ARM_MOVW_ABS_NC:
        case R_ARM_THM_MOVW_ABS_NC: {
            uint32_t value = (uint32_t)((s + addend) | t) & 0xffffu;

            if (type == R_ARM_THM_MOVW_ABS_NC) {
                modloader_reloc_encode_thumb_mov(place, value);
            } else {
                modloader_reloc_encode_arm_mov(place, value);
            }
            return NO_ERROR;
        }

        case R_ARM_MOVT_ABS:
        case R_ARM_THM_MOVT_ABS: {
            uint32_t value = (uint32_t)(s + addend);
            uint32_t arg = (value & 0xffff0000u) >> 16;

            if (type == R_ARM_THM_MOVT_ABS) {
                modloader_reloc_encode_thumb_mov(place, arg);
            } else {
                modloader_reloc_encode_arm_mov(place, arg);
            }
            return NO_ERROR;
        }

        default:
            printf("modloader: unsupported relocation type %u at %p\n", type, place);
            return ERR_NOT_SUPPORTED;
    }
}

static status_t modloader_find_entry(const struct Elf32_Shdr *shdrs, uint16_t shnum,
                                     const struct Elf32_Sym *syms, uint32_t sym_count,
                                     const char *strtab, const vaddr_t *sec_addr,
                                     vaddr_t load_base, vaddr_t *entry) {
    for (uint32_t i = 0; i < sym_count; i++) {
        const struct Elf32_Sym *sym = &syms[i];

        if (ELF32_ST_TYPE(sym->st_info) != STT_FUNC) {
            continue;
        }

        if (ELF32_ST_BIND(sym->st_info) != STB_GLOBAL &&
            ELF32_ST_BIND(sym->st_info) != STB_WEAK) {
            continue;
        }

        if (strcmp(strtab + sym->st_name, "payload_entry") != 0) {
            continue;
        }

        if (sym->st_shndx == SHN_UNDEF || sym->st_shndx >= shnum) {
            return ERR_NOT_FOUND;
        }

        vaddr_t addr = load_base + sec_addr[sym->st_shndx] + (sym->st_value & ~1u);
        if (sym->st_value & 1u) {
            addr |= 1;
        }
        *entry = addr;
        return NO_ERROR;
    }

    return ERR_NOT_FOUND;
}

status_t modloader_load_rel_image(const uint8_t *image, size_t image_size,
                                  uint32_t flags) {
    (void)flags;
    if (image_size < sizeof(struct Elf32_Ehdr)) {
        return ERR_NOT_VALID;
    }

    if (modloader_mod.loaded) {
        return ERR_ALREADY_EXISTS;
    }

    modloader_symbols_clear();

    const struct Elf32_Ehdr *ehdr = (const struct Elf32_Ehdr *)image;
    if (memcmp(ehdr->e_ident, ELF_MAGIC, 4) != 0 ||
        ehdr->e_ident[EI_CLASS] != ELFCLASS32 ||
        ehdr->e_ident[EI_DATA] != ELFDATA2LSB ||
        ehdr->e_type != ET_REL || ehdr->e_machine != EM_ARM) {
        return ERR_NOT_VALID;
    }

    if (ehdr->e_shentsize != sizeof(struct Elf32_Shdr) || ehdr->e_shnum == 0) {
        return ERR_NOT_VALID;
    }

    if ((size_t)ehdr->e_shoff + (size_t)ehdr->e_shentsize * ehdr->e_shnum > image_size) {
        return ERR_NOT_VALID;
    }

    const struct Elf32_Shdr *shdrs =
        (const struct Elf32_Shdr *)(image + ehdr->e_shoff);
    const struct Elf32_Shdr *shstrtab = &shdrs[ehdr->e_shstrndx];
    if ((size_t)shstrtab->sh_offset + shstrtab->sh_size > image_size) {
        return ERR_NOT_VALID;
    }

    const char *shstr = (const char *)(image + shstrtab->sh_offset);
    vaddr_t sec_addr[ehdr->e_shnum];
    memset(sec_addr, 0, sizeof(sec_addr));

    size_t total_size = 0;
    for (uint16_t i = 1; i < ehdr->e_shnum; i++) {
        if (!(shdrs[i].sh_flags & SHF_ALLOC)) {
            continue;
        }

        if (shdrs[i].sh_addralign > 1) {
            total_size = ROUNDUP(total_size, shdrs[i].sh_addralign);
        }

        sec_addr[i] = total_size;
        total_size += shdrs[i].sh_size;
    }

    if (total_size == 0) {
        return ERR_NOT_VALID;
    }

    void *vaddr = (void *)PAYLOAD_LOAD_VADDR;
    size_t map_size = ROUNDUP(total_size, PAGE_SIZE);
    status_t err = vmm_alloc(vmm_get_kernel_aspace(), "modloader", map_size, &vaddr, 0,
                             VMM_FLAG_VALLOC_SPECIFIC, 0);
    if (err < 0) {
        return err;
    }

    uint8_t *load_base = (uint8_t *)vaddr;
    for (uint16_t i = 1; i < ehdr->e_shnum; i++) {
        if (!(shdrs[i].sh_flags & SHF_ALLOC)) {
            continue;
        }

        uint8_t *dest = load_base + sec_addr[i];
        if (shdrs[i].sh_type == SHT_PROGBITS) {
            if ((size_t)shdrs[i].sh_offset + shdrs[i].sh_size > image_size) {
                vmm_free_region(vmm_get_kernel_aspace(), (vaddr_t)load_base);
                return ERR_NOT_VALID;
            }
            memcpy(dest, image + shdrs[i].sh_offset, shdrs[i].sh_size);
        } else if (shdrs[i].sh_type == SHT_NOBITS) {
            memset(dest, 0, shdrs[i].sh_size);
        }
    }

    const struct Elf32_Sym *syms = NULL;
    uint32_t sym_count = 0;
    const char *symstr = NULL;

    for (uint16_t i = 1; i < ehdr->e_shnum; i++) {
        if (shdrs[i].sh_type != SHT_SYMTAB) {
            continue;
        }

        if ((size_t)shdrs[i].sh_offset + shdrs[i].sh_size > image_size) {
            vmm_free_region(vmm_get_kernel_aspace(), (vaddr_t)load_base);
            return ERR_NOT_VALID;
        }

        syms = (const struct Elf32_Sym *)(image + shdrs[i].sh_offset);
        sym_count = shdrs[i].sh_size / sizeof(struct Elf32_Sym);

        const struct Elf32_Shdr *strtab = &shdrs[shdrs[i].sh_link];
        if ((size_t)strtab->sh_offset + strtab->sh_size > image_size) {
            vmm_free_region(vmm_get_kernel_aspace(), (vaddr_t)load_base);
            return ERR_NOT_VALID;
        }
        symstr = (const char *)(image + strtab->sh_offset);
        break;
    }

    vaddr_t *sym_resolved = NULL;
    if (syms && sym_count > 0) {
        sym_resolved = calloc(sym_count, sizeof(vaddr_t));
        if (!sym_resolved) {
            vmm_free_region(vmm_get_kernel_aspace(), (vaddr_t)load_base);
            return ERR_NO_MEMORY;
        }

        status_t resolve_err = modloader_resolve_externals(syms, sym_count, symstr, sym_resolved);
        if (resolve_err < 0) {
            free(sym_resolved);
            vmm_free_region(vmm_get_kernel_aspace(), (vaddr_t)load_base);
            return resolve_err;
        }
    }

    for (uint16_t i = 1; i < ehdr->e_shnum; i++) {
        if (shdrs[i].sh_type != SHT_REL && shdrs[i].sh_type != SHT_RELA) {
            continue;
        }

        if (shdrs[i].sh_info >= ehdr->e_shnum) {
            vmm_free_region(vmm_get_kernel_aspace(), (vaddr_t)load_base);
            return ERR_NOT_VALID;
        }

        uint16_t target_idx = shdrs[i].sh_info;
        if (!(shdrs[target_idx].sh_flags & SHF_ALLOC)) {
            continue;
        }

        if ((size_t)shdrs[i].sh_offset + shdrs[i].sh_size > image_size) {
            vmm_free_region(vmm_get_kernel_aspace(), (vaddr_t)load_base);
            return ERR_NOT_VALID;
        }

        size_t count = shdrs[i].sh_size / shdrs[i].sh_entsize;
        for (size_t r = 0; r < count; r++) {
            uint32_t sym_idx;
            uint32_t type;
            uint32_t offset;
            int32_t addend = 0;
            bool is_rela = false;

            if (shdrs[i].sh_type == SHT_REL) {
                const struct Elf32_Rel *rel =
                    (const struct Elf32_Rel *)(image + shdrs[i].sh_offset +
                                               r * shdrs[i].sh_entsize);
                sym_idx = ELF32_R_SYM(rel->r_info);
                type = ELF32_R_TYPE(rel->r_info);
                offset = rel->r_offset;
                switch (type) {
                case R_ARM_ABS32:
                case R_ARM_GLOB_DAT:
                case R_ARM_JUMP_SLOT:
                case R_ARM_REL32:
                    addend = *(int32_t *)(load_base + sec_addr[target_idx] + offset);
                    break;
                case R_ARM_THM_CALL:
                case R_ARM_THM_JUMP24:
                case R_ARM_CALL:
                case R_ARM_JUMP24:
                case R_ARM_THM_MOVW_ABS_NC:
                case R_ARM_THM_MOVT_ABS:
                case R_ARM_MOVW_ABS_NC:
                case R_ARM_MOVT_ABS:
                    addend = modloader_reloc_read_addend(
                        type, load_base + sec_addr[target_idx] + offset);
                    break;
                default:
                    addend = 0;
                    break;
                }
            } else {
                const struct Elf32_Rela *rela =
                    (const struct Elf32_Rela *)(image + shdrs[i].sh_offset +
                                                r * shdrs[i].sh_entsize);
                sym_idx = ELF32_R_SYM(rela->r_info);
                type = ELF32_R_TYPE(rela->r_info);
                offset = rela->r_offset;
                addend = rela->r_addend;
                is_rela = true;
            }

            vaddr_t sym_val = 0;
            if (syms && sym_idx < sym_count) {
                if (syms[sym_idx].st_shndx == SHN_UNDEF && sym_resolved &&
                    sym_resolved[sym_idx] != 0) {
                    sym_val = sym_resolved[sym_idx];
                } else {
                    sym_val = modloader_sym_value(&syms[sym_idx], (vaddr_t)load_base, sec_addr,
                                                  ehdr->e_shnum);
                }
            } else if (type != R_ARM_RELATIVE) {
                free(sym_resolved);
                vmm_free_region(vmm_get_kernel_aspace(), (vaddr_t)load_base);
                return ERR_NOT_VALID;
            }

            if (type == R_ARM_RELATIVE) {
                sym_val = (vaddr_t)load_base;
            }

            bool sym_thumb = false;
            if (syms && sym_idx < sym_count) {
                sym_thumb = modloader_sym_is_thumb(&syms[sym_idx], sym_val);
            }

            uint8_t *place = load_base + sec_addr[target_idx] + offset;
            err = modloader_apply_arm_reloc(type, place, sym_val, addend, sym_thumb, is_rela);
            if (err < 0) {
                printf("modloader: reloc %s[%u] type %u failed: %d\n",
                       shstr + shdrs[target_idx].sh_name, (unsigned)offset, type, err);
                free(sym_resolved);
                vmm_free_region(vmm_get_kernel_aspace(), (vaddr_t)load_base);
                return err;
            }
        }
    }

    free(sym_resolved);

    vaddr_t entry = 0;
    if (!syms || !symstr) {
        vmm_free_region(vmm_get_kernel_aspace(), (vaddr_t)load_base);
        return ERR_NOT_FOUND;
    }

    err = modloader_find_entry(shdrs, ehdr->e_shnum, syms, sym_count, symstr, sec_addr,
                               (vaddr_t)load_base, &entry);
    if (err < 0) {
        vmm_free_region(vmm_get_kernel_aspace(), (vaddr_t)load_base);
        return err;
    }

    err = modloader_apply_wx_sections((vaddr_t)load_base, map_size, shdrs, ehdr->e_shnum,
                                      sec_addr);
    if (err < 0) {
        vmm_free_region(vmm_get_kernel_aspace(), (vaddr_t)load_base);
        return err;
    }

    arch_sync_cache_range((addr_t)load_base, total_size);
    ISB;

    err = modloader_symbols_build(syms, sym_count, symstr, (vaddr_t)load_base, sec_addr,
                                  ehdr->e_shnum);
    if (err < 0) {
        vmm_free_region(vmm_get_kernel_aspace(), (vaddr_t)load_base);
        return err;
    }

    modloader_mod.loaded = true;
    modloader_mod.wx = true;
    modloader_mod.kind = MODLOADER_REL;
    modloader_mod.base = (vaddr_t)load_base;
    modloader_mod.size = map_size;
    modloader_mod.entry = entry;
    return NO_ERROR;
}
