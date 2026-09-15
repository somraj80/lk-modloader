/*
 * Copyright (c) 2026 The LK authors
 *
 * Use of this source code is governed by a MIT-style
 * license that can be found in the LICENSE file or at
 * https://opensource.org/licenses/MIT
 */
/* modtest shell command: manual and autorun regression tests. */

#include "modloader_priv.h"

#include <app/modloader.h>
#include "payload_meta.h"

#include <lib/console.h>
#include <stdio.h>
#include <string.h>

static int cmd_modtest(int argc, const console_cmd_args *argv) {
    if (argc < 2) {
        printf("usage: %s load|run|unload|info|wxcheck|loadrel|runrel|loadsym|runsym|"
               "loadimport|runimport|callsym <name>|"
               "loadfilerel|loadfilesym <path>\n",
               argv[0].str);
        return -1;
    }

    if (!strcmp(argv[1].str, "load")) {
        status_t err = modloader_load_image(MODLOADER_IMAGE_EXEC, 0);
        printf("modtest load: %d\n", err);
        return (err < 0) ? -1 : 0;
    }

    if (!strcmp(argv[1].str, "loadrel")) {
        status_t err = modloader_load_image(MODLOADER_IMAGE_REL, 0);
        printf("modtest loadrel: %d\n", err);
        return (err < 0) ? -1 : 0;
    }

    if (!strcmp(argv[1].str, "loadsym")) {
        status_t err = modloader_load_image(MODLOADER_IMAGE_SYM,
                                            MODLOADER_FLAG_RESOLVE_EXPORTS);
        printf("modtest loadsym: %d\n", err);
        return (err < 0) ? -1 : 0;
    }

    if (!strcmp(argv[1].str, "loadimport")) {
        status_t err = modloader_load_image(MODLOADER_IMAGE_IMPORT,
                                            MODLOADER_FLAG_RESOLVE_EXPORTS);
        printf("modtest loadimport: %d\n", err);
        return (err < 0) ? -1 : 0;
    }

    if (!strcmp(argv[1].str, "loadfilerel")) {
        if (argc < 3) {
            printf("usage: %s loadfilerel <path>\n", argv[0].str);
            return -1;
        }
        status_t err = modloader_load_uri(argv[2].str, 0);
        printf("modtest loadfilerel %s: %d\n", argv[2].str, err);
        return (err < 0) ? -1 : 0;
    }

    if (!strcmp(argv[1].str, "loadfilesym")) {
        if (argc < 3) {
            printf("usage: %s loadfilesym <path>\n", argv[0].str);
            return -1;
        }
        status_t err = modloader_load_uri(argv[2].str, MODLOADER_FLAG_RESOLVE_EXPORTS);
        printf("modtest loadfilesym %s: %d\n", argv[2].str, err);
        return (err < 0) ? -1 : 0;
    }

    if (!strcmp(argv[1].str, "run") || !strcmp(argv[1].str, "runrel") ||
        !strcmp(argv[1].str, "runsym") || !strcmp(argv[1].str, "runimport")) {
        int ret = modloader_run();
        if (ret < 0 && ret > -256) {
            printf("modtest run: error %d\n", ret);
            return -1;
        }
        printf("modtest run: payload returned %d\n", ret);
        return 0;
    }

    if (!strcmp(argv[1].str, "callsym")) {
        if (argc < 3) {
            printf("usage: %s callsym <symbol>\n", argv[0].str);
            return -1;
        }
        if (!modloader_mod.loaded || modloader_mod.kind != MODLOADER_REL) {
            printf("modtest callsym: no relocatable module loaded\n");
            return -1;
        }
        void *sym = modloader_lookup_symbol(argv[2].str);
        if (!sym) {
            printf("modtest callsym: symbol '%s' not found\n", argv[2].str);
            return -1;
        }
        typedef int (*payload_sym_fn_t)(void);
        payload_sym_fn_t fn = (payload_sym_fn_t)sym;
        int ret = fn();
        printf("modtest callsym %s: payload returned %d\n", argv[2].str, ret);
        return 0;
    }

    if (!strcmp(argv[1].str, "unload")) {
        status_t err = modloader_unload();
        printf("modtest unload: %d\n", err);
        return (err < 0) ? -1 : 0;
    }

    if (!strcmp(argv[1].str, "wxcheck")) {
        if (!modloader_mod.loaded) {
            printf("modtest wxcheck: module not loaded\n");
            return -1;
        }
        status_t err = modloader_wx_verify();
        if (err < 0) {
            printf("modtest wxcheck: failed (%d)\n", err);
            return -1;
        }
        printf("modtest wxcheck: W^X ok (%zu pages)\n", modloader_mod.size / PAGE_SIZE);
        return 0;
    }

    if (!strcmp(argv[1].str, "info")) {
        printf("exec payload size:  %zu bytes\n", modloader_embedded_size(MODLOADER_IMAGE_EXEC));
        printf("reloc payload size: %zu bytes\n", modloader_embedded_size(MODLOADER_IMAGE_REL));
        printf("sym payload size:   %zu bytes\n", modloader_embedded_size(MODLOADER_IMAGE_SYM));
        printf("import payload size:%zu bytes\n", modloader_embedded_size(MODLOADER_IMAGE_IMPORT));
        printf("exec entry:         0x%x\n", PAYLOAD_ENTRY_VADDR);
        printf("exec rx size:       0x%x\n", PAYLOAD_RX_SIZE);
        printf("load vaddr:         0x%x\n", PAYLOAD_LOAD_VADDR);
        printf("loaded:             %s\n", modloader_mod.loaded ? "yes" : "no");
        if (modloader_mod.loaded) {
            const char *kind_str = "ET_EXEC";
            if (modloader_mod.kind == MODLOADER_REL) {
                kind_str = "ET_REL";
            }
            printf("kind:               %s\n", kind_str);
            printf("w^x:                %s\n", modloader_mod.wx ? "yes" : "no");
            printf("mapping base:       0x%lx\n", modloader_mod.base);
            printf("mapping size:       0x%zx\n", modloader_mod.size);
            printf("entry:              0x%lx\n", modloader_mod.entry);
        }
        return 0;
    }

    printf("unknown subcommand '%s'\n", argv[1].str);
    return -1;
}

STATIC_COMMAND_START
STATIC_COMMAND("modtest",
             "dynamic module loader PoC (load|run|unload|info|wxcheck|loadrel|runrel|"
             "loadsym|runsym|loadimport|runimport|callsym|"
             "loadfilerel|loadfilesym)",
             &cmd_modtest)
STATIC_COMMAND_END(modtest);
