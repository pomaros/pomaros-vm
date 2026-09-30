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
test: all build/vz-platform-test build/gic-contract-test build/pci-ecam-access-test build/virtio-pci-map-test build/virtio-status-test
	python3 tests/x18.py
	python3 tests/virt-fdt.py
	python3 tests/early-fdt.py
	build/vz-platform-test
	build/gic-contract-test
	build/pci-ecam-access-test
	python3 tests/pci-ecam-codegen.py
	build/virtio-pci-map-test
	build/virtio-status-test
	python3 tools/gen-bootinfo.py --check
	cmp include/vm-console.h ../aros-core-vm/arch/aarch64-apple/include/aros/apple/vmconsole.h
clean:
	rm -rf build

build/macaros-vm: src/main.swift entitlements.plist | build
	swiftc -target arm64-apple-macos13.0 -O -warnings-as-errors -framework AppKit -framework Virtualization $< -o $@
	codesign --force --sign - --entitlements entitlements.plist $@

build/vz-platform-test: tests/vz-platform.c boot/arosboot/vz_platform.c include/vm-console.h $(FDT_SOURCES) | build
	clang -std=c11 -Wall -Wextra -Werror -fsanitize=address,undefined -fno-sanitize-recover=all \
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

VIRTIO_SOURCE := $(AROS_CORE_SOURCE)/rom/devs/virtio
.PHONY: test-virtio-pci-map
test-virtio-pci-map: build/virtio-pci-map-test
	build/virtio-pci-map-test
build/virtio-pci-map-test: tests/virtio-pci-map.c $(VIRTIO_SOURCE)/pci_mapping.h | build
	clang -std=c11 -Wall -Wextra -Werror -fsanitize=address,undefined \
	 -I$(VIRTIO_SOURCE) $< -o $@

.PHONY: test-virtio-status
test-virtio-status: build/virtio-status-test
	build/virtio-status-test
build/virtio-status-test: tests/virtio-status.c $(VIRTIO_SOURCE)/status.h | build
	clang -std=c11 -Wall -Wextra -Werror -fsanitize=address,undefined \
	 -I$(VIRTIO_SOURCE) $< -o $@

.PHONY: test-virtio-lifecycle
test-virtio-lifecycle: build/virtio-controller-test build/virtio-bus-test
	build/virtio-controller-test
	build/virtio-bus-test
build/virtio-controller-test: tests/virtio-controller.c $(VIRTIO_SOURCE)/virtio_controllerclass.c $(VIRTIO_SOURCE)/virtio_intern.h | build
	clang -std=gnu11 -Wall -Wextra -Werror -Wno-unused-parameter -Wno-ignored-attributes -Wno-cast-function-type-mismatch -fsanitize=address,undefined \
	 -I$(VIRTIO_SOURCE) -idirafter $(AROS_VM_SDK)/AROS/Developer/include $< -o $@
build/virtio-bus-test: tests/virtio-bus.c $(VIRTIO_SOURCE)/virtio_busclass.c $(VIRTIO_SOURCE)/virtio_intern.h | build
	clang -std=gnu11 -Wall -Wextra -Werror -Wno-unused-parameter -Wno-unused-function -Wno-ignored-attributes -Wno-cast-function-type-mismatch -fsanitize=address,undefined \
	 -I$(VIRTIO_SOURCE) -idirafter $(AROS_VM_SDK)/AROS/Developer/include $< -o $@

.PHONY: test-vmpci-dma
test-vmpci-dma: build/vmpci-dma-test
	build/vmpci-dma-test
VMPCI_SOURCE := $(AROS_CORE_SOURCE)/arch/aarch64-all/hidds/vmpci
build/vmpci-dma-test: tests/vmpci-dma.c $(VMPCI_SOURCE)/vmpci_dma.c $(VMPCI_SOURCE)/vmpci.h $(PCI_SOURCE)/ecam/pciecam.h | build
	clang -std=gnu11 -Wall -Wextra -Werror -Wno-ignored-attributes -fsanitize=address,undefined \
	 -I$(PCI_SOURCE) -I$(VMPCI_SOURCE) -idirafter $(AROS_VM_SDK)/AROS/Developer/include $< -o $@

