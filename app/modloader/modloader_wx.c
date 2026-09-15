/*
 * Copyright (c) 2026 The LK authors
 *
 * Use of this source code is governed by a MIT-style
 * license that can be found in the LICENSE file or at
 * https://opensource.org/licenses/MIT
 */
/* W^X page remap after load and verification (M4). */

#include "modloader_priv.h"

#include <arch/mmu.h>
#include <arch/ops.h>
#include <kernel/vm.h>
#include <lk/err.h>
#include <stdio.h>

status_t modloader_remap_page(vaddr_t va, uint arch_flags) {
    vmm_aspace_t *aspace = vmm_get_kernel_aspace();
    paddr_t pa;
    uint existing = 0;
    status_t err = arch_mmu_query(&aspace->arch_aspace, va, &pa, &existing);

    if (err < 0) {
        return err;
    }

    return arch_mmu_map(&aspace->arch_aspace, va, pa, 1, arch_flags);
}

static bool modloader_wx_flags_ok(uint flags) {
    bool ro = (flags & ARCH_MMU_FLAG_PERM_RO) != 0;
    bool nx = (flags & ARCH_MMU_FLAG_PERM_NO_EXECUTE) != 0;

    return ro || nx;
}

status_t modloader_apply_wx_split(vaddr_t base, size_t map_size, size_t rx_size) {
    rx_size = MIN(rx_size, map_size);
    rx_size = ROUNDUP(rx_size, PAGE_SIZE);

    for (size_t off = 0; off < map_size; off += PAGE_SIZE) {
        vaddr_t va = base + off;
        uint flags = (off < rx_size) ? MODLOADER_MMU_RX : MODLOADER_MMU_RW;
        status_t err = modloader_remap_page(va, flags);

        if (err < 0) {
            return err;
        }
    }

    arch_sync_cache_range((addr_t)base, map_size);
    ISB;
    return NO_ERROR;
}

static uint modloader_page_flags(const struct Elf32_Shdr *shdrs, uint16_t shnum,
                                 const vaddr_t *sec_addr, vaddr_t load_base,
                                 vaddr_t page_va) {
    bool exec = false;
    bool write = false;
    bool alloc = false;

    for (uint16_t i = 1; i < shnum; i++) {
        if (!(shdrs[i].sh_flags & SHF_ALLOC)) {
            continue;
        }

        vaddr_t sec_start = load_base + sec_addr[i];
        vaddr_t sec_end = sec_start + shdrs[i].sh_size;
        vaddr_t page_end = page_va + PAGE_SIZE;

        if (page_va >= sec_end || page_end <= sec_start) {
            continue;
        }

        alloc = true;
        if (shdrs[i].sh_flags & SHF_EXECINSTR) {
            exec = true;
        }
        if (shdrs[i].sh_flags & SHF_WRITE) {
            write = true;
        }
    }

    if (!alloc) {
        return MODLOADER_MMU_RO;
    }
    if (exec) {
        return MODLOADER_MMU_RX;
    }
    if (write) {
        return MODLOADER_MMU_RW;
    }
    return MODLOADER_MMU_RO;
}

status_t modloader_apply_wx_sections(vaddr_t base, size_t map_size,
                                     const struct Elf32_Shdr *shdrs,
                                     uint16_t shnum, const vaddr_t *sec_addr) {
    for (size_t off = 0; off < map_size; off += PAGE_SIZE) {
        vaddr_t va = base + off;
        uint flags = modloader_page_flags(shdrs, shnum, sec_addr, base, va);
        status_t err = modloader_remap_page(va, flags);

        if (err < 0) {
            return err;
        }
    }

    arch_sync_cache_range((addr_t)base, map_size);
    ISB;
    return NO_ERROR;
}

status_t modloader_verify_wx(vaddr_t base, size_t map_size) {
    vmm_aspace_t *aspace = vmm_get_kernel_aspace();

    for (size_t off = 0; off < map_size; off += PAGE_SIZE) {
        vaddr_t va = base + off;
        paddr_t pa;
        uint flags = 0;
        status_t err = arch_mmu_query(&aspace->arch_aspace, va, &pa, &flags);

        if (err < 0) {
            printf("modloader: wxcheck query failed at 0x%lx: %d\n", va, err);
            return err;
        }

        if (!modloader_wx_flags_ok(flags)) {
            printf("modloader: wxcheck failed at 0x%lx (flags 0x%x)\n", va, flags);
            return ERR_NOT_ALLOWED;
        }
    }

    return NO_ERROR;
}
