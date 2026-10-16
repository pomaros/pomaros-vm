# SPDX-License-Identifier: MIT
# Copyright (c) 2026 John Knipper
LLVM ?= /opt/homebrew/opt/llvm/bin
LLD ?= /opt/homebrew/opt/lld/bin
CC := $(LLVM)/clang
LD := $(LLD)/lld-link
AROS_CORE_SOURCE ?= ../../../aros-core-pomaros
FDT_DIR := $(AROS_CORE_SOURCE)/arch/aarch64-all/boot/fdt
BUILD := ../../build/uefi
CFLAGS := --target=aarch64-pc-windows-msvc -std=c11 -O2 -Wall -Wextra -Werror \
 -ffreestanding -fno-stack-protector -fshort-wchar -mgeneral-regs-only -Iinclude -I../../include -I$(FDT_DIR)/libfdt -DAROS_FDT_FREESTANDING \
 -DAROSBOOT_ENTRY_NEGATIVE_CONTROL=0 -DAROSBOOT_FRAME_DIAGNOSTIC=0 \
 -DAROSBOOT_FRAME_DIAGNOSTIC_LATCH=0 -DAROSBOOT_FRAME_DIAGNOSTIC_DELAY_US=0
OBJECTS := $(addprefix $(BUILD)/,main.obj bundle_loader.obj allocation_tracker.obj sha256.obj handoff.obj vz_platform.obj libfdt_fdt.obj libfdt_fdt_ro.obj libfdt_fdt_sw.obj libfdt_fdt_check.obj)
all: $(BUILD)/BOOTAA64.EFI
$(BUILD)/vz_platform.obj: ../../include/vm-console.h
$(BUILD):
	mkdir -p $@
$(BUILD)/%.obj: %.c | $(BUILD)
	$(CC) $(CFLAGS) -c $< -o $@
$(BUILD)/handoff.obj: handoff.S | $(BUILD)
	$(CC) $(CFLAGS) -c $< -o $@
$(BUILD)/BOOTAA64.EFI: $(OBJECTS)
	$(LD) /subsystem:efi_application /entry:efi_main /machine:arm64 /nodefaultlib /dynamicbase /nxcompat /timestamp:0 /out:$@ $(OBJECTS)

$(BUILD)/libfdt_%.obj: $(FDT_DIR)/libfdt/%.c $(FDT_DIR)/libfdt/libfdt_env.h $(FDT_DIR)/freestanding.h | $(BUILD)
	$(CC) $(CFLAGS) -c $< -o $@
