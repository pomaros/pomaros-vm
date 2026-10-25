#!/bin/sh
# SPDX-License-Identifier: MIT
# Copyright (c) 2026 John Knipper
set -eu
root=$(CDPATH= cd -- "$(dirname -- "$0")/.." && pwd)
core=${AROS_CORE_SOURCE:-"$root/../aros-core"}
build=${AROS_BUILD:-"$root/../Build/pomaros-vm/reference-core"}
[ -f "$build/arch/aarch64-apple/mmakefile" ] || {
    echo 'An isolated configured AROS Apple build is required; see docs/BUILD.md.' >&2
    exit 1
}
# Admit the exact source/ABI pin; detached public source checkouts are normal.
python3 - "$root/CORE.lock" "$core" "$build/vm-build-environment.json" <<'PYPIN'
import json, subprocess, sys
from pathlib import Path
lock=dict(line.split('=',1) for line in Path(sys.argv[1]).read_text().splitlines()
          if line and not line.startswith('#'))
revision=subprocess.check_output(['git','-C',sys.argv[2],'rev-parse','HEAD'],text=True).strip()
if (revision != lock['CORE_REVISION'] or lock['CORE_X18_POLICY'] != 'unrestricted'
        or lock['CORE_TARGET'] != 'apple-aarch64'
        or subprocess.check_output(['git','-C',sys.argv[2],'status','--porcelain'],text=True)
        or json.loads(Path(sys.argv[3]).read_text())['source_revision'] != revision):
    raise SystemExit('Source, build or guest ABI differs from CORE.lock; use a fresh matching VM build.')
PYPIN
# A stale generated makefile silently selects the native x18 task-anchor ABI.
if ! grep -q -- '-DAROS_VM_GUEST=1' "$build/arch/aarch64-apple/kernel/mmakefile"; then
    echo 'Regenerate the isolated kernel makefile for the VM profile; see docs/BUILD.md.' >&2
    exit 1
fi
# Refuse a stale hosted register reservation rather than rewriting build metadata.
if grep -q -- '-ffixed-x18' "$build/bin/apple-aarch64/gen/config/target.cfg"; then
    echo 'VM configuration reserves x18; configure a new guest build.' >&2
    exit 1
fi
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
