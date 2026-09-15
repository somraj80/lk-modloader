/*
 * Copyright (c) 2026 The LK authors
 *
 * Use of this source code is governed by a MIT-style
 * license that can be found in the LICENSE file or at
 * https://opensource.org/licenses/MIT
 */

/*
 * Milestone 1/2 dynamic-module payload. M1 links as ET_EXEC at 0x7f108000; M2
 * links as ET_REL. The modloader maps at PAYLOAD_LOAD_VADDR and calls
 * payload_entry() with the Thumb bit set.
 */

#ifndef PAYLOAD_RETURN_VALUE
#define PAYLOAD_RETURN_VALUE 42
#endif

__attribute__((section(".text.entry"), used))
int payload_entry(void) {
    return PAYLOAD_RETURN_VALUE;
}
