#!/usr/bin/env python3
# SPDX-License-Identifier: MIT
# Copyright (c) 2026 John Knipper
"""Build or verify the deterministic B2.8 FAT12 SYS: image."""

from __future__ import annotations

import argparse
import hashlib
from pathlib import Path
import struct


SECTOR_SIZE = 512
SECTOR_COUNT = 2880
SECTORS_PER_FAT = 9
ROOT_ENTRIES = 224
ROOT_SECTORS = 14
DATA_START_SECTOR = 1 + 2 * SECTORS_PER_FAT + ROOT_SECTORS
IMAGE_SIZE = SECTOR_SIZE * SECTOR_COUNT
FAT_DATE = ((2026 - 1980) << 9) | (8 << 5) | 29
FAT_TIME = 0

BASE_FILES = (
    ("AROS.boot", b"AROS~1  BOO", b"apple-aarch64\n"),
    (None, b"B2PASS  TXT", b"AROS Apple Silicon B2.8 boot volume\n"),
)
ABI_PROBE_NAME = b"ABIPROBE   "
WORKBENCH_ABI_PROBE_NAME = b"WBPROBE    "
WORKBENCH_ABI_REQUIREMENT_NAME = b"WBTEST  REQ"
WORKBENCH_ABI_REQUIREMENT = b"workbench-abi-v1\n"
TLS_ABI_PROBE_NAME = b"TLSPROBE   "
TLS_ABI_REQUIREMENT_NAME = b"TLSTEST REQ"
TLS_ABI_REQUIREMENT = b"tls-concurrency-v1\n"
WANDERER_NAME = b"WANDERER   "
WANDERER_REQUIREMENT_NAME = b"WANDER  REQ"
WANDERER_REQUIREMENT = b"wanderer-ram-v1\n"
APPLE_USB_DIAG_NAME = b"USBDIAG    "
APPLE_USB_DIAG_REQUIREMENT_NAME = b"USBTEST REQ"
APPLE_USB_DIAG_REQUIREMENT = b"apple-usb-diag-v1\n"
APPLE_PMGR_DIAG_NAME = b"PMGRDIAG   "
APPLE_PMGR_DIAG_REQUIREMENT_NAME = b"PMGRTESTREQ"
APPLE_PMGR_DIAG_REQUIREMENT = b"apple-pmgr-diag-v1\n"
APPLE_AIC_DIAG_NAME = b"AICDIAG    "
APPLE_AIC_DIAG_REQUIREMENT_NAME = b"AICTEST REQ"
APPLE_AIC_DIAG_REQUIREMENT = b"apple-aic-diag-v1\n"
APPLE_TIMER_DIAG_NAME = b"TIMERDIAG  "
APPLE_TIMER_DIAG_REQUIREMENT_NAME = b"TIMETESTREQ"
APPLE_TIMER_DIAG_REQUIREMENT = b"apple-timer-diag-v1\n"
STARTUP_SEQUENCE_LONG_NAME = "Startup-Sequence"
STARTUP_SEQUENCE_NAME = b"STARTU~1SEQ"
STARTUP_SEQUENCE = (
    b"; G4 deterministic RAM desktop boot\n"
    b"G4Ready\n"
    b"EndCLI\n"
)
MAX_ABI_PROBE_SIZE = 512 * 1024
MAX_WANDERER_SIZE = 1024 * 1024
MAX_APPLE_USB_DIAG_SIZE = 1024 * 1024
MAX_APPLE_PMGR_DIAG_SIZE = 1024 * 1024
MAX_APPLE_AIC_DIAG_SIZE = 1024 * 1024
MAX_APPLE_TIMER_DIAG_SIZE = 1024 * 1024
MAX_STARTUP_SEQUENCE_SIZE = SECTOR_SIZE
# The SYS:C directory the boot assign (cliinit AddBootAssign "SYS:C" -> "C")
# points at; without it C: falls back to the SYS: root and every external
# command reports "object not found". One 512-byte directory sector holds
# "." + ".." + up to 14 command entries.
COMMAND_DIR_NAME = b"C          "
MAX_COMMAND_SIZE = 512 * 1024
MAX_COMMAND_COUNT = 64
# Any other file the SYS: volume carries, by path relative to its root
# (Libs/kms.library, Devs/Keymaps/pc105_d): directories are created as needed,
# each a cluster chain of 16 entries per sector, and a name that does not fit
# 8.3 gets a long-name entry with a "~1" alias.
MAX_TREE_FILE_SIZE = 1024 * 1024
MAX_TREE_FILES = 64


