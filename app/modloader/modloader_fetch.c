/*
 * Copyright (c) 2026 The LK authors
 *
 * Use of this source code is governed by a MIT-style
 * license that can be found in the LICENSE file or at
 * https://opensource.org/licenses/MIT
 */
/* Fetch module images via virtio 9p (M5). URIs: file:/path or /path. */

#include "modloader_priv.h"

#include <lib/bio.h>
#include <lib/fs.h>
#include <lk/err.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#define MODLOADER_V9P_MOUNT "/v9p"
#define MODLOADER_V9P_FS "9p"
#define MODLOADER_V9P_BDEV "v9p0"

static bool modloader_v9p_mounted;

static status_t modloader_v9p_ensure_mounted(void) {
    if (modloader_v9p_mounted) {
        return NO_ERROR;
    }

    bdev_t *bdev = bio_open(MODLOADER_V9P_BDEV);
    if (!bdev) {
        return ERR_NOT_FOUND;
    }
    bio_close(bdev);

    status_t err = fs_mount(MODLOADER_V9P_MOUNT, MODLOADER_V9P_FS, MODLOADER_V9P_BDEV,
                            FS_MOUNT_OPTION_NONE);
    if (err != NO_ERROR) {
        return err;
    }

    modloader_v9p_mounted = true;
    return NO_ERROR;
}

static status_t modloader_fetch_file_path(const char *path, uint8_t **out_buf,
                                          size_t *out_size) {
    filehandle *fh = NULL;
    struct file_stat st;
    uint8_t *buf = NULL;
    status_t err;

    if (!path || !out_buf || !out_size) {
        return ERR_INVALID_ARGS;
    }

    *out_buf = NULL;
    *out_size = 0;

    err = modloader_v9p_ensure_mounted();
    if (err != NO_ERROR) {
        return err;
    }

    err = fs_open_file(path, &fh);
    if (err != NO_ERROR) {
        return err;
    }

    err = fs_stat_file(fh, &st);
    if (err != NO_ERROR) {
        goto out;
    }
    if (st.is_dir) {
        err = ERR_NOT_FILE;
        goto out;
    }
    if (st.size == 0 || st.size > MODLOADER_MAX_FETCH_SIZE) {
        err = ERR_TOO_BIG;
        goto out;
    }

    buf = malloc((size_t)st.size);
    if (!buf) {
        err = ERR_NO_MEMORY;
        goto out;
    }

    ssize_t nread = fs_read_file(fh, buf, 0, (size_t)st.size);
    if (nread < 0) {
        err = (status_t)nread;
        goto out;
    }
    if ((size_t)nread != st.size) {
        err = ERR_IO;
        goto out;
    }

    *out_buf = buf;
    *out_size = (size_t)st.size;
    buf = NULL;
    err = NO_ERROR;

out:
    if (buf) {
        free(buf);
    }
    if (fh) {
        fs_close_file(fh);
    }
    return err;
}

static const char *modloader_uri_path(const char *uri) {
    if (!strncmp(uri, "file:", 5)) {
        const char *path = uri + 5;
        if (path[0] == '/') {
            return path;
        }
        return NULL;
    }

    if (uri[0] == '/') {
        return uri;
    }

    return NULL;
}

status_t modloader_fetch(const char *uri, uint8_t **out_buf, size_t *out_size) {
    const char *path;

    if (!uri || !out_buf || !out_size) {
        return ERR_INVALID_ARGS;
    }

    path = modloader_uri_path(uri);
    if (!path) {
        printf("modloader: unsupported fetch URI '%s' (use file:/path or /path)\n", uri);
        return ERR_NOT_SUPPORTED;
    }

    return modloader_fetch_file_path(path, out_buf, out_size);
}
