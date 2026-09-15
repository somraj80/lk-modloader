# From ET_EXEC to ET_REL: a stage-by-stage guide to the loader

This document is a companion to [`ARCHITECTURE.md`](ARCHITECTURE.md), written to
be read top to bottom rather than looked up. `ARCHITECTURE.md` is the
reference; this is the walkthrough — **why each milestone (M1–M6) exists, in
the order it was built, what "fixed address" vs. "relocatable address"
actually means, and what a relocation entry says to a loader that has to
link it by hand.** Code references point at real functions in
`app/modloader/`, not pseudocode.

If you already know what `.o` files and relocations are, skip to
[§4](#4-milestone-by-milestone) for the M1–M6 walkthrough, or
[§5](#5-how-a-relocation-is-actually-applied) for the mechanics and the
full `R_ARM_*` reference table.

---

## 1. Two kinds of ELF, and why this loader only wants one of them

Every stage in this project is downstream of one question: **what shape of
ELF file is the loader willing to accept?** This loader only ever deals with
two of ELF's `e_type` values, and they are not two flavors of the same
thing — they represent two different *amounts of linking left to do*.

| `e_type` | Produced by | Addresses inside it | Who finishes the job |
|---|---|---|---|
| **`ET_REL`** (relocatable) | `clang -c`, or `ld -r` | Section-relative — every symbol is "offset X into section Y," not a real address | A **linker** (or, here, the loader itself) |
| **`ET_EXEC`** (executable) | `ld` with a fixed link address | Final, absolute — baked in for one specific load address | Nobody — it only runs at that one address |

A compiler never emits `ET_EXEC` directly — that's what a *linker* produces
by consuming one or more `ET_REL` objects and deciding, once, where they'll
live in memory. `ET_REL` is the rawest, most honest output of "compile this
file": every cross-reference is still a placeholder plus an instruction for
how to patch it once a real address is known. That patch-instruction is a
**relocation entry**, and applying it is the one job both of these formats
require *someone* to do — either a build-time linker (for `ET_EXEC`), or,
for `ET_REL`, whoever loads it.

**This project's loader is that linker for `ET_REL`.** Not a shim in front
of one — an actual, small, purpose-built implementation of the same job
`ld` does at build time (section layout, symbol resolution, relocation
application), just running at runtime, inside the kernel, against exactly
one object at a time. This mirrors how Linux loads a kernel module: `insmod`
takes a `.ko` — itself just an `ET_REL` object — and relocates it directly
into memory the kernel already controls, rather than pre-linking it for one
fixed address ahead of time.

`M1` proves the simplest possible version of "load and run code" using
`ET_EXEC` (no linking left to do at all, see §4); every milestone after it
is about becoming a correct-enough linker for `ET_REL`. §2 explains exactly
what "relocatable" means at the address level; §3 covers the ARM/Thumb
specifics needed to read the relocation formulas in §5.

---

## 2. Fixed address vs. relocatable address

This is the transformation the whole project turns on, so it's worth being
precise about what "address" even means at each stage.

### 2.1 What a symbol's address is, at each stage

When the compiler emits `payload_entry`, it does **not** know what address
that function will run at. All it can record is: *"this function starts at
byte offset 0 within the `.text` section of this object file."* That's
`Elf32_Sym.st_value` for a defined symbol in an `ET_REL` object — a
**section-relative offset**, not a real address. It only becomes a real,
callable address once two more things are known:

1. **Where the containing section itself ends up** — its offset within
   whatever the loader lays out in memory (`sec_addr[i]` in this codebase,
   computed in §5.2's layout pass).
2. **Where the whole object is based** — the load address the loader chose
   this time (`load_base`; `PAYLOAD_LOAD_VADDR`, `0x7f108000`, here).

Put together, that's exactly the formula `modloader_sym_value()` computes
for every symbol reference in the object:

```c
addr = load_base + sec_addr[sym->st_shndx] + (sym->st_value & ~1u);
```

**For `ET_EXEC`, this computation already happened once, permanently, at
build time.** The linker picked a `load_base` (here, the same
`0x7f108000`), folded every section's offset into every symbol's
`st_value`, and wrote out final, absolute addresses. That's *why* an
`ET_EXEC` blob can't be relocated afterward — the information needed to
redo the computation (which offset came from which section, which part was
the base) is gone; all that's left is the answer, baked in for one specific
address.

**For `ET_REL`, that computation is deferred — and repeatable.** The
section-relative offsets and the relocation entries that reference them
survive untouched in the file. Nothing stops the loader from picking a
*different* `load_base` on the next run, laying sections out in a
*different* order, or loading the same object twice at two different
addresses in the same boot. That repeatability is the entire meaning of
"relocatable": the file itself carries enough information to compute a
correct address for *any* base, not just the one it happened to be built
against.

### 2.2 A concrete before/after

Take `payload_entry` in `payload_reloc.o`. Before loading:

```
Elf32_Sym { st_name: "payload_entry", st_shndx: 1 (.text), st_value: 0x00 }
```

— "offset 0 into whichever section ends up being section index 1." After
`modloader_load_rel_image()` runs its layout pass and picks, say,
`sec_addr[1] = 0` (`.text` happens to be the first allocated section) with
`load_base = 0x7f108000`:

```
real address = 0x7f108000 + 0 + 0x00 = 0x7f108000
```

That real address is what `modloader_find_entry()` hands back as
`modloader_mod.entry`, and what `modloader_run()` actually calls. Every
other symbol in the object — imported or exported, code or data — goes
through the exact same transformation; §5 is about what happens to every
*reference* to one of these symbols once its real address is known.

### 2.3 Why relocation entries exist at all

A symbol's own address is only half the story — the harder half is every
*other place in the code that refers to it*. A `bl payload_entry`-style call
site, compiled before the linker or loader knows `payload_entry`'s real
address, can't have the right target baked in yet either. So the compiler
emits the instruction with a placeholder offset and records a **relocation
entry**: *"when you know the real addresses, come back to this exact byte
offset and patch in the right value, using this specific encoding."*
Applying every one of those entries — for every reference to every symbol,
using whichever ARM or Thumb instruction encoding happens to be sitting at
that offset — is what §5 walks through in full.

---

## 3. ARM/Thumb concepts behind the relocations

A handful of ARM-architecture facts explain *why* the relocation formulas
in §5 look the way they do. This section exists so none of them come as a
surprise later.

### 3.1 Two instruction sets, one register file

ARM CPUs of this class support two instruction encodings: 32-bit-per-
instruction **ARM state**, and a denser, mostly-16-bit **Thumb state**
(Thumb-2 mixes in some 32-bit instructions too). Both LK itself
(`qemu-virt-arm32`, Cortex-A15) and every payload in this project compile
`-mthumb` — Thumb state throughout. This matters for relocations because
**the same logical operation — "write an absolute address," "write a
branch offset" — is encoded in completely different bit layouts depending
on which state the instruction sitting at the relocation site is in.**
That's why `modloader_reloc.c` has a separate encode/decode function pair
for almost every relocation type: an ARM-mode `MOVW` and a Thumb-mode
`MOVW` hide their 16-bit immediate in different bit positions entirely,
even though both do the same thing.

### 3.2 The Thumb bit — an instruction-set flag hiding in an address

A real, aligned instruction's address always has bit 0 equal to zero (Thumb
instructions are 2-byte aligned at minimum). ARM's calling convention
repurposes that otherwise-always-zero bit: **bit 0 of a function pointer
signals which instruction set to switch into** when branching to it via an
interworking branch (`BX`/`BLX`) — 1 means "switch to Thumb," 0 means "use
ARM." This is why:

- `modloader_sym_is_thumb()` treats a symbol as Thumb code if bit 0 of its
  computed address is set, or if its ELF type is `STT_FUNC`.
- Every place this codebase computes "the address of some function" —
  `modloader_sym_value()` for relocation targets, `modloader_export_vaddr()`
  for kernel exports handed to a payload, `modloader_resolved_sym_addr()`
  for payload exports handed back to the kernel (M6) — **OR**s that bit in,
  never adds it, and always masks it back off (`& ~1u`) before using the
  value as a real byte address for anything other than a branch target.
- The relocation formulas in §5 write `(S + A) | T`, not `+ T` — this is
  the same rule applied at patch time.

Get the OR/mask direction wrong anywhere in that chain and the symptom is
usually a hang or a hard fault the instant control reaches Thumb code
through an address that's missing (or wrongly carrying) its low bit.

### 3.3 PC-relative addressing, and where the "+8 / +4" convention actually shows up

ARM CPUs of this era fetch several instructions ahead of the one currently
executing, so when the *architecture* defines something as "PC-relative,"
the PC value used in that calculation is conventionally the current
instruction's address **plus 8 bytes in ARM state, or plus 4 in Thumb
state** — not the instruction's own address. This is a real, load-bearing
fact about how the CPU will interpret whatever bits end up in the
instruction.

It is *not*, however, something this project's relocation code adds by
hand anywhere. The ARM ELF ABI defines branch-relocation formulas (§5.3)
purely in terms of `P`, "the address of the place being relocated" — the
instruction's own address, no bias added — because the ABI's formula and
the instruction-encoding routines that implement it are already defined
consistently with how the hardware will read the result. The loader just
follows the formula; it never needs to reason about the pipeline itself.

The one place a *literal, undisguised* PC-relative read-ahead genuinely
shows up in this project is a completely different kind of instruction: a
Thumb `LDR` reading a literal pool constant (`R_ARM_THM_PC12`). And even
there, the loader doesn't compute anything — because section layout is
fixed before any relocation runs and never moves again afterward (§5.2),
the compiler's own already-correct, already-biased offset is still correct
once that layout is preserved verbatim in memory. See §5.4 for why that
specific relocation type is this loader's one deliberate no-op.

### 3.4 Section-relative addressing, once more, tying back to §2

Every fact above only matters because of §2's central move: an `ET_REL`
object's addresses are section-relative until the loader picks a base and
a layout. ARM/Thumb's specific encodings, the Thumb bit, and PC-relative
branches are all just *details of how a particular instruction spends its
bits* once the loader already knows the real numbers to plug in. §5 is
where those details and that base computation meet.

---

## 4. Milestone by milestone

Each milestone below states the problem it closes, why it had to come at
that point in the sequence (not before, not after), and the observable proof
that it works. The full command reference lives in
[`app/modloader/README.md`](../app/modloader/README.md); this is about the
*why*, not the *how to run it*.

### M1 — ET_EXEC blob load — proves the mechanism, not the format

**Problem:** before any of this can matter, something has to prove the
kernel can allocate memory, copy foreign code into it, mark it executable,
and jump to it — without that code corrupting the kernel around it.

**Why first:** every later milestone adds *more work before the jump*
(relocating, resolving symbols). M1 strips all of that away by using an
`ET_EXEC` blob pre-linked for the loader's fixed address, `0x7f108000` (see
`ARCHITECTURE.md` §3.2 for why that address). There is nothing left to fix
up — `memcpy` the bytes in, flush the cache, cast the entry address to a
function pointer, call it.

**What's new:** `modloader_load_exec_blob()` in `modloader_reloc.c` — the
entire body is `vmm_alloc` → `memcpy` → `arch_sync_cache_range` → apply
W^X → done.

**Proof:** `modtest load` / `modtest run` → returns **42**.

**The limit that motivates M2:** an `ET_EXEC` blob only works because it was
linked, once, for one specific address (§2.1). It can't be relocated to run
anywhere else, it can't reference anything the loader didn't know about at
*link* time, and it can't be produced by a normal `clang -c` invocation — real
compiler output is `ET_REL`. M1 answers "can we execute foreign code at all?"
and stops there on purpose.

### M2 — ET_REL relocation — the loader becomes a linker

**Problem:** make the loader accept what compilers actually emit, and make
the result position-independent — loadable at whatever address happens to
be free, not one baked in ahead of time.

**Why second:** this is the pivot the whole project is built around (§1,
§2). Nothing else — kernel imports, payload exports, W^X, 9p fetch — has
anywhere to attach until the loader can lay out an `ET_REL` object's
sections in memory and patch its relocations against that layout. M2 is the
minimum version of that: no external symbol resolution yet, just enough
relocation handling for a self-contained payload.

**What's new:** `modloader_load_rel_image()` — parse the ELF header and
section table, walk every `SHF_ALLOC` section computing a cumulative,
alignment-respecting offset (`sec_addr[i]`), copy `PROGBITS` / zero
`NOBITS`, then walk every `SHT_REL`/`SHT_RELA` section applying each entry
via `modloader_apply_arm_reloc()`. See §5 for exactly what "applying a
relocation" means.

**Proof:** `modtest loadrel` / `modtest runrel` → returns **43**.

### M3 — Payload imports kernel symbols — the loader gets an export table

**Problem:** a self-contained payload can prove relocation works, but it's
not useful — real modules need kernel services: `malloc`, `printf`, driver
entry points.

**Why third:** this only makes sense once M2 can already apply relocations;
M3 adds exactly one new input to that pipeline — what address to plug in
when a symbol is `SHN_UNDEF` (defined nowhere in the object itself).
`modloader_resolve_externals()` (`modloader_export.c`) walks the symbol
table, and for every global/weak `UNDEF` symbol looks its name up in a
**kernel export table** — a short, explicit allow-list
(`modloader_builtin_exports[]` plus up to 16 names registered at runtime via
`modloader_export_register()`), not a dump of every kernel symbol.

**Design choice, stated plainly:** this is a capability boundary, not just a
lookup table. A module can only call what the kernel chose to expose by
name — exposing the *entire* kernel symbol namespace to arbitrary loaded
code would make every `modtest loadsym` a supply-chain risk, not a plugin
API.

**A real bug this stage found:** placing an import literal right after
`payload_entry` with no padding let it collide with the linker-script symbol
marking the end of `.text` (`__modloader_rx_end`) — the relocation then
silently targeted the wrong address and the payload hung. Fixed with
explicit `.org 0x10` padding. (§7.3 of `ARCHITECTURE.md`.) The lesson: a
relocatable object's *layout* is as load-bearing as its relocation entries —
get the offsets wrong and the relocation applies correctly to the wrong
byte.

**Proof:** `modtest loadsym` / `modtest runsym` → returns **44** (also
exercised via a C import, `modtest loadimport` / `runimport`, using
`R_ARM_THM_JUMP24` instead of the hand-written assembly's `R_ARM_ABS32`).

### M4 — W^X enforcement — cross-cutting, not sequential

**Problem:** relocation requires writable memory (the loader is patching
instruction bytes and data words in place); execution requires the CPU to
be allowed to fetch instructions from that memory. A page that's
simultaneously writable *and* executable is exactly the primitive most code
injection relies on.

**Why it doesn't have its own number in the load sequence:** M4 isn't a
step *between* other steps — it's a remap that happens **after** every load
path finishes writing (M1's `memcpy`, M2/M3's relocation passes), splitting
permissions per section: `SHF_EXECINSTR` → RX, writable-non-exec → RW,
everything else → RO. `modloader_wx_verify()` then walks every page and
rejects any that are still both writable and executable.

**Proof:** `modtest wxcheck` after *every* successful load — this is the one
check every milestone above shares.

### M5 — Runtime fetch via virtio 9p — decouples "new module" from "rebuild kernel"

**Problem:** M1–M4 only load payloads the kernel image was built with
embedded inside it (`embed:rel`, `embed:sym`). Iterating on a payload
therefore meant rebuilding and reflashing the *kernel* — unworkable for fast
edit-run cycles.

**Why now, not earlier:** M5 adds a **transport**, not a new load format —
it reuses the exact M2/M3 `ET_REL` pipeline unchanged. It could only be
"just a transport" because M2/M3 already made `modloader_load_rel_image()`
take a raw byte buffer, source-agnostic. `modloader_fetch()` mounts a QEMU
`virtio-9p` share (a host directory exposed to the guest) at `/v9p` and
reads the file into that same buffer.

**Why 9p and not something else:** QEMU `virt` and the LK overlay already
had 9p support; it needs zero image-build step (edit the `.o` on the host,
rerun QEMU) — ideal for a fast development loop. `ARCHITECTURE.md` §8.2
compares it against the planned `virtio-blk` alternative (M8, closer to how
a real device would store modules on flash) and against network fetch
(out of scope — needs a network stack for no proportionate benefit here).

**Proof:** `modtest loadfilerel <path>` → 43, `modtest loadfilesym <path>` →
44 — identical return codes to the embedded M2/M3 tests, because it's the
same pipeline with a different byte source.

### M6 — Kernel imports payload symbols — linking becomes bidirectional

**Problem:** M3 lets a payload call *into* the kernel. A plugin API usually
needs the reverse too — the kernel calling a function the *payload* defines
(callbacks, `on_event` hooks, anything shaped like a real plugin
architecture rather than a one-shot `run()`).

**Why last:** this is the mirror image of M3, and needed everything before
it to exist first — a loaded, relocated, correctly-based `ET_REL` image
(M2) whose symbol table is trustworthy. `modloader_symbols_build()`
(`modloader_symbols.c`) indexes every global/weak `STT_FUNC`/`STT_OBJECT`
symbol the module *defines* (skipping linker artifacts like `$t` mapping
symbols and `__modloader_*` internals) into a small table, computing each
one's final address the same way `modloader_sym_value()` does for
relocation targets (§2.1). `modloader_lookup_symbol()` is a name-in,
function-pointer-out lookup against that table.

**Proof:** `modtest callsym payload_get_magic` → **55**. The kernel looks up
a symbol the payload defined and calls it — full bidirectional linking.

### Dependency shape

```
M1 (ET_EXEC)
  └─> M2 (ET_REL relocation) ──> M3 (payload → kernel) ──> M6 (kernel → payload)
                │                       │
                └───────────────────────┴──> M5 (9p fetch: same M2/M3 pipeline, new byte source)

M4 (W^X) wraps the *end* of every load path from M1 onward — not a link in the chain, a gate on all of them.
```

Nothing here is arbitrary ordering for its own sake: each milestone is the
smallest next capability that the *previous* one made expressible, and each
one's test return code (42 → 43 → 44 → 55) is chosen to be unmistakable in
a QEMU serial log — if the wrong number comes back, something upstream of
that milestone broke.

---

## 5. How a relocation is actually applied

This is the mechanical core the whole project sits on top of. Skip it if
you just wanted the milestone rationale — read it if "the loader relocates
the object" has ever felt like it was hiding the interesting part.

### 5.1 What a relocation entry actually says

An `ET_REL` object's code and data are full of placeholders (§2.3). Take a
call to a function the compiler doesn't yet know the address of — it still
has to emit *some* bytes at that call site, so it emits a branch
instruction with a made-up or zeroed offset, and records, in a separate
`.rel.text` / `.rela.text` section, an entry that says, in effect:

> *"At byte offset `r_offset` into this section, there's a reference to
> symbol `r_sym`, encoded as relocation type `r_type` — here's how to
> compute and write the real value once you know where both this section
> and that symbol actually live."*

`r_type` matters because **the same underlying idea — "patch in an
address" — is encoded completely differently depending on what kind of
instruction or data word is sitting at that offset** (§3.1). A `.word`
holding an absolute pointer and a Thumb `BL` instruction's branch-offset
field are both "a relocation," but reading and writing them requires
knowing the exact bit layout of that specific encoding. That's the bulk of
what `modloader_reloc.c` is.

### 5.2 The three passes, in the order they have to happen

`modloader_load_rel_image()` does exactly three things, strictly in this
order, and the order is load-bearing:

1. **Layout sections** — walk every `SHF_ALLOC` section, assign each one a
   cumulative, alignment-respecting offset into one flat allocation
   (`sec_addr[i]`), copy `PROGBITS` bytes in / zero `NOBITS`. After this
   pass, *every* symbol defined inside the object has a real address (§2.1):
   `load_base + sec_addr[sym->st_shndx] + sym->st_value`. This has to
   happen before relocation because a relocation's target symbol might live
   in a *different* section than the one being patched, and both need real
   addresses first.

2. **Resolve external symbols** — for every symbol that's `SHN_UNDEF`
   (defined nowhere in this object), look its name up in the kernel export
   table (§4, M3) *before* touching any relocation that references it. This
   has to happen before pass 3 because relocation application needs a
   concrete address for every symbol, defined-locally or imported, before
   it can compute anything.

3. **Apply relocations** — walk every `SHT_REL`/`SHT_RELA` section, and for
   each entry: find the target symbol's now-known address (from pass 1 or
   2), compute the relocation's value, and write it into the instruction or
   data word at `r_offset` using that relocation type's specific encoding
   (§5.4).

Only after all three passes succeed does the loader look for
`payload_entry`, apply W^X, and mark the module loaded. If pass 3 hits a
relocation type it doesn't recognize, or pass 2 finds an import with no
matching export, the whole load aborts and the partial mapping is freed —
there is no partially-linked state a caller can observe.

### 5.3 One relocation, start to finish: `R_ARM_THM_CALL`

Take a Thumb `BL`/`BLX` instruction calling an imported kernel function —
the exact case M3's `payload_sym.S` exercises. The formula (ELF for the ARM
Architecture ABI, and `modloader_apply_arm_reloc()`'s `R_ARM_THM_CALL` case)
is:

```
result = ((S + A) | T) - P
```

Read left to right, in terms of real local variables in the code:

| Term | Meaning | Where it comes from |
|---|---|---|
| `S` | The symbol's address | `sym_val` — either a locally-defined symbol's computed address (pass 1, §2.1), or an imported one's kernel export address (pass 2) |
| `A` | The addend — an extra offset baked into the call, usually 0 for a plain call | For `SHT_REL` (no explicit addend field), **decoded from the instruction's own bits** by `modloader_reloc_addend_thumb32_branch()` — REL-type relocations store the addend *in the instruction being patched*, not in the relocation entry. `SHT_RELA` stores it explicitly in `r_addend` instead; the loader handles both (`is_rela` branch). |
| `T` | 1 if the target is Thumb code, else 0 | `modloader_sym_is_thumb()` (§3.2) |
| `P` | The address of the relocation site itself (the branch instruction) | `place` — computed the same way `S` is, for whatever's being patched right now (§3.3 on why there's no separate pipeline-bias term here) |