def tree_short_name(name: str) -> tuple[bytes, bool]:
    """8.3 name for a tree entry; True when a long-name entry is needed."""
    if not name or name in (".", "..") or "/" in name or "\\" in name:
        raise SystemExit(f"tree entry name is not usable: {name!r}")
    base, dot, extension = name.partition(".")
    plain = (base.replace("_", "").replace("-", "").isalnum() and
             base.isascii() and 1 <= len(base) <= 8 and
             (not dot or (extension.isalnum() and extension.isascii() and
                          1 <= len(extension) <= 3)) and
             "-" not in base and name.count(".") <= 1)
    if plain:
        return (base.upper().ljust(8) + extension.upper().ljust(3)).encode(
            "ascii"), False
    stem = "".join(ch for ch in base.upper() if ch.isalnum())[:6] or "FILE"
    ext = "".join(ch for ch in extension.upper() if ch.isalnum())[:3]
    return (stem + "~1").ljust(8).encode("ascii") + ext.ljust(3).encode(
        "ascii"), True


def root_entry(name: bytes, attributes: int, cluster: int, data: bytes) -> bytes:
    if len(name) != 11:
        raise ValueError("FAT 8.3 name must be exactly 11 bytes")
    entry = bytearray(32)
    entry[0:11] = name
    entry[11] = attributes
    struct.pack_into("<HHHHHHHI", entry, 14,
                     FAT_TIME, FAT_DATE, FAT_DATE, 0,
                     FAT_TIME, FAT_DATE, cluster, len(data))
    return bytes(entry)


def lfn_checksum(short_name: bytes) -> int:
    checksum = 0
    for value in short_name:
        checksum = ((checksum & 1) << 7) + (checksum >> 1) + value
        checksum &= 0xFF
    return checksum


def lfn_entries(long_name: str, short_name: bytes) -> tuple[bytes, ...]:
    codepoints = [ord(character) for character in long_name]
    if not codepoints or len(codepoints) > 255 or any(
            value > 0xFFFF for value in codepoints):
        raise ValueError("fixture long name is outside FAT LFN limits")
    codepoints.append(0)
    codepoints.extend([0xFFFF] * ((-len(codepoints)) % 13))
    offsets = (1, 3, 5, 7, 9, 14, 16, 18, 20, 22, 24, 28, 30)
    chunks = tuple(
        codepoints[index:index + 13]
        for index in range(0, len(codepoints), 13)
    )
    entries = []
    for sequence in range(len(chunks), 0, -1):
        entry = bytearray(32)
        entry[0] = sequence | (0x40 if sequence == len(chunks) else 0)
        entry[11] = 0x0F
        entry[12] = 0
        entry[13] = lfn_checksum(short_name)
        struct.pack_into("<H", entry, 26, 0)
        for offset, value in zip(offsets, chunks[sequence - 1]):
            struct.pack_into("<H", entry, offset, value)
        entries.append(bytes(entry))
    return tuple(entries)


def set_fat12_entry(fat: bytearray, cluster: int, value: int) -> None:
    if cluster < 2 or cluster > 0xFEF or value < 0 or value > 0xFFF:
        raise ValueError("FAT12 cluster or value outside supported range")
    offset = cluster + cluster // 2
    if offset + 1 >= len(fat):
        raise ValueError("FAT12 entry outside FAT")
    if cluster & 1:
        fat[offset] = (fat[offset] & 0x0F) | ((value << 4) & 0xF0)
        fat[offset + 1] = (value >> 4) & 0xFF
    else:
        fat[offset] = value & 0xFF
        fat[offset + 1] = (fat[offset + 1] & 0xF0) | ((value >> 8) & 0x0F)


