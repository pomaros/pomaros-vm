AROS_CORE_SOURCE ?= ../aros-core-vm
FDT_DIR := $(AROS_CORE_SOURCE)/arch/aarch64-all/boot/fdt
FDT_SOURCES := $(addprefix $(FDT_DIR)/libfdt/,fdt.c fdt_ro.c fdt_sw.c fdt_check.c)
# SPDX-License-Identifier: MIT
# Copyright (c) 2026 John Knipper
.PHONY: all test clean
all: build/x18-probe build/x18.bin build/macaros-vm
build:
	mkdir -p $@
build/x18.o: guest/x18.S | build
	clang --target=aarch64-none-elf -c $< -o $@
build/x18.bin: build/x18.o
	ld.lld -Ttext=0x40000000 --oformat=binary $< -o $@
build/x18-probe: src/x18-probe.c entitlements.plist | build
	clang -Wall -Wextra -Werror -O2 $< -framework Hypervisor -o $@
	codesign --force --sign - --entitlements entitlements.plist $@
test: all build/vz-platform-test build/gic-contract-test build/pci-ecam-access-test
	python3 tests/x18.py
	python3 tests/virt-fdt.py
	python3 tests/early-fdt.py
	build/vz-platform-test
	build/gic-contract-test
	build/pci-ecam-access-test
	python3 tests/pci-ecam-codegen.py
	python3 tools/gen-bootinfo.py --check
	cmp include/vm-console.h ../aros-core-vm/arch/aarch64-apple/include/aros/apple/vmconsole.h
clean:
	rm -rf build

build/macaros-vm: src/main.swift entitlements.plist | build
	swiftc -target arm64-apple-macos13.0 -O -warnings-as-errors -framework AppKit -framework Virtualization $< -o $@
	codesign --force --sign - --entitlements entitlements.plist $@

build/vz-platform-test: tests/vz-platform.c boot/arosboot/vz_platform.c include/vm-console.h $(FDT_SOURCES) | build
	clang -std=c11 -Wall -Wextra -Werror -fsanitize=address,undefined \
	 -Iboot/arosboot/include -Iinclude -I$(FDT_DIR)/libfdt $< $(FDT_SOURCES) -o $@

GIC_CONTRACT_SOURCES := $(AROS_CORE_SOURCE)/arch/aarch64-apple/bootstrap/stage2_tags.c \
 $(AROS_CORE_SOURCE)/arch/aarch64-apple/bootstrap/boot_memory_map.c \
 $(AROS_CORE_SOURCE)/arch/aarch64-apple/kernel/memory_plan_contract.c \
 $(AROS_CORE_SOURCE)/arch/aarch64-apple/kernel/startup_contract.c
build/gic-contract-test: tests/gic-contract.c $(GIC_CONTRACT_SOURCES) | build
	clang -std=c11 -Wall -Wextra -Werror -fsanitize=address,undefined \
	 -I$(AROS_CORE_SOURCE)/arch/aarch64-apple/include \
	 -I$(AROS_CORE_SOURCE)/arch/aarch64-apple/bootstrap $< $(GIC_CONTRACT_SOURCES) -o $@

AROS_VM_SDK ?= $(HOME)/Build/macaros-vm/clean-core/bin/apple-aarch64
OF_SOURCE := $(AROS_CORE_SOURCE)/rom/openfirmware
.PHONY: test-openfirmware
test-openfirmware: build/openfirmware-fdt-test
	dtc -q -I dts -O dtb tests/fdt/portable.dts -o build/openfirmware-fixture.dtb
	build/openfirmware-fdt-test build/openfirmware-fixture.dtb
build/openfirmware-fdt-test: tests/openfirmware-fdt.c $(OF_SOURCE)/of_fdt.c $(OF_SOURCE)/of_intern.h $(FDT_SOURCES) $(FDT_DIR)/early_fdt.c | build
	clang -std=c11 -Wall -Wextra -Werror -Wno-ignored-attributes -fsanitize=address,undefined \
	 -I$(OF_SOURCE) -idirafter $(AROS_VM_SDK)/AROS/Developer/include \
	 -I$(FDT_DIR) -I$(FDT_DIR)/libfdt $< $(FDT_DIR)/early_fdt.c $(FDT_SOURCES) -o $@

PCI_SOURCE := $(AROS_CORE_SOURCE)/rom/hidds/pci
build/pci-ecam-access-test: tests/pci-ecam-access.c $(PCI_SOURCE)/ecam/access.h | build
	clang -std=c11 -Wall -Wextra -Werror -fsanitize=address,undefined \
	 -I$(PCI_SOURCE) $< -o $@

.PHONY: test-pci-ecam-class
test-pci-ecam-class: build/pci-ecam-class-test
	build/pci-ecam-class-test
build/pci-ecam-class-test: tests/pci-ecam-class.c $(PCI_SOURCE)/ecam/pciecam_class.c $(PCI_SOURCE)/ecam/pciecam.h $(PCI_SOURCE)/ecam/access.h | build
	clang -std=c11 -Wall -Wextra -Werror -Wno-ignored-attributes -fsanitize=address,undefined \
	 -I$(PCI_SOURCE)/ecam -idirafter $(AROS_VM_SDK)/AROS/Developer/include $< -o $@
