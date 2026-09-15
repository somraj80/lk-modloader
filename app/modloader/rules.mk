LOCAL_DIR := $(GET_LOCAL_DIR)

MODULE := $(LOCAL_DIR)

MODULE_INCLUDES += $(LOCAL_DIR)/include

MODULE_SRCS += \
	$(LOCAL_DIR)/modloader.c \
	$(LOCAL_DIR)/modloader_export.c \
	$(LOCAL_DIR)/modloader_fetch.c \
	$(LOCAL_DIR)/modloader_reloc.c \
	$(LOCAL_DIR)/modloader_shell.c \
	$(LOCAL_DIR)/modloader_symbols.c \
	$(LOCAL_DIR)/modloader_wx.c

MODULE_DEPS += \
	kernel \
	lib/bio \
	lib/console \
	lib/elf \
	lib/fs

include $(LOCAL_DIR)/payload_buildrules.mk

include make/module.mk
