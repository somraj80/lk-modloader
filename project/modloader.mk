# QEMU ARM32 virt project for the dynamic modloader PoC (M1-M6, ET_REL only).
# ATfE clang/lld: setup.sh writes build/lk/local.mk with CLANG_BINDIR.
TOOLCHAIN := clang
LD := ld.lld

MODULES += \
	app/shell \
	app/modloader \
	lib/fs

include project/target/qemu-virt-arm32.mk
