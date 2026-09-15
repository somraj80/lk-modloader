# Build rules for the freestanding payload ET_EXEC and ET_REL objects.

PAYLOAD_SRCDIR := app/modloader/payload
PAYLOAD_BUILDDIR := $(BUILDDIR)/$(PAYLOAD_SRCDIR)
PAYLOAD_ELF := $(PAYLOAD_BUILDDIR)/payload.elf
PAYLOAD_BIN := $(PAYLOAD_BUILDDIR)/payload.bin
PAYLOAD_META_H := $(PAYLOAD_BUILDDIR)/payload_meta.h
PAYLOAD_EMBED_O := $(PAYLOAD_BUILDDIR)/payload_embed.o

PAYLOAD_RELOC_O := $(PAYLOAD_BUILDDIR)/payload_reloc.o
PAYLOAD_RELOC_EMBED_O := $(PAYLOAD_BUILDDIR)/payload_reloc_embed.o

PAYLOAD_SYM_O := $(PAYLOAD_BUILDDIR)/payload_sym.o
PAYLOAD_SYM_EMBED_O := $(PAYLOAD_BUILDDIR)/payload_sym_embed.o

PAYLOAD_IMPORT_O := $(PAYLOAD_BUILDDIR)/payload_import.o
PAYLOAD_IMPORT_EMBED_O := $(PAYLOAD_BUILDDIR)/payload_import_embed.o

# ATfE is clang/lld based; CLANG_BINDIR is set by setup.sh via local.mk.
PAYLOAD_BINDIR := $(CLANG_BINDIR)
PAYLOAD_CC := $(PAYLOAD_BINDIR)/clang
PAYLOAD_LD := $(PAYLOAD_BINDIR)/ld.lld
PAYLOAD_OBJCOPY := $(PAYLOAD_BINDIR)/llvm-objcopy
PAYLOAD_READELF := $(PAYLOAD_BINDIR)/llvm-readelf

ifeq ($(wildcard $(PAYLOAD_CC)),)
$(error modloader payload requires ATfE clang at $(PAYLOAD_CC); set CLANG_BINDIR in local.mk)
endif
ifeq ($(wildcard $(PAYLOAD_LD)),)
$(error modloader payload requires ATfE ld.lld at $(PAYLOAD_LD); set CLANG_BINDIR in local.mk)
endif
ifeq ($(wildcard $(PAYLOAD_OBJCOPY)),)
$(error modloader payload requires ATfE llvm-objcopy at $(PAYLOAD_OBJCOPY); set CLANG_BINDIR in local.mk)
endif
ifeq ($(wildcard $(PAYLOAD_READELF)),)
$(error modloader payload requires ATfE llvm-readelf at $(PAYLOAD_READELF); set CLANG_BINDIR in local.mk)
endif

PAYLOAD_TARGET := arm-none-eabi
PAYLOAD_CFLAGS := --target=$(PAYLOAD_TARGET) -mcpu=cortex-a15 -mthumb -mfloat-abi=softfp -mfpu=neon \
    -ffreestanding -nostdlib -fno-builtin -fno-stack-protector -O2 -g
PAYLOAD_LDFLAGS := --target=$(PAYLOAD_TARGET) -nostdlib -fuse-ld=lld -T $(PAYLOAD_SRCDIR)/payload.ld

PAYLOAD_RELOC_CFLAGS := $(PAYLOAD_CFLAGS) -fpic -DPAYLOAD_RETURN_VALUE=43
PAYLOAD_RELOC_LDFLAGS := --target=$(PAYLOAD_TARGET) -nostdlib -fuse-ld=lld -r \
    -T $(PAYLOAD_SRCDIR)/payload_reloc.ld

PAYLOAD_SYM_SRC := $(PAYLOAD_SRCDIR)/payload_sym.S
PAYLOAD_SYM_CFLAGS := $(PAYLOAD_CFLAGS) -fpic -O0
PAYLOAD_SYM_LDFLAGS := $(PAYLOAD_RELOC_LDFLAGS)

