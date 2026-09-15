/*
 * Copyright (c) 2026 The LK authors
 *
 * Use of this source code is governed by a MIT-style
 * license that can be found in the LICENSE file or at
 * https://opensource.org/licenses/MIT
 */
#pragma once

#include <lk/compiler.h>
#include <stddef.h>
#include <sys/types.h>

__BEGIN_CDECLS

/* Public modloader API — see docs/ARCHITECTURE.md. */

/* Embedded payload images (build-time blobs). */
typedef enum {
    MODLOADER_IMAGE_EXEC = 0,
    MODLOADER_IMAGE_REL,
    MODLOADER_IMAGE_SYM,
    MODLOADER_IMAGE_IMPORT,
} modloader_image_t;

#define MODLOADER_FLAG_RESOLVE_EXPORTS (1u << 0)

status_t modloader_load_image(modloader_image_t image, uint32_t flags);
status_t modloader_load_uri(const char *uri, uint32_t flags);
int modloader_run(void);
status_t modloader_unload(void);
status_t modloader_wx_verify(void);

status_t modloader_export_register(const char *name, void *addr);
status_t modloader_export_unregister(const char *name);

/* Resolve a symbol exported by the loaded module (M6, dlsym-style). */
void *modloader_lookup_symbol(const char *name);

/* Test export used by the M3/M5 sym payload. */
int modloader_export_add(int a, int b);

__END_CDECLS
