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
test: all
	python3 tests/x18.py
	python3 tests/virt-fdt.py
clean:
	rm -rf build

build/macaros-vm: src/main.swift entitlements.plist | build
	swiftc -O -warnings-as-errors -framework AppKit -framework Virtualization $< -o $@
	codesign --force --sign - --entitlements entitlements.plist $@
