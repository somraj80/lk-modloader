/*
 * Copyright (c) 2026 The LK authors
 *
 * Use of this source code is governed by a MIT-style
 * license that can be found in the LICENSE file or at
 * https://opensource.org/licenses/MIT
 */
/* Kernel export table: symbols payload ET_REL objects may import (M3). */

#include "modloader_priv.h"

#include <app/modloader.h>
#include <lk/err.h>
#include <stdio.h>
#include <string.h>

#define MODLOADER_MAX_DYNAMIC_EXPORTS 16
#define MODLOADER_EXPORT_NAME_MAX 64

struct modloader_dynexport {
    bool used;
    char name[MODLOADER_EXPORT_NAME_MAX];
    void *addr;
};

static struct modloader_dynexport modloader_dyn_exports[MODLOADER_MAX_DYNAMIC_EXPORTS];

int modloader_export_add(int a, int b) {
    return a + b;
}

static const struct {
    const char *name;
    void *addr;
} modloader_builtin_exports[] = {
    { "modloader_export_add", (void *)modloader_export_add },
};

static vaddr_t modloader_export_vaddr(void *addr, bool is_func) {
    vaddr_t vaddr = (vaddr_t)(uintptr_t)addr;
#if defined(ARCH_ARM) || defined(__thumb__)
    if (is_func) {
        vaddr |= 1;
    }
#endif
    return vaddr;
}

status_t modloader_export_register(const char *name, void *addr) {
    if (!name || !name[0] || !addr) {
        return ERR_INVALID_ARGS;
    }

    if (strlen(name) >= MODLOADER_EXPORT_NAME_MAX) {
        return ERR_TOO_BIG;
    }

    for (size_t i = 0; i < sizeof(modloader_builtin_exports) /
                            sizeof(modloader_builtin_exports[0]);
         i++) {
        if (!strcmp(modloader_builtin_exports[i].name, name)) {
            return ERR_ALREADY_EXISTS;
        }
    }

    for (size_t i = 0; i < MODLOADER_MAX_DYNAMIC_EXPORTS; i++) {
        if (modloader_dyn_exports[i].used && !strcmp(modloader_dyn_exports[i].name, name)) {
            modloader_dyn_exports[i].addr = addr;
            return NO_ERROR;
        }
    }

    for (size_t i = 0; i < MODLOADER_MAX_DYNAMIC_EXPORTS; i++) {
        if (!modloader_dyn_exports[i].used) {
            modloader_dyn_exports[i].used = true;
            strlcpy(modloader_dyn_exports[i].name, name, sizeof(modloader_dyn_exports[i].name));
            modloader_dyn_exports[i].addr = addr;
            return NO_ERROR;
        }
    }

    return ERR_NO_MEMORY;
}

status_t modloader_export_unregister(const char *name) {
    if (!name || !name[0]) {
        return ERR_INVALID_ARGS;
    }

    for (size_t i = 0; i < MODLOADER_MAX_DYNAMIC_EXPORTS; i++) {
        if (modloader_dyn_exports[i].used && !strcmp(modloader_dyn_exports[i].name, name)) {
            memset(&modloader_dyn_exports[i], 0, sizeof(modloader_dyn_exports[i]));
            return NO_ERROR;
        }
    }

    return ERR_NOT_FOUND;
}

vaddr_t modloader_lookup_export(const char *name, bool is_func) {
    for (size_t i = 0; i < sizeof(modloader_builtin_exports) /
                            sizeof(modloader_builtin_exports[0]);
         i++) {
        if (!strcmp(modloader_builtin_exports[i].name, name)) {
            return modloader_export_vaddr(modloader_builtin_exports[i].addr, is_func);
        }
    }

    for (size_t i = 0; i < MODLOADER_MAX_DYNAMIC_EXPORTS; i++) {
        if (modloader_dyn_exports[i].used && !strcmp(modloader_dyn_exports[i].name, name)) {
            return modloader_export_vaddr(modloader_dyn_exports[i].addr, is_func);
        }
    }

    return 0;
}

status_t modloader_resolve_externals(const struct Elf32_Sym *syms, uint32_t sym_count,
                                     const char *symstr, vaddr_t *resolved) {
    memset(resolved, 0, sym_count * sizeof(vaddr_t));

    for (uint32_t i = 0; i < sym_count; i++) {
        const struct Elf32_Sym *sym = &syms[i];

        if (sym->st_shndx != SHN_UNDEF) {
            continue;
        }

        uint8_t bind = ELF32_ST_BIND(sym->st_info);
        if (bind != STB_GLOBAL && bind != STB_WEAK) {
            continue;
        }

        const char *name = symstr + sym->st_name;
        if (name[0] == '\0') {
            continue;
        }

        uint8_t type = ELF32_ST_TYPE(sym->st_info);
        bool is_func = (type == STT_FUNC);
        if (type == STT_NOTYPE) {
            /* clang ET_REL imports are often NOTYPE; all kernel exports are functions */
            is_func = modloader_lookup_export(name, false) != 0;
        }
        vaddr_t addr = modloader_lookup_export(name, is_func);
        if (!addr) {
            if (bind == STB_WEAK) {
                continue;
            }
            printf("modloader: undefined symbol '%s' not in export table\n", name);
            return ERR_NOT_FOUND;
        }

        resolved[i] = addr;
    }

    return NO_ERROR;
}