MODULE_INCLUDES += $(PAYLOAD_BUILDDIR)
PAYLOAD_IMPORT_CFLAGS := $(PAYLOAD_RELOC_CFLAGS)
PAYLOAD_IMPORT_LDFLAGS := $(PAYLOAD_RELOC_LDFLAGS)

MODULE_EXTRA_OBJS += $(PAYLOAD_EMBED_O) $(PAYLOAD_RELOC_EMBED_O) $(PAYLOAD_SYM_EMBED_O) \
    $(PAYLOAD_IMPORT_EMBED_O)
MODULE_SRCDEPS += $(PAYLOAD_META_H) $(PAYLOAD_BIN) $(PAYLOAD_RELOC_O) $(PAYLOAD_SYM_O) \
    $(PAYLOAD_IMPORT_O)
GENERATED += $(PAYLOAD_BIN) $(PAYLOAD_META_H) $(PAYLOAD_EMBED_O) \
    $(PAYLOAD_RELOC_O) $(PAYLOAD_RELOC_EMBED_O) \
    $(PAYLOAD_SYM_O) $(PAYLOAD_SYM_EMBED_O) \
    $(PAYLOAD_IMPORT_O) $(PAYLOAD_IMPORT_EMBED_O)

$(PAYLOAD_ELF): $(PAYLOAD_SRCDIR)/payload.c $(PAYLOAD_SRCDIR)/payload.ld
	@$(MKDIR)
	$(info building payload ELF: $@)
	$(NOECHO)$(PAYLOAD_CC) $(PAYLOAD_CFLAGS) -c $(PAYLOAD_SRCDIR)/payload.c \
		-o $(PAYLOAD_BUILDDIR)/payload.o
	$(NOECHO)$(PAYLOAD_CC) $(PAYLOAD_LDFLAGS) $(PAYLOAD_BUILDDIR)/payload.o -o $@

$(PAYLOAD_BIN): $(PAYLOAD_ELF)
	@$(MKDIR)
	$(info extracting payload binary: $@)
	$(NOECHO)$(PAYLOAD_OBJCOPY) -O binary $< $@

$(PAYLOAD_META_H): $(PAYLOAD_ELF)
	@$(MKDIR)
	$(info generating $@)
	$(NOECHO)entry=$$($(PAYLOAD_READELF) -h $< | grep 'Entry point address' | grep -o '0x[0-9a-fA-F]*'); \
	rx_end=$$($(PAYLOAD_READELF) -s $< | awk '/__modloader_rx_end/ {print $$2; exit}'); \
	text_base=$$($(PAYLOAD_READELF) -S -W $< | awk '$$3 == ".text" {print $$5; exit}'); \
	if [ -z "$$rx_end" ] || [ -z "$$text_base" ]; then \
		echo "failed to extract payload layout from $<"; exit 1; \
	fi; \
	rx_size=$$((0x$$rx_end - 0x$$text_base)); \
	printf '#pragma once\n#define PAYLOAD_ENTRY_VADDR %s\n#define PAYLOAD_RX_SIZE %u\n' \
		"$$entry" "$$rx_size" > $@

$(PAYLOAD_EMBED_O): $(PAYLOAD_BIN)
	@$(MKDIR)
	$(info embedding payload binary: $@)
	$(NOECHO)cd $(PAYLOAD_BUILDDIR) && $(PAYLOAD_OBJCOPY) -I binary -O elf32-littlearm -B arm \
		--rename-section .data=.rodata,alloc,load,readonly,data,contents \
		--redefine-sym _binary_payload_bin_start=modloader_payload \
		--redefine-sym _binary_payload_bin_end=modloader_payload_end \
		payload.bin payload_embed.o