.PHONY: test-virtio-queue
test-virtio-queue: build/virtio-queue-test
	build/virtio-queue-test
build/virtio-queue-test: tests/virtio-queue.c $(VIRTIO_SOURCE)/virtio_queue.c $(VIRTIO_SOURCE)/virtio_queue.h $(VIRTIO_SOURCE)/virtio_intern.h | build
	clang -std=gnu11 -Wall -Wextra -Werror -Wno-ignored-attributes -fsanitize=address,undefined \
	 -I$(VIRTIO_SOURCE) -idirafter $(AROS_VM_SDK)/AROS/Developer/include $< -o $@

.PHONY: test-vmpci-irq
test-vmpci-irq: build/vmpci-irq-test
	build/vmpci-irq-test
build/vmpci-irq-test: tests/vmpci-irq.c $(VMPCI_SOURCE)/vmpci_irq.c $(VMPCI_SOURCE)/vmpci.h $(PCI_SOURCE)/ecam/pciecam.h | build
	clang -std=gnu11 -Wall -Wextra -Werror -Wno-ignored-attributes -fsanitize=address,undefined -fno-sanitize-recover=all \
	 -I$(PCI_SOURCE) -I$(VMPCI_SOURCE) -idirafter $(AROS_VM_SDK)/AROS/Developer/include $< -o $@

.PHONY: test-virtio-io
test-virtio-io: build/virtio-io-test
	build/virtio-io-test
build/virtio-io-test: tests/virtio-io.c $(VIRTIO_SOURCE)/virtio_io.c $(VIRTIO_SOURCE)/virtio_intern.h | build
	clang -std=gnu11 -Wall -Wextra -Werror -Wno-ignored-attributes -Wno-unused-parameter -fsanitize=address,undefined -fno-sanitize-recover=all \
	 -I$(VIRTIO_SOURCE) -idirafter $(AROS_VM_SDK)/AROS/Developer/include $< -o $@

.PHONY: test-console-timeout
test-console-timeout: build/console-timeout-test
	build/console-timeout-test
build/console-timeout-test: tests/console-timeout.c include/vm-console.h | build
	clang -std=gnu11 -Wall -Wextra -Werror -fsanitize=address,undefined -fno-sanitize-recover=all $< -o $@

XHCI_SOURCE := $(AROS_CORE_SOURCE)/rom/usb/pcixhci
build/xhci-dma-test: tests/xhci-dma.c $(XHCI_SOURCE)/xhci_dma.c | build
	clang -std=gnu11 -Wall -Wextra -Werror -Wno-unused-parameter -Wno-ignored-attributes -fsanitize=address,undefined -fno-sanitize-recover=all \
	 -I$(XHCI_SOURCE) -idirafter $(HOME)/Build/macaros-vm/clean-core/bin/apple-aarch64/AROS/Developer/include $< -o $@
.PHONY: test-xhci-dma
test-xhci-dma: build/xhci-dma-test
	build/xhci-dma-test

build/xhci-ring-test: tests/xhci-ring.c $(XHCI_SOURCE)/xhci_ring.c $(XHCI_SOURCE)/xhci_control.c | build
	clang -std=gnu11 -Wall -Wextra -Werror -Wno-unused-parameter -Wno-ignored-attributes -fsanitize=address,undefined -fno-sanitize-recover=all \
	 -I$(XHCI_SOURCE) -idirafter $(AROS_VM_SDK)/AROS/Developer/include $< -o $@
.PHONY: test-xhci-ring
test-xhci-ring: build/xhci-ring-test
	build/xhci-ring-test

build/xhci-request-test: tests/xhci-request.c $(XHCI_SOURCE)/xhci_request.c $(XHCI_SOURCE)/xhci_async.c $(XHCI_SOURCE)/xhci_periodic.c | build
	clang -std=gnu11 -Wall -Wextra -Werror -Wno-unused-parameter -Wno-unused-but-set-variable -Wno-unused-variable -Wno-ignored-attributes -fsanitize=address,undefined -fno-sanitize-recover=all \
	 -I$(XHCI_SOURCE) -idirafter $(AROS_VM_SDK)/AROS/Developer/include $< -o $@
