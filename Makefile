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
test: all build/vz-platform-test
	python3 tests/x18.py
	python3 tests/virt-fdt.py
	build/vz-platform-test
	python3 tools/gen-bootinfo.py --check
	cmp include/vm-console.h ../aros-core-vm/arch/aarch64-apple/include/aros/apple/vmconsole.h
clean:
	rm -rf build

build/macaros-vm: src/main.swift entitlements.plist | build
	swiftc -target arm64-apple-macos13.0 -O -warnings-as-errors -framework AppKit -framework Virtualization $< -o $@
	codesign --force --sign - --entitlements entitlements.plist $@

build/vz-platform-test: tests/vz-platform.c boot/arosboot/vz_platform.c include/vm-console.h | build
	clang -std=c11 -Wall -Wextra -Werror -fsanitize=address,undefined \
	 -Iboot/arosboot/include -Iinclude $< -o $@