$(PAYLOAD_RELOC_O): $(PAYLOAD_SRCDIR)/payload.c $(PAYLOAD_SRCDIR)/payload_reloc.ld
	@$(MKDIR)
	$(info building relocatable payload: $@)
	$(NOECHO)$(PAYLOAD_CC) $(PAYLOAD_RELOC_CFLAGS) -c $(PAYLOAD_SRCDIR)/payload.c \
		-o $(PAYLOAD_BUILDDIR)/payload_reloc_tu.o
	$(NOECHO)$(PAYLOAD_CC) $(PAYLOAD_RELOC_LDFLAGS) $(PAYLOAD_BUILDDIR)/payload_reloc_tu.o -o $@

$(PAYLOAD_RELOC_EMBED_O): $(PAYLOAD_RELOC_O)
	@$(MKDIR)
	$(info embedding relocatable payload: $@)
	$(NOECHO)cd $(PAYLOAD_BUILDDIR) && $(PAYLOAD_OBJCOPY) -I binary -O elf32-littlearm -B arm \
		--rename-section .data=.rodata,alloc,load,readonly,data,contents \
		--redefine-sym _binary_payload_reloc_o_start=modloader_payload_reloc \
		--redefine-sym _binary_payload_reloc_o_end=modloader_payload_reloc_end \
		payload_reloc.o payload_reloc_embed.o

$(PAYLOAD_SYM_O): $(PAYLOAD_SYM_SRC) $(PAYLOAD_SRCDIR)/payload_reloc.ld
	@$(MKDIR)
	$(info building symbol-resolving payload: $@)
	$(NOECHO)$(PAYLOAD_CC) $(PAYLOAD_SYM_CFLAGS) -c $(PAYLOAD_SYM_SRC) \
		-o $(PAYLOAD_BUILDDIR)/payload_sym_tu.o
	$(NOECHO)$(PAYLOAD_CC) $(PAYLOAD_SYM_LDFLAGS) $(PAYLOAD_BUILDDIR)/payload_sym_tu.o -o $@

$(PAYLOAD_SYM_EMBED_O): $(PAYLOAD_SYM_O)
	@$(MKDIR)
	$(info embedding symbol-resolving payload: $@)
	$(NOECHO)cd $(PAYLOAD_BUILDDIR) && $(PAYLOAD_OBJCOPY) -I binary -O elf32-littlearm -B arm \
		--rename-section .data=.rodata,alloc,load,readonly,data,contents \
		--redefine-sym _binary_payload_sym_o_start=modloader_payload_sym \
		--redefine-sym _binary_payload_sym_o_end=modloader_payload_sym_end \
		payload_sym.o payload_sym_embed.o

$(PAYLOAD_IMPORT_O): $(PAYLOAD_SRCDIR)/payload_import.c $(PAYLOAD_SRCDIR)/payload_reloc.ld
	@$(MKDIR)
	$(info building C-import payload: $@)
	$(NOECHO)$(PAYLOAD_CC) $(PAYLOAD_IMPORT_CFLAGS) -c $(PAYLOAD_SRCDIR)/payload_import.c \
		-o $(PAYLOAD_BUILDDIR)/payload_import_tu.o
	$(NOECHO)$(PAYLOAD_CC) $(PAYLOAD_IMPORT_LDFLAGS) $(PAYLOAD_BUILDDIR)/payload_import_tu.o -o $@

$(PAYLOAD_IMPORT_EMBED_O): $(PAYLOAD_IMPORT_O)
	@$(MKDIR)
	$(info embedding C-import payload: $@)
	$(NOECHO)cd $(PAYLOAD_BUILDDIR) && $(PAYLOAD_OBJCOPY) -I binary -O elf32-littlearm -B arm \
		--rename-section .data=.rodata,alloc,load,readonly,data,contents \
		--redefine-sym _binary_payload_import_o_start=modloader_payload_import \
		--redefine-sym _binary_payload_import_o_end=modloader_payload_import_end \
		payload_import.o payload_import_embed.o
