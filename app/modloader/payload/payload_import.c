/*
 * Copyright (c) 2026 The LK authors
 *
 * Use of this source code is governed by a MIT-style
 * license that can be found in the LICENSE file or at
 * https://opensource.org/licenses/MIT
 */

/*
 * M3 variant: import modloader_export_add from C (-fpic -mthumb).
 * Returns modloader_export_add(20, 24) == 44 to match payload_sym.S.
 */

extern int modloader_export_add(int a, int b);

__attribute__((section(".text.entry"), used))
int payload_entry(void) {
    return modloader_export_add(20, 24);
}
