# SPDX-License-Identifier: MIT
# Copyright (c) 2026 John Knipper
LLVM ?= /opt/homebrew/opt/llvm/bin
LLD ?= /opt/homebrew/opt/lld/bin
CC := $(LLVM)/clang
LD := $(LLD)/lld-link
BUILD := ../../build/uefi
CFLAGS := --target=aarch64-pc-windows-msvc -std=c11 -O2 -Wall -Wextra -Werror \
 -ffreestanding -fno-stack-protector -fshort-wchar -mgeneral-regs-only -Iinclude \
 -DAROSBOOT_ENTRY_NEGATIVE_CONTROL=0 -DAROSBOOT_FRAME_DIAGNOSTIC=0 \
 -DAROSBOOT_FRAME_DIAGNOSTIC_LATCH=0 -DAROSBOOT_FRAME_DIAGNOSTIC_DELAY_US=0
OBJECTS := $(addprefix $(BUILD)/,main.obj bundle_loader.obj allocation_tracker.obj sha256.obj handoff.obj)
all: $(BUILD)/BOOTAA64.EFI
$(BUILD):
	mkdir -p $@
$(BUILD)/%.obj: %.c | $(BUILD)
	$(CC) $(CFLAGS) -c $< -o $@
$(BUILD)/handoff.obj: handoff.S | $(BUILD)
	$(CC) $(CFLAGS) -c $< -o $@
$(BUILD)/BOOTAA64.EFI: $(OBJECTS)
	$(LD) /subsystem:efi_application /entry:efi_main /machine:arm64 /nodefaultlib /dynamicbase /nxcompat /timestamp:0 /out:$@ $(OBJECTS)
