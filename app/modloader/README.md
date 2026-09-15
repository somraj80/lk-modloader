# Dynamic modloader (ARM32 PoC)

Proof-of-concept dynamic module loader for LK on QEMU ARM32 `virt`.

**Project:** `modloader` (`project/modloader.mk`)  
**Load VA:** `0x7f108000`  
**Build output:** `build/lk/build-modloader/` (after `./setup.sh`)

Design and rationale: [`docs/ARCHITECTURE.md`](../../docs/ARCHITECTURE.md) /
[`docs/lk-modloader-architecture.pdf`](../../docs/lk-modloader-architecture.pdf).

## Quick reference

| Milestone | Load | Run / verify | Return |
|-----------|------|--------------|--------|
| M1 | `modtest load` | `modtest run`; `modtest wxcheck` | 42 |
| M2 | `modtest loadrel` | `modtest runrel`; `modtest wxcheck` | 43 |
| M3 | `modtest loadsym` | `modtest runsym`; `modtest wxcheck` | 44 |
| M3 (C) | `modtest loadimport` | `modtest runimport`; `modtest wxcheck` | 44 |
| M5 | `modtest loadfilerel <path>` | `modtest runrel`; `modtest wxcheck` | 43 |
| M5 | `modtest loadfilesym <path>` | `modtest runsym`; `modtest wxcheck` | 44 |
| M6 | `modtest loadsym` | `modtest callsym payload_get_magic`; `modtest wxcheck` | 55 |

Use `modtest unload` after each test. M4 (W^X) is checked by `wxcheck`. Other: `modtest info`.

## Build and test

```bash
# from overlay repo root
./setup.sh
cd build/lk
make modloader -j$(nproc)
./app/modloader/run_modtest_all.sh    # M1–M6 autorun (two QEMU passes)
```

`run_modtest_all.sh` runs two QEMU passes: embedded M1–M6, then 9p fetch of the same ET_REL payloads.

### Interactive QEMU

```bash
cd build/lk
PAYLOAD_DIR="$(pwd)/build-modloader/app/modloader/payload"
scripts/do-qemuarm -p modloader -f "$PAYLOAD_DIR"
```

## Source layout

```
app/modloader/
├── include/app/modloader.h  # public load/run API
├── modloader.c              # unified load API (image + URI)
├── modloader_export.c       # kernel export table (payload imports)
├── modloader_symbols.c      # module export table (kernel lookups)
├── modloader_fetch.c        # virtio 9p file fetch (M5)
├── modloader_reloc.c        # ET_EXEC/ET_REL loader + ARM reloc
├── modloader_shell.c        # modtest shell command
├── modloader_wx.c           # W^X remap + verify
├── modloader_priv.h         # internal shared definitions
├── rules.mk
├── payload_buildrules.mk    # payload build + embed
├── run_modtest_all.sh       # autorun M1–M6 (two passes)
└── payload/
    ├── payload.c            # M1/M2 entry (42 or 43)
    ├── payload.ld           # M1 ET_EXEC link script
    ├── payload_reloc.ld     # M2/M3/M5/M6 ET_REL link script
    ├── payload_sym.S        # M3/M6 sym payload (import + export)
    └── payload_import.c     # M3 C-import (THM_JUMP24)
```

Public API: `include/app/modloader.h`.
