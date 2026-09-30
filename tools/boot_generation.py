#!/usr/bin/env python3
# SPDX-License-Identifier: MIT
# Copyright (c) 2026 John Knipper
"""Build and inspect the fixed-size AROS boot-generation record."""

from __future__ import annotations

import argparse
import hashlib
import json
import os
import struct
import sys
import tempfile
from pathlib import Path

sys.dont_write_bytecode = True

MAGIC = 0x474F5241  # "AROG" in the little-endian record.
VERSION = 1
RECORD_BYTES = 128
LOADER_ABI = 1
MAX_BUNDLE_BYTES = 512 * 1024 * 1024
FLAG_ACCEPTED = 1
FLAG_CANDIDATE = 2
DIGEST_OFFSET = 72
DIGEST_BYTES = 32
RECORD = struct.Struct("<IHHQIIQII32s32sQQQ")

if RECORD.size != RECORD_BYTES:
    raise RuntimeError("boot-generation record layout drift")


class GenerationError(ValueError):
    """A generation record or its input violates the closed contract."""


def bounded_bundle(path: Path) -> bytes:
    path = path.expanduser()
    if (not path.is_file() or path.is_symlink() or path.stat().st_size <= 0 or
            path.stat().st_size > MAX_BUNDLE_BYTES):
        raise GenerationError(f"bundle is not a bounded regular file: {path}")
    return path.read_bytes()


def state_flag(state: str) -> int:
    if state == "accepted":
        return FLAG_ACCEPTED
    if state == "candidate":
        return FLAG_CANDIDATE
    raise GenerationError(f"unsupported generation state: {state}")


def build_record(
    bundle: bytes,
    *,
    sequence: int,
    slot: int,
    state: str,
    loader_abi_min: int = LOADER_ABI,
    loader_abi_max: int = LOADER_ABI,
) -> bytes:
    if not isinstance(bundle, bytes) or not 0 < len(bundle) <= MAX_BUNDLE_BYTES:
        raise GenerationError("bundle bytes are outside the supported range")
    return build_record_identity(
        len(bundle),
        hashlib.sha256(bundle).hexdigest(),
        sequence=sequence,
        slot=slot,
        state=state,
        loader_abi_min=loader_abi_min,
        loader_abi_max=loader_abi_max,
    )


def build_record_identity(
    bundle_bytes: int,
    bundle_sha256: str,
    *,
    sequence: int,
    slot: int,
    state: str,
    loader_abi_min: int = LOADER_ABI,
    loader_abi_max: int = LOADER_ABI,
) -> bytes:
    if not isinstance(bundle_bytes, int) or not 0 < bundle_bytes <= MAX_BUNDLE_BYTES:
        raise GenerationError("bundle size is outside the supported range")
    if (not isinstance(bundle_sha256, str) or len(bundle_sha256) != 64 or
            any(character not in "0123456789abcdef" for character in bundle_sha256)):
        raise GenerationError("bundle SHA-256 is not canonical lowercase hex")
    if not isinstance(sequence, int) or not 0 < sequence < 1 << 64:
        raise GenerationError("sequence is outside 1..2^64-1")
    if slot not in (0, 1):
        raise GenerationError("slot must be 0 or 1")
    if (not isinstance(loader_abi_min, int) or
            not isinstance(loader_abi_max, int) or
            not 0 < loader_abi_min <= loader_abi_max < 1 << 32):
        raise GenerationError("loader ABI interval is invalid")
    bundle_digest = bytes.fromhex(bundle_sha256)
    record = RECORD.pack(
        MAGIC,
        VERSION,
        RECORD_BYTES,
        sequence,
        slot,
        state_flag(state),
        bundle_bytes,
        loader_abi_min,
        loader_abi_max,
        bundle_digest,
        bytes(DIGEST_BYTES),
        0,
        0,
        0,
    )
    record_digest = hashlib.sha256(record).digest()
    return record[:DIGEST_OFFSET] + record_digest + record[DIGEST_OFFSET + DIGEST_BYTES:]