.PHONY: test-xhci-request
test-xhci-request: build/xhci-request-test
	build/xhci-request-test

build/xhci-completion-test: tests/xhci-completion.c $(XHCI_SOURCE)/xhci_completion.inc | build
	clang -std=gnu11 -Wall -Wextra -Werror -Wno-unused-parameter -Wno-ignored-attributes -fsanitize=address,undefined -fno-sanitize-recover=all \
	 -I$(XHCI_SOURCE) -idirafter $(AROS_VM_SDK)/AROS/Developer/include $< -o $@
.PHONY: test-xhci-completion
test-xhci-completion: build/xhci-completion-test
	build/xhci-completion-test

build/xhci-cancel-test: tests/xhci-cancel.c $(XHCI_SOURCE)/xhci_cancel.c $(XHCI_SOURCE)/xhci_ring.c | build
	clang -std=gnu11 -Wall -Wextra -Werror -Wno-unused-parameter -Wno-ignored-attributes -fsanitize=address,undefined -fno-sanitize-recover=all \
	 -I$(XHCI_SOURCE) -idirafter $(AROS_VM_SDK)/AROS/Developer/include $< -o $@
.PHONY: test-xhci-cancel
test-xhci-cancel: build/xhci-cancel-test
	build/xhci-cancel-test

.PHONY: test-virtio-net
test-virtio-net: build/virtio-net-queue-test
	build/virtio-net-queue-test
build/virtio-net-queue-test: tests/virtio-net-queue.c $(VIRTIO_SOURCE)/net_transport.c $(VIRTIO_SOURCE)/net_transport.h | build
	clang -std=gnu11 -Wall -Wextra -Werror -Wno-ignored-attributes -Wno-cast-function-type-mismatch -fsanitize=address,undefined -Wl,-dead_strip \
	 -I$(VIRTIO_SOURCE) -idirafter $(AROS_VM_SDK)/AROS/Developer/include $< -o $@

.PHONY: test-virtio-net-irq
test-virtio-net-irq: build/virtio-net-irq-test
	build/virtio-net-irq-test
build/virtio-net-irq-test: tests/virtio-net-irq.c $(VIRTIO_SOURCE)/net_transport.c $(VIRTIO_SOURCE)/net_transport.h | build
	clang -std=gnu11 -Wall -Wextra -Werror -Wno-ignored-attributes -Wno-cast-function-type-mismatch -fsanitize=address,undefined -Wl,-dead_strip \
	 -I$(VIRTIO_SOURCE) -idirafter $(AROS_VM_SDK)/AROS/Developer/include $< -o $@

.PHONY: test-virtio-vsock
test-virtio-vsock: build/virtio-vsock-queue-test
	build/virtio-vsock-queue-test
build/virtio-vsock-queue-test: tests/virtio-vsock-queue.c $(VIRTIO_SOURCE)/vsock_transport.c $(VIRTIO_SOURCE)/vsock_transport.h | build
	clang -std=gnu11 -Wall -Wextra -Werror -Wno-ignored-attributes -Wno-cast-function-type-mismatch -fsanitize=address,undefined -Wl,-dead_strip \
	 -I$(VIRTIO_SOURCE) -idirafter $(AROS_VM_SDK)/AROS/Developer/include $< -o $@

.PHONY: test-vsock-stream
test-vsock-stream: build/vsock-stream-test
	build/vsock-stream-test
build/vsock-stream-test: tests/vsock-stream.c $(VIRTIO_SOURCE)/vsock_stream.c $(VIRTIO_SOURCE)/vsock_stream.h | build
	clang -std=c11 -Wall -Wextra -Werror -fsanitize=address,undefined -fno-sanitize-recover=all \
	 -I$(VIRTIO_SOURCE) tests/vsock-stream.c $(VIRTIO_SOURCE)/vsock_stream.c -o $@