def build_image(abi_probe: bytes | None = None,
                workbench_abi_probe: bytes | None = None,
                tls_abi_probe: bytes | None = None,
                wanderer: bytes | None = None,
                apple_usb_diag: bytes | None = None,
                apple_pmgr_diag: bytes | None = None,
                startup_sequence: bytes | None = None,
                apple_aic_diag: bytes | None = None,
                apple_timer_diag: bytes | None = None,
                commands: tuple[tuple[str, bytes], ...] | None = None,
                tree: tuple[tuple[str, bytes], ...] | None = None
                ) -> tuple[bytes, int]:
    files = list(BASE_FILES)
    if abi_probe is not None:
        if not abi_probe or len(abi_probe) > MAX_ABI_PROBE_SIZE:
            raise ValueError("ABI probe must contain 1..524288 bytes")
        files.append((None, ABI_PROBE_NAME, abi_probe))
    if workbench_abi_probe is not None:
        if (not workbench_abi_probe or
                len(workbench_abi_probe) > MAX_ABI_PROBE_SIZE):
            raise ValueError("Workbench ABI probe must contain 1..524288 bytes")
        files.append((None, WORKBENCH_ABI_PROBE_NAME, workbench_abi_probe))
        files.append((None, WORKBENCH_ABI_REQUIREMENT_NAME,
                      WORKBENCH_ABI_REQUIREMENT))
    if tls_abi_probe is not None:
        if not tls_abi_probe or len(tls_abi_probe) > MAX_ABI_PROBE_SIZE:
            raise ValueError("TLS ABI probe must contain 1..524288 bytes")
        files.append((None, TLS_ABI_PROBE_NAME, tls_abi_probe))
        files.append((None, TLS_ABI_REQUIREMENT_NAME, TLS_ABI_REQUIREMENT))
    if wanderer is not None:
        if not wanderer or len(wanderer) > MAX_WANDERER_SIZE:
            raise ValueError("Wanderer must contain 1..1048576 bytes")
        files.append((None, WANDERER_NAME, wanderer))
        files.append((None, WANDERER_REQUIREMENT_NAME,
                      WANDERER_REQUIREMENT))
    if apple_usb_diag is not None:
        if (not apple_usb_diag or
                len(apple_usb_diag) > MAX_APPLE_USB_DIAG_SIZE):
            raise ValueError("AppleUSBDiag must contain 1..1048576 bytes")
        files.append((None, APPLE_USB_DIAG_NAME, apple_usb_diag))
        files.append((None, APPLE_USB_DIAG_REQUIREMENT_NAME,
                      APPLE_USB_DIAG_REQUIREMENT))
    if apple_pmgr_diag is not None:
        if (not apple_pmgr_diag or
                len(apple_pmgr_diag) > MAX_APPLE_PMGR_DIAG_SIZE):
            raise ValueError("ApplePMGRDiag must contain 1..1048576 bytes")
        files.append((None, APPLE_PMGR_DIAG_NAME, apple_pmgr_diag))
        files.append((None, APPLE_PMGR_DIAG_REQUIREMENT_NAME,
                      APPLE_PMGR_DIAG_REQUIREMENT))
    if apple_aic_diag is not None:
        if (not apple_aic_diag or
                len(apple_aic_diag) > MAX_APPLE_AIC_DIAG_SIZE):
            raise ValueError("AppleAICDiag must contain 1..1048576 bytes")
        files.append((None, APPLE_AIC_DIAG_NAME, apple_aic_diag))
        files.append((None, APPLE_AIC_DIAG_REQUIREMENT_NAME,
                      APPLE_AIC_DIAG_REQUIREMENT))
    if apple_timer_diag is not None:
        if (not apple_timer_diag or
                len(apple_timer_diag) > MAX_APPLE_TIMER_DIAG_SIZE):
            raise ValueError("AppleTimerDiag must contain 1..1048576 bytes")
        files.append((None, APPLE_TIMER_DIAG_NAME, apple_timer_diag))
        files.append((None, APPLE_TIMER_DIAG_REQUIREMENT_NAME,
                      APPLE_TIMER_DIAG_REQUIREMENT))
    if startup_sequence is not None and (
            not startup_sequence or
            len(startup_sequence) > MAX_STARTUP_SEQUENCE_SIZE):
        raise ValueError("startup sequence must contain 1..512 bytes")
    tree_entries: list[tuple[str, bytes]] = []
    command_file_count = 0
    if commands:
        if len(commands) > MAX_COMMAND_COUNT:
            raise ValueError(f"at most {MAX_COMMAND_COUNT} C: commands")
        for name, data in commands:
            if not data or len(data) > MAX_COMMAND_SIZE:
                raise ValueError("C: command must contain 1..524288 bytes")
            tree_short_name(name)  # rejects names FAT cannot carry
            tree_entries.append(("C/" + name, data))
            command_file_count += 1
    if tree:
        if len(tree) > MAX_TREE_FILES:
            raise ValueError(f"at most {MAX_TREE_FILES} tree files")
        seen_paths: set[str] = set()
        for path, data in tree:
            parts = path.strip("/").split("/")
            if not parts or any(not part for part in parts):
                raise ValueError(f"tree path is not usable: {path!r}")
            if path.lower() in seen_paths:
                raise ValueError(f"duplicate tree path {path!r}")
            if parts[0].upper() == "S" and len(parts) > 1:
                raise ValueError("S is placed by the startup sequence, not "
                                 "by the tree")
            seen_paths.add(path.lower())
            if not data or len(data) > MAX_TREE_FILE_SIZE:
                raise ValueError("tree file must contain 1..1048576 bytes")
            tree_entries.append(("/".join(parts), data))
    if tree_entries:
        lowered = [path.lower() for path, _data in tree_entries]
        if len(set(lowered)) != len(lowered):
            raise ValueError("duplicate path in the SYS: tree")
        tree_entries.sort(key=lambda item: item[0].lower())

    image = bytearray(IMAGE_SIZE)
    boot = memoryview(image)[:SECTOR_SIZE]
    boot[0:3] = b"\xeb\x3c\x90"
    boot[3:11] = b"AROSB28 "
    struct.pack_into("<HBHBHHBHHHII", boot, 11,
                     SECTOR_SIZE, 1, 1, 2, ROOT_ENTRIES, SECTOR_COUNT,
                     0xF0, SECTORS_PER_FAT, 18, 2, 0, 0)
    boot[36] = 0
    boot[37] = 0
    boot[38] = 0x29
    struct.pack_into("<I", boot, 39, 0xB2082026)
    boot[43:54] = b"AROSB2BOOT "
    boot[54:62] = b"FAT12   "
    boot[62:78] = b"AROS B2.8 SYS:  "
    boot[510:512] = b"\x55\xaa"

    fat = bytearray(SECTORS_PER_FAT * SECTOR_SIZE)
    fat[0:3] = b"\xf0\xff\xff"

    root_start = (1 + 2 * SECTORS_PER_FAT) * SECTOR_SIZE
    image[root_start:root_start + 32] = root_entry(
        b"AROSB2BOOT ", 0x08, 0, b"")
    root_index = 1
    next_cluster = 2
    for long_name, short_name, data in files:
        cluster_count = (len(data) + SECTOR_SIZE - 1) // SECTOR_SIZE
        cluster = next_cluster
        if (DATA_START_SECTOR + cluster - 2 + cluster_count > SECTOR_COUNT or
                cluster + cluster_count - 1 > 0xFEF):
            raise ValueError("fixture files exceed FAT12 data capacity")
        for cluster_offset in range(cluster_count):
            current = cluster + cluster_offset
            following = current + 1 if cluster_offset + 1 < cluster_count else 0xFFF
            set_fat12_entry(fat, current, following)
        if long_name is not None:
            for entry in lfn_entries(long_name, short_name):
                entry_start = root_start + root_index * 32
                image[entry_start:entry_start + 32] = entry
                root_index += 1
        entry_start = root_start + root_index * 32
        image[entry_start:entry_start + 32] = root_entry(
            short_name, 0x20, cluster, data)
        root_index += 1
        data_start = (DATA_START_SECTOR + cluster - 2) * SECTOR_SIZE
        image[data_start:data_start + len(data)] = data
        next_cluster += cluster_count

    tree_file_count = 0
    if tree_entries:
        # Group files by directory path, then create directories depth-first
        # so each one knows its parent cluster and its own entry count.
        directories: dict[str, list[tuple[str, bytes]]] = {}
        for path, data in tree_entries:
            folder, _, leaf = path.rpartition("/")
            directories.setdefault(folder, []).append((leaf, data))
        folders: list[str] = []
        for folder in list(directories):
            parts = folder.split("/") if folder else []
            for depth in range(1, len(parts) + 1):
                prefix = "/".join(parts[:depth])
                if prefix not in folders:
                    folders.append(prefix)
        folders.sort(key=lambda item: (item.count("/"), item.lower()))
        directory_cluster_of: dict[str, int] = {"": 0}
        directory_view: dict[str, tuple[bytearray, list[int]]] = {}

        def entry_count(folder: str) -> int:
            count = 0 if folder == "" else 2
            for child in folders:
                if child.rpartition("/")[0] == folder and child != folder:
                    count += 1 + (1 if tree_short_name(
                        child.rpartition("/")[2])[1] else 0)
            for leaf, _data in directories.get(folder, []):
                short, long_needed = tree_short_name(leaf)
                count += 1 + (len(lfn_entries(leaf, short)) if long_needed
                              else 0)
            return count

        def allocate_directory(folder: str) -> int:
            nonlocal next_cluster
            sectors = max(1, (entry_count(folder) * 32 + SECTOR_SIZE - 1)
                          // SECTOR_SIZE)
            first = next_cluster
            if (DATA_START_SECTOR + first - 2 + sectors > SECTOR_COUNT or
                    first + sectors - 1 > 0xFEF):
                raise ValueError("tree directories exceed FAT12 data capacity")
            for offset in range(sectors):
                set_fat12_entry(fat, first + offset,
                                first + offset + 1 if offset + 1 < sectors
                                else 0xFFF)
            next_cluster += sectors
            return first

        def write_entry(folder: str, entry: bytes) -> None:
            nonlocal root_index
            if folder == "":
                start = root_start + root_index * 32
                if root_index >= ROOT_ENTRIES:
                    raise ValueError("root directory is full")
                image[start:start + 32] = entry
                root_index += 1
                return
            view, index = directory_view[folder]
            start = index[0] * 32
            if start + 32 > len(view):
                raise ValueError(f"directory {folder!r} overflowed its sectors")
            view[start:start + 32] = entry
            index[0] += 1

        for folder in folders:
            parent, _, leaf = folder.rpartition("/")
            cluster = allocate_directory(folder)
            directory_cluster_of[folder] = cluster
            sectors = max(1, (entry_count(folder) * 32 + SECTOR_SIZE - 1)
                          // SECTOR_SIZE)
            start = (DATA_START_SECTOR + cluster - 2) * SECTOR_SIZE
            view = memoryview(image)[start:start + sectors * SECTOR_SIZE]
            directory_view[folder] = (view, [0])
            write_entry(folder, root_entry(b".          ", 0x10, cluster, b""))
            write_entry(folder, root_entry(
                b"..         ", 0x10, directory_cluster_of[parent], b""))
            short, long_needed = tree_short_name(leaf)
            if long_needed:
                for entry in lfn_entries(leaf, short):
                    write_entry(parent, entry)
            write_entry(parent, root_entry(short, 0x10, cluster, b""))

        for folder in [""] + folders:
            for leaf, data in directories.get(folder, []):
                cluster_count = (len(data) + SECTOR_SIZE - 1) // SECTOR_SIZE
                cluster = next_cluster
                if (DATA_START_SECTOR + cluster - 2 + cluster_count >
                        SECTOR_COUNT or cluster + cluster_count - 1 > 0xFEF):
                    raise ValueError("tree files exceed FAT12 data capacity")
                for cluster_offset in range(cluster_count):
                    current = cluster + cluster_offset
                    set_fat12_entry(fat, current,
                                    current + 1 if cluster_offset + 1 <
                                    cluster_count else 0xFFF)
                short, long_needed = tree_short_name(leaf)
                if long_needed:
                    for entry in lfn_entries(leaf, short):
                        write_entry(folder, entry)
                write_entry(folder, root_entry(short, 0x20, cluster, data))
                data_start = (DATA_START_SECTOR + cluster - 2) * SECTOR_SIZE
                image[data_start:data_start + len(data)] = data
                next_cluster += cluster_count
                tree_file_count += 1

    nested_file_count = 0
    effective_startup = (startup_sequence if startup_sequence is not None
                         else STARTUP_SEQUENCE if wanderer is not None else None)
    if effective_startup is not None:
        directory_cluster = next_cluster
        startup_cluster = directory_cluster + 1
        if DATA_START_SECTOR + startup_cluster - 2 >= SECTOR_COUNT:
            raise ValueError("fixture directory exceeds FAT12 data capacity")
        set_fat12_entry(fat, directory_cluster, 0xFFF)
        set_fat12_entry(fat, startup_cluster, 0xFFF)

        entry_start = root_start + root_index * 32
        image[entry_start:entry_start + 32] = root_entry(
            b"S          ", 0x10, directory_cluster, b"")
        root_index += 1

        directory_start = (
            DATA_START_SECTOR + directory_cluster - 2) * SECTOR_SIZE
        directory = memoryview(image)[
            directory_start:directory_start + SECTOR_SIZE]
        directory[0:32] = root_entry(
            b".          ", 0x10, directory_cluster, b"")
        directory[32:64] = root_entry(b"..         ", 0x10, 0, b"")
        directory_index = 2
        for entry in lfn_entries(
                STARTUP_SEQUENCE_LONG_NAME, STARTUP_SEQUENCE_NAME):
            start = directory_index * 32
            directory[start:start + 32] = entry
            directory_index += 1
        start = directory_index * 32
        directory[start:start + 32] = root_entry(
            STARTUP_SEQUENCE_NAME, 0x20, startup_cluster,
            effective_startup)
        startup_start = (
            DATA_START_SECTOR + startup_cluster - 2) * SECTOR_SIZE
        image[startup_start:startup_start + len(effective_startup)] = \
            effective_startup
        nested_file_count = 1

    first_fat = SECTOR_SIZE
    second_fat = first_fat + len(fat)
    image[first_fat:first_fat + len(fat)] = fat
    image[second_fat:second_fat + len(fat)] = fat

    return (bytes(image),
            len(files) + nested_file_count + command_file_count +
            tree_file_count)


def main() -> int:
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("output", type=Path)
    parser.add_argument("--verify", action="store_true")
    parser.add_argument("--abi-probe", type=Path)
    parser.add_argument("--workbench-abi-probe", type=Path)
    parser.add_argument("--tls-abi-probe", type=Path)
    parser.add_argument("--wanderer", type=Path)
    parser.add_argument("--apple-usb-diag", type=Path)
    parser.add_argument("--apple-pmgr-diag", type=Path)
    parser.add_argument("--apple-aic-diag", type=Path)
    parser.add_argument("--apple-timer-diag", type=Path)
    parser.add_argument("--startup-sequence", type=Path)
    parser.add_argument(
        "--command", type=Path, action="append", default=[],
        metavar="PATH",
        help="C: command binary placed in SYS:C (repeatable); its file name "
             "becomes the 8.3 command name")
    parser.add_argument(
        "--file", action="append", default=[], metavar="REL=PATH",
        help="any other file, at REL under the SYS: root (directories are "
             "created; repeatable)")
    args = parser.parse_args()

    abi_probe = args.abi_probe.read_bytes() if args.abi_probe is not None else None
    workbench_abi_probe = (args.workbench_abi_probe.read_bytes()
                           if args.workbench_abi_probe is not None else None)
    tls_abi_probe = (args.tls_abi_probe.read_bytes()
                     if args.tls_abi_probe is not None else None)
    wanderer = args.wanderer.read_bytes() if args.wanderer is not None else None
    apple_usb_diag = (args.apple_usb_diag.read_bytes()
                      if args.apple_usb_diag is not None else None)
    apple_pmgr_diag = (args.apple_pmgr_diag.read_bytes()
                       if args.apple_pmgr_diag is not None else None)
    apple_aic_diag = (args.apple_aic_diag.read_bytes()
                      if args.apple_aic_diag is not None else None)
    apple_timer_diag = (args.apple_timer_diag.read_bytes()
                        if args.apple_timer_diag is not None else None)
    startup_sequence = (args.startup_sequence.read_bytes()
                        if args.startup_sequence is not None else None)
    commands = None
    if args.command:
        commands = tuple((path.name, path.read_bytes()) for path in args.command)
    tree = None
    if args.file:
        items = []
        for spec in args.file:
            rel, separator, source = spec.partition("=")
            if not separator or not rel or not source:
                raise SystemExit(f"--file expects REL=PATH, got {spec!r}")
            items.append((rel, Path(source).read_bytes()))
        tree = tuple(items)
    expected, file_count = build_image(
        abi_probe, workbench_abi_probe, tls_abi_probe, wanderer,
        apple_usb_diag, apple_pmgr_diag, startup_sequence, apple_aic_diag,
        apple_timer_diag, commands=commands, tree=tree)
    if args.verify:
        actual = args.output.read_bytes()
        if actual != expected:
            print(f"fat12-image result=FAIL path={args.output} reason=content mismatch")
            return 1
    else:
        args.output.write_bytes(expected)

    digest = hashlib.sha256(expected).hexdigest()
    print(
        f"fat12-image result=PASS path={args.output} bytes={len(expected)} "
        f"files={file_count} sha256={digest}"
    )
    return 0


if __name__ == "__main__":
    raise SystemExit(main())
