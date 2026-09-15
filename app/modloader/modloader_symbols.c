/*
 * Copyright (c) 2026 The LK authors
 *
 * Use of this source code is governed by a MIT-style
 * license that can be found in the LICENSE file or at
 * https://opensource.org/licenses/MIT
 */
/* Module export index: kernel lookup of payload symbols (M6, dlsym-style). */

#include "modloader_priv.h"

#include <app/modloader.h>
#include <lk/err.h>
#include <stdio.h>
#include <string.h>

#define MODLOADER_MAX_MODULE_EXPORTS 32
#define MODLOADER_SYMBOL_NAME_MAX 64

struct modloader_module_export {
    char name[MODLOADER_SYMBOL_NAME_MAX];
    vaddr_t addr;
};

static struct modloader_module_export modloader_module_exports[MODLOADER_MAX_MODULE_EXPORTS];
static size_t modloader_module_export_count;

void modloader_symbols_clear(void) {
    memset(modloader_module_exports, 0, sizeof(modloader_module_exports));
    modloader_module_export_count = 0;
}

static bool modloader_skip_module_export(const char *name) {
    if (!name || !name[0]) {
        return true;
    }

    if (name[0] == '$') {
        return true;
    }

    if (!strncmp(name, "__modloader_", 12)) {
        return true;
    }

    return false;
}

static vaddr_t modloader_resolved_sym_addr(const struct Elf32_Sym *sym, vaddr_t load_base,
                                           const vaddr_t *sec_addr, uint16_t shnum) {
    if (sym->st_shndx == SHN_UNDEF) {
        return 0;
    }

    if (sym->st_shndx == SHN_ABS) {
        return sym->st_value;
    }

    if (sym->st_shndx >= SHN_LORESERVE || sym->st_shndx >= shnum) {
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

status_t modloader_symbols_build(const struct Elf32_Sym *syms, uint32_t sym_count,
                                 const char *symstr, vaddr_t load_base,
                                 const vaddr_t *sec_addr, uint16_t shnum) {
    modloader_symbols_clear();

    if (!syms || !symstr || !sec_addr) {
        return ERR_INVALID_ARGS;
    }

    for (uint32_t i = 0; i < sym_count; i++) {
        const struct Elf32_Sym *sym = &syms[i];
        uint8_t bind = ELF32_ST_BIND(sym->st_info);
        uint8_t type = ELF32_ST_TYPE(sym->st_info);
        const char *name = symstr + sym->st_name;

        if (bind != STB_GLOBAL && bind != STB_WEAK) {
            continue;
        }

        if (type != STT_FUNC && type != STT_OBJECT) {
            continue;
        }

        if (sym->st_shndx == SHN_UNDEF) {
            continue;
        }

        if (modloader_skip_module_export(name)) {
            continue;
        }

        if (modloader_module_export_count >= MODLOADER_MAX_MODULE_EXPORTS) {
            printf("modloader: module export table full\n");
            return ERR_NO_MEMORY;
        }

        vaddr_t addr = modloader_resolved_sym_addr(sym, load_base, sec_addr, shnum);
        if (!addr) {
            continue;
        }

        struct modloader_module_export *exp =
            &modloader_module_exports[modloader_module_export_count++];
        strlcpy(exp->name, name, sizeof(exp->name));
        exp->addr = addr;
    }

    return NO_ERROR;
}

void *modloader_lookup_symbol(const char *name) {
    if (!name || !name[0] || !modloader_mod.loaded ||
        modloader_mod.kind != MODLOADER_REL) {
        return NULL;
    }

    for (size_t i = 0; i < modloader_module_export_count; i++) {
        if (!strcmp(modloader_module_exports[i].name, name)) {
            return (void *)(uintptr_t)modloader_module_exports[i].addr;
        }
    }

    return NULL;
}