`(S + A) | T` — not `+ T` — is the detail from §3.2 showing up in the
formula directly: OR-ing the Thumb bit in, rather than adding it, is what
keeps a Thumb function's address correctly odd wherever it's referenced —
from a branch target, from a kernel export handed to a payload, from a
payload symbol handed back to the kernel via M6. Every one of those three
paths in this codebase applies the same rule independently, because each
one is computing "the address of some Thumb code" from a different
direction.

Once `result` is computed, it isn't stored as a plain 32-bit word — Thumb-2
`BL`'s 25-bit signed branch range is split across two 16-bit halfwords in a
scrambled bit order (`S`/`I1`/`I2`/`imm10`/`imm11`, ARM's own naming, not
this project's). `modloader_reloc_encode_thumb32_branch()` exists
specifically to pack a plain signed offset into that layout — and
`modloader_reloc_addend_thumb32_branch()` is the same transform run
backwards, to pull the pre-existing (usually-zero) addend back out of
whatever the compiler left there.

### 5.4 The full reference: every `R_ARM_*` type this loader handles

Every relocation type below is defined by the *ELF for the ARM
Architecture* ABI; the numeric value is the `r_type` field's actual byte
value, as used by `ELF32_R_TYPE(r_info)`. They fall into four families —
worth telling apart, because each solves a different kind of problem.

**Data relocations — write a full value straight into a plain memory word:**

| Type | # | Meaning |
|---|---|---|
| `R_ARM_ABS32` | 2 | Write the absolute address `(S + A) \| T` into a 32-bit word — a literal-pool entry, a function-pointer field, any plain `.word` holding an address. |
| `R_ARM_REL32` | 3 | Write `(S + A) \| T` **minus the address of this word** — a self-relative offset, used where the reader will add its own address back at runtime. |
| `R_ARM_PREL31` | 42 | Like `REL32`, but only the low 31 bits are the offset — bit 31 is preserved from whatever was already in the word (used where that bit carries its own meaning to the reader). |

**Branch relocations — write a scaled, PC-relative offset into a specific field of a branch instruction (§3.3):**

| Type | # | Meaning |
|---|---|---|
| `R_ARM_CALL` | 28 | ARM-mode `BL`/`BLX` — 26-bit word-aligned range; the loader flips the instruction to `BLX` automatically when the target is Thumb (an ISA switch a plain `BL` can't perform). |
| `R_ARM_JUMP24` | 29 | ARM-mode `B`/`BL` — same 26-bit range, no automatic ISA switch. |
| `R_ARM_THM_CALL` | 10 (ABI's legacy alias `R_ARM_THM_PC22`) | Thumb-mode `BL`/`BLX` — up to 25-bit range with the J1/J2 range extension; same automatic `BL`↔`BLX` ISA-switch logic as `R_ARM_CALL`. |
| `R_ARM_THM_JUMP24` | 30 | Thumb-mode `B` — up to 25-bit range, no link register write, no ISA switch. |

**Immediate-pair relocations — write half of a 32-bit value into one `MOVW`/`MOVT` instruction's split immediate field (the `_NC` suffix means "no overflow check" on the 16-bit truncation):**

| Type | # | Meaning |
|---|---|---|
| `R_ARM_MOVW_ABS_NC` | 43 | Low 16 bits of `(S + A) \| T`, ARM-mode `MOVW`. |
| `R_ARM_MOVT_ABS` | 44 | High 16 bits of `S + A`, ARM-mode `MOVT`. |
| `R_ARM_THM_MOVW_ABS_NC` | 47 | Low 16 bits of `(S + A) \| T`, Thumb-2 `MOVW` (different bit layout than the ARM-mode version, same value). |
| `R_ARM_THM_MOVT_ABS` | 48 | High 16 bits of `S + A`, Thumb-2 `MOVT`. |

A `MOVW`/`MOVT` pair is how ARM/Thumb build an arbitrary 32-bit constant in
two instructions when there's no literal pool nearby to `LDR` from — one
`MOVW` relocation and one `MOVT` relocation, both targeting the same
symbol, always travel together.

**Special case:**

| Type | # | Meaning |
|---|---|---|
| `R_ARM_THM_PC12` | 54 | Thumb `LDR` reading a literal-pool constant (§3.3). This loader's one deliberate no-op: section layout is frozen before any relocation runs (§5.2) and never moves afterward, so the compiler's own already-correct PC-relative offset is still correct once that layout is preserved verbatim in memory — there's nothing to patch. |

**Recognized but never exercised by this loader's `ET_REL`-only scope:**
`R_ARM_GLOB_DAT` (21), `R_ARM_JUMP_SLOT` (22), `R_ARM_RELATIVE` (23), and
`R_ARM_TARGET2` (41) are handled in `modloader_apply_arm_reloc()` for
completeness against the ABI, but a plain `clang -c` invocation never
emits them — they're written by a *linker* for load scenarios outside this
project's scope. Kept because removing them narrows the relocation surface
without narrowing what this loader actually does.

### 5.5 Why the export table is the whole security model

Put together, passes 2 and 3 (§5.2) mean: **a payload can only ever branch
to, or read the address of, something that is either (a) defined inside
itself, or (b) explicitly present in the kernel's export table by name.**
There's no walk of every kernel symbol as a fallback — an `UNDEF` symbol
with no matching export entry fails the load outright
(`modloader_resolve_externals()` returns `ERR_NOT_FOUND` for anything that
isn't `STB_WEAK`). The export table isn't an optimization over a "real"
full symbol table; for this loader, it *is* the boundary between "code the
kernel trusts enough to run" and "everything else the kernel knows how to
do."

---

## 6. Where this leaves the loader

By M6, the loader is — deliberately — a small, special-purpose linker: it
turns section-relative addresses into real ones (§2), lays out sections,
resolves a bounded, explicit set of imports, applies a known set of
ARM/Thumb relocation types (§5.4), and indexes what the loaded object
exports, all against exactly one `ET_REL` object at a time, directly into
memory it already owns. That is the entire feature set `insmod` needs for a
`.ko` — and, per §1, not coincidentally, the entire feature set this
project needs either.

For the full reference — file-by-file responsibilities, the public API, W^X
internals, the 9p transport, CI, and the roadmap (M8) — see
[`ARCHITECTURE.md`](ARCHITECTURE.md).
