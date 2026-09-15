#!/bin/bash
# Run M1-M6 modloader tests via lk.autorun (split: LK autorun max is 512 bytes).
set -euo pipefail
cd "$(dirname "$0")/../.."

PAYLOAD_DIR="$(pwd)/build-modloader/app/modloader/payload"
if [ ! -f "$PAYLOAD_DIR/payload_reloc.o" ] || [ ! -f "$PAYLOAD_DIR/payload_sym.o" ]; then
    echo "building payloads for 9p share ..."
    make modloader -j"$(nproc)"
fi

GREP_FILTER='modtest|modloader|payload returned|callsym|wxcheck|undefined symbol|panic|fault|autorun script'

run_qemu() {
    scripts/do-qemuarm -p modloader "$@" 2>&1 | grep --line-buffered -E "$GREP_FILTER"
}

# Pass 1: embedded payloads (M1-M3, import, M6 callsym)
AUTORUN_EMBED='modtest+load;modtest+run;modtest+wxcheck;modtest+unload;modtest+loadrel;modtest+runrel;modtest+wxcheck;modtest+unload;modtest+loadsym;modtest+runsym;modtest+callsym+payload_get_magic;modtest+wxcheck;modtest+unload;modtest+loadimport;modtest+runimport;modtest+wxcheck;modtest+unload;poweroff'

# Pass 2: 9p fetch (M5)
AUTORUN_9P='modtest+loadfilerel+/v9p/payload_reloc.o;modtest+runrel;modtest+wxcheck;modtest+unload;modtest+loadfilesym+/v9p/payload_sym.o;modtest+runsym;modtest+callsym+payload_get_magic;modtest+wxcheck;modtest+unload;poweroff'

run_qemu -A "lk.autorun=${AUTORUN_EMBED}"
run_qemu -f "$PAYLOAD_DIR" -A "lk.autorun=${AUTORUN_9P}"
