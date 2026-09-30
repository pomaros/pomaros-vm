#!/bin/sh
# SPDX-License-Identifier: MIT
# Copyright (c) 2026 John Knipper
set -eu
root=$(CDPATH= cd -- "$(dirname -- "$0")/.." && pwd)
core=${AROS_CORE_SOURCE:-"$root/../aros-core-vm"}
build=${AROS_BUILD:-"$HOME/Build/macaros-vm/core"}
[ -f "$build/arch/aarch64-apple/mmakefile" ] || {
    echo 'An isolated configured AROS Apple build is required; see docs/BUILD.md.' >&2
    exit 1
}
[ "$(git -C "$core" branch --show-current)" = codex-macaros-vm ] || {
    echo 'Refusing a shared native core branch; select the isolated VM checkout.' >&2
    exit 1
}
# A stale generated makefile silently selects the native x18 task-anchor ABI.
if ! grep -q -- '-DAROS_VM_GUEST=1' "$build/arch/aarch64-apple/kernel/mmakefile"; then
    echo 'Regenerate the isolated kernel makefile for the VM profile; see docs/BUILD.md.' >&2
    exit 1
fi
# An APFS clone can retain native compiler flags in generated configuration.
# Normalize only this explicitly selected VM build, before any make target.
python3 - "$build/bin/apple-aarch64/gen/config/target.cfg" <<'PYCONFIG'
from pathlib import Path
import sys
p = Path(sys.argv[1])
s = p.read_bytes()
updated = s.replace(b"-ffixed-x18", b"")
if updated != s:
    p.write_bytes(updated)
    print("Removed inherited native x18 reservation from isolated VM configuration.")
PYCONFIG
# Refresh generated public headers in this isolated build, never the native build.
cp "$core"/arch/aarch64-apple/include/aros/apple/*.h "$build/bin/apple-aarch64/gen/include/aros/apple/"
cd "$build"
platform_make() {
    directory=$1 target=$2
    make -f "$directory/mmakefile" TOP="$build" SRCDIR="$core" CURDIR="$directory" \
        TARGET="$target" AROS_TARGET_CPU=aarch64 AROS_TARGET_ARCH=apple \
        AROS_HOST_CPU=aarch64 AROS_HOST_ARCH=darwin AROS_VM_GUEST=1 "$target"
}
platform_make arch/aarch64-apple apple-stage2
platform_make arch/aarch64-apple/kernel kernel-kernel-apple-aarch64
platform_make rom/kernel kernel-kernel
