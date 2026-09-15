/*
 * Copyright (c) 2026 The LK authors
 *
 * Use of this source code is governed by a MIT-style
 * license that can be found in the LICENSE file or at
 * https://opensource.org/licenses/MIT
 */
/* Unified load API: embedded images, URIs, run, unload. */

#include "modloader_priv.h"

#include <app/modloader.h>
#include "payload_meta.h"

#include <kernel/vm.h>
#include <lk/err.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

const uint8_t *modloader_embedded_bytes(modloader_image_t image) {
    switch (image) {
        case MODLOADER_IMAGE_EXEC:
            return modloader_payload;
        case MODLOADER_IMAGE_REL:
            return modloader_payload_reloc;
        case MODLOADER_IMAGE_SYM:
            return modloader_payload_sym;
        case MODLOADER_IMAGE_IMPORT:
            return modloader_payload_import;
        default:
            return NULL;
    }
}

size_t modloader_embedded_size(modloader_image_t image) {
    const uint8_t *start = modloader_embedded_bytes(image);
    const uint8_t *end = NULL;

    if (!start) {
        return 0;
    }

    switch (image) {
        case MODLOADER_IMAGE_EXEC:
            end = modloader_payload_end;
            break;
        case MODLOADER_IMAGE_REL:
            end = modloader_payload_reloc_end;
            break;
        case MODLOADER_IMAGE_SYM:
            end = modloader_payload_sym_end;
            break;
        case MODLOADER_IMAGE_IMPORT:
            end = modloader_payload_import_end;
            break;
        default:
            return 0;
    }

    return (size_t)(end - start);
}

static status_t modloader_load_elf_buffer(const uint8_t *bytes, size_t size,
                                          uint32_t flags);

static status_t modloader_load_embedded(modloader_image_t image, uint32_t flags) {
    if (image == MODLOADER_IMAGE_EXEC) {
        size_t size = modloader_embedded_size(image);
        if (size == 0) {
            return ERR_NOT_VALID;
        }
        return modloader_load_exec_blob(modloader_payload, size,
                                        PAYLOAD_ENTRY_VADDR, PAYLOAD_RX_SIZE);
    }

    const uint8_t *bytes = modloader_embedded_bytes(image);
    size_t size = modloader_embedded_size(image);
    if (!bytes || size == 0) {
        return ERR_NOT_VALID;
    }

    return modloader_load_elf_buffer(bytes, size, flags);
}

static status_t modloader_load_elf_buffer(const uint8_t *bytes, size_t size,
                                          uint32_t flags) {
    if (!bytes || size == 0) {
        return ERR_NOT_VALID;
    }

    if (size < sizeof(struct Elf32_Ehdr)) {
        return ERR_NOT_VALID;
    }

    const struct Elf32_Ehdr *ehdr = (const struct Elf32_Ehdr *)bytes;
    if (memcmp(ehdr->e_ident, ELF_MAGIC, 4) != 0 ||
        ehdr->e_ident[EI_CLASS] != ELFCLASS32) {
        return ERR_NOT_VALID;
    }

    return modloader_load_rel_image(bytes, size, flags);
}

static status_t modloader_parse_embed_uri(const char *uri, modloader_image_t *image,
                                          uint32_t *flags) {
    if (!strcmp(uri, "embed:exec")) {
        *image = MODLOADER_IMAGE_EXEC;
        *flags = 0;
        return NO_ERROR;
    }
    if (!strcmp(uri, "embed:rel")) {
        *image = MODLOADER_IMAGE_REL;
        *flags = 0;
        return NO_ERROR;
    }
    if (!strcmp(uri, "embed:sym")) {
        *image = MODLOADER_IMAGE_SYM;
        *flags = MODLOADER_FLAG_RESOLVE_EXPORTS;
        return NO_ERROR;
    }
    if (!strcmp(uri, "embed:import")) {
        *image = MODLOADER_IMAGE_IMPORT;
        *flags = MODLOADER_FLAG_RESOLVE_EXPORTS;
        return NO_ERROR;
    }

    return ERR_NOT_FOUND;
}

status_t modloader_load_image(modloader_image_t image, uint32_t flags) {
    if (image > MODLOADER_IMAGE_IMPORT) {
        return ERR_INVALID_ARGS;
    }

    return modloader_load_embedded(image, flags);
}

status_t modloader_load_uri(const char *uri, uint32_t flags) {
    modloader_image_t image;
    uint32_t embed_flags;
    status_t err;

    if (!uri || !uri[0]) {
        return ERR_INVALID_ARGS;
    }

    if (modloader_parse_embed_uri(uri, &image, &embed_flags) == NO_ERROR) {
        return modloader_load_embedded(image, embed_flags | flags);
    }

    uint8_t *buf = NULL;
    size_t size = 0;

    err = modloader_fetch(uri, &buf, &size);
    if (err != NO_ERROR) {
        return err;
    }

    err = modloader_load_elf_buffer(buf, size, flags);
    free(buf);
    return err;
}

int modloader_run(void) {
    if (!modloader_mod.loaded) {
        return ERR_NOT_READY;
    }

    typedef int (*payload_fn_t)(void);
    payload_fn_t entry = (payload_fn_t)(uintptr_t)modloader_mod.entry;

    return entry();
}

status_t modloader_unload(void) {
    if (!modloader_mod.loaded) {
        return ERR_NOT_FOUND;
    }

    status_t err = vmm_free_region(vmm_get_kernel_aspace(), modloader_mod.base);
    if (err == NO_ERROR) {
        modloader_symbols_clear();
        memset(&modloader_mod, 0, sizeof(modloader_mod));
    }
    return err;
}

status_t modloader_wx_verify(void) {
    if (!modloader_mod.loaded) {
        return ERR_NOT_READY;
    }

    return modloader_verify_wx(modloader_mod.base, modloader_mod.size);
}