def inspect_record(data: bytes, *, expected_slot: int | None = None) -> dict[str, object]:
    if not isinstance(data, bytes) or len(data) != RECORD_BYTES:
        raise GenerationError(f"record must be exactly {RECORD_BYTES} bytes")
    fields = RECORD.unpack(data)
    (magic, version, size, sequence, slot, flags, bundle_bytes,
     loader_abi_min, loader_abi_max, bundle_digest, record_digest,
     reserved0, reserved1, reserved2) = fields
    if magic != MAGIC or version != VERSION or size != RECORD_BYTES:
        raise GenerationError("record magic/version/size is invalid")
    if sequence == 0 or slot not in (0, 1) or flags not in (FLAG_ACCEPTED, FLAG_CANDIDATE):
        raise GenerationError("record sequence/slot/state is invalid")
    if expected_slot is not None and slot != expected_slot:
        raise GenerationError("record slot disagrees with its fixed filename")
    if not 0 < bundle_bytes <= MAX_BUNDLE_BYTES:
        raise GenerationError("record bundle size is invalid")
    if not 0 < loader_abi_min <= loader_abi_max < 1 << 32:
        raise GenerationError("record loader ABI interval is invalid")
    if reserved0 != 0 or reserved1 != 0 or reserved2 != 0:
        raise GenerationError("record reserved fields are non-zero")
    zeroed = data[:DIGEST_OFFSET] + bytes(DIGEST_BYTES) + data[DIGEST_OFFSET + DIGEST_BYTES:]
    if hashlib.sha256(zeroed).digest() != record_digest:
        raise GenerationError("record SHA-256 is invalid")
    return {
        "magic": "AROG",
        "version": version,
        "bytes": size,
        "sequence": sequence,
        "slot": slot,
        "state": "accepted" if flags == FLAG_ACCEPTED else "candidate",
        "bundle_bytes": bundle_bytes,
        "bundle_sha256": bundle_digest.hex(),
        "loader_abi_min": loader_abi_min,
        "loader_abi_max": loader_abi_max,
        "record_sha256": record_digest.hex(),
    }


def write_new(path: Path, data: bytes) -> None:
    path = path.expanduser()
    if path.is_symlink():
        raise GenerationError(f"output already exists: {path}")
    path = path.resolve()
    if path.exists():
        raise GenerationError(f"output already exists: {path}")
    path.parent.mkdir(parents=True, exist_ok=True)
    descriptor, temporary_name = tempfile.mkstemp(prefix=f".{path.name}.", dir=path.parent)
    temporary = Path(temporary_name)
    try:
        with os.fdopen(descriptor, "wb") as stream:
            stream.write(data)
            stream.flush()
            os.fsync(stream.fileno())
        temporary.chmod(0o600)
        os.replace(temporary, path)
    except BaseException:
        if temporary.exists():
            temporary.unlink()
        raise


def main() -> int:
    parser = argparse.ArgumentParser(description=__doc__)
    subparsers = parser.add_subparsers(dest="command", required=True)
    create = subparsers.add_parser("create")
    create.add_argument("--bundle", type=Path, required=True)
    create.add_argument("--sequence", type=int, required=True)
    create.add_argument("--slot", type=int, choices=(0, 1), required=True)
    create.add_argument("--state", choices=("accepted", "candidate"), required=True)
    create.add_argument("--loader-abi-min", type=int, default=LOADER_ABI)
    create.add_argument("--loader-abi-max", type=int, default=LOADER_ABI)
    create.add_argument("--output", type=Path, required=True)
    inspect = subparsers.add_parser("inspect")
    inspect.add_argument("record", type=Path)
    inspect.add_argument("--expected-slot", type=int, choices=(0, 1))
    args = parser.parse_args()
    try:
        if args.command == "create":
            record = build_record(
                bounded_bundle(args.bundle),
                sequence=args.sequence,
                slot=args.slot,
                state=args.state,
                loader_abi_min=args.loader_abi_min,
                loader_abi_max=args.loader_abi_max,
            )
            write_new(args.output, record)
            value = inspect_record(record, expected_slot=args.slot)
        else:
            path = args.record.expanduser()
            if not path.is_file() or path.is_symlink():
                raise GenerationError(f"record is not a regular file: {path}")
            value = inspect_record(path.read_bytes(), expected_slot=args.expected_slot)
        print(json.dumps(value, sort_keys=True))
        return 0
    except (GenerationError, OSError) as exc:
        print(f"boot-generation result=FAIL reason={exc}", file=sys.stderr)
        return 1


if __name__ == "__main__":
    raise SystemExit(main())
