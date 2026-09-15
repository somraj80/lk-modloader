# lk-modloader

ARM32 dynamic module loader proof-of-concept for [Little Kernel (LK)](https://github.com/littlekernel/lk).

This repository is a **thin overlay** on upstream LK: it stores only the modloader
diff. Run `./setup.sh` to fetch ATfE, clone LK at a pinned commit, and apply the
overlay into `build/lk/`, then build and test from there.

**Scope: ET_REL, relocated at runtime.** The loader accepts freestanding
ARM32 ELF objects — `ET_REL` (`.o`) relocatable objects as its main format
(M2–M6), plus a fixed-address `ET_EXEC` blob for the simplest possible
proof of the load mechanism (M1 only). It lays out sections, resolves
undefined symbols against the kernel, and applies every relocation entry
itself, directly into kernel memory it already owns — the same model Linux
kernel modules (`.ko`) use. See
[`docs/STAGES.md`](docs/STAGES.md) for the full walkthrough of what that
means and why.

**Repository:** https://github.com/somraj80/lk-modloader  
**Technical details:** [`app/modloader/README.md`](app/modloader/README.md)  
**Stage-by-stage walkthrough:** [`docs/STAGES.md`](docs/STAGES.md) — M1–M6 rationale, ET_EXEC → ET_REL, and how relocations are actually applied  
**Architecture (PDF):** [`docs/lk-modloader-architecture.pdf`](docs/lk-modloader-architecture.pdf) — `python3 scripts/generate_architecture_pdf.py`

## Milestones

| Milestone | Summary | Status |
|-----------|---------|--------|
| M1 | ET_EXEC blob load | Done — returns 42 |
| M2 | ET_REL relocation | Done — returns 43 |
| M3 | Kernel symbol resolution | Done — returns 44 |
| M4 | W^X memory protection | Done — `modtest wxcheck` |
| M5 | Runtime fetch via virtio 9p | Done — returns 43 / 44 |
| M6 | Bidirectional symbol linking | Done — `modtest callsym` returns 55 |

### Roadmap (TODO)

| Milestone | Goal |
|-----------|------|
| **M8 — virtio-blk fetch** | Alternate to 9p: modules on a virtual disk (FAT/ext2), closer to on-device storage |

Design rationale: [`docs/ARCHITECTURE.md`](docs/ARCHITECTURE.md) §8, or the narrative
version in [`docs/STAGES.md`](docs/STAGES.md). Roadmap: §15.

## Prerequisites

- **WSL2** or native Linux (build from `~/projects/...`, not `/mnt/c/...`)
- **QEMU** with `qemu-system-arm` on `PATH`
- **git**, **rsync**, **curl**, **sha256sum**

`setup.sh` downloads **Arm Toolchain for Embedded (ATfE)** from
[arm/arm-toolchain](https://github.com/arm/arm-toolchain) releases (see
[`ATFE_VERSION`](ATFE_VERSION)) into `build/toolchain/` and writes
`build/lk/local.mk` with `CLANG_BINDIR`.

## Quick start

```bash
git clone git@github.com:somraj80/lk-modloader.git ~/projects/lk-modloader
cd ~/projects/lk-modloader
./setup.sh
cd build/lk
make modloader -j$(nproc)
./app/modloader/run_modtest_all.sh
```

Expected: return codes 42, 43, 44, 55 across two QEMU passes (embedded
M1–M6, 9p fetch), with `modtest wxcheck: W^X ok` after each successful load.

CI runs the same flow on push/PR via [`.github/workflows/test.yml`](.github/workflows/test.yml).

## Layout

```
lk-modloader/
├── setup.sh              # fetch ATfE, clone LK, apply overlay
├── LK_PIN                # upstream LK commit
├── ATFE_VERSION          # ATfE release pin
├── project/modloader.mk  # LK project definition
├── app/modloader/        # loader sources and tests
├── docs/                 # architecture doc (MD + PDF)
├── build/toolchain/      # ATfE (gitignored)
└── build/lk/             # upstream LK + overlay (gitignored)
```

### Environment variables

| Variable | Default | Purpose |
|----------|---------|---------|
| `LK_DIR` | `$ROOT/build/lk` | Upstream LK tree |
| `ATFE_ROOT` | `$ROOT/build/toolchain` | ATfE download/cache directory |
| `ATFE_DIR` | `$ATFE_ROOT/ATfE-<ver>-<platform>` | ATfE install root |
| `ATFE_TARBALL` | `$ATFE_ROOT/ATfE-<ver>-<platform>.tar.xz` | Use a local tarball instead of downloading |

Re-run `./setup.sh` after pulling overlay changes to refresh `build/lk/`.
