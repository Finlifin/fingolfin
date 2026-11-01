#!/usr/bin/env python3
"""Offline, independent canonical-hash and malformed-package corpus for C loader."""

import argparse
import copy
import hashlib
import json
import struct
from pathlib import Path

parser = argparse.ArgumentParser()
parser.add_argument("--input", type=Path, required=True)
parser.add_argument("--output", type=Path, required=True)
args = parser.parse_args()
raw = args.input.read_bytes()
original = json.loads(raw)


def encode(value, *, sort=False):
    return json.dumps(value, ensure_ascii=False, separators=(",", ":"), sort_keys=sort).encode()


def seal(value):
    # Compiler struct field order is retained by Python's ordered dictionaries.
    value["integrity"] = hashlib.sha256(encode(value["manifest"])).hexdigest()
    return encode(value)


assert seal(copy.deepcopy(original)) == raw.strip(), "fixture must use compiler canonical encoding"
cases = [(1, raw), (1, json.dumps(original, sort_keys=True, indent=2).encode())]
for n in range(min(len(raw), 256)):
    cases.append((0, raw[:n]))
for n in range(256, len(raw), 97):
    cases.append((0, raw[:n]))
cases += [(0, raw + b"{}"), (0, raw + b"\x00"), (0, b"[" * 100 + b"]" * 100)]
# Equivalent escaped field names, duplicates with/without escaped spelling.
cases += [
    (1, raw.replace(b'"magic"', b'"ma\\u0067ic"', 1)),
    (0, raw.replace(b"{", b'{"magic":"bad",', 1)),
    (0, raw.replace(b"{", b'{"ma\\u0067ic":"bad",', 1)),
]
for number in [b"18446744073709551616", b"-9223372036854775809", b"3.0", b"3e0"]:
    cases.append((0, raw.replace(b'"version":3', b'"version":' + number, 1)))
for text in [b"\\u0000", b"\\ud800", b"\\udc00", b"\xff"]:
    cases.append((0, raw.replace(b"FINGOLFIN-FGO", text, 1)))


def manifest_case(field, value):
    obj = copy.deepcopy(original)
    obj["manifest"][field] = value
    cases.append((0, seal(obj)))


for field, value in [
    ("version", 2),
    ("target", "wrong"),
    ("profile", "wrong"),
    ("typed_ir", []),
    ("entries", []),
    ("clang_flags", [0]),
    ("program_source_hash", "0" * 63),
    ("extra", 1),
]:
    manifest_case(field, value)


def entry_case(field, value, rehash=None):
    obj = copy.deepcopy(original)
    entry = next(iter(obj["manifest"]["entries"].values()))
    entry[field] = value
    if rehash:
        data = bytes(value) if isinstance(value, list) else encode(value, sort=True)
        entry[rehash] = hashlib.sha256(data).hexdigest()
    cases.append((0, seal(obj)))


for field, value in [
    ("function", "wrong"),
    ("hook", 3),
    ("dependencies", [1]),
    ("helpers", [2, 1]),
    ("helpers", [1, 1]),
    ("helpers", [1]),
    ("helpers", [5]),
    ("signature_hash", "0" * 64),
    ("elf_hash", "0" * 64),
    ("frame_schema_hash", "0" * 64),
    ("frame_hash", "0" * 64),
    ("elf", [256]),
    ("frame_header", None),
    ("extra", 0),
]:
    entry_case(field, value)
entry = next(iter(original["manifest"]["entries"].values()))
schema = entry["frame_schema"].copy()
schema[0] ^= 1
entry_case("frame_schema", schema, "frame_schema_hash")
elf = entry["elf"].copy()
elf[0] ^= 1
entry_case("elf", elf, "elf_hash")
signature = copy.deepcopy(entry["signature"])
signature["parameters"][0] = "Bool"
entry_case("signature", signature, "signature_hash")
obj = copy.deepcopy(original)
obj["manifest"]["entries"]["program::record"] = []
cases.append((0, seal(obj)))
# Generic serde_json values exercise uint64, signed limits, escaping and Unicode.
for value in [18446744073709551615, -9223372036854775808, "é😀/\b\f\n\r\t\x01"]:
    obj = copy.deepcopy(original)
    obj["manifest"]["typed_ir"] = {"value": value}
    cases.append((1, seal(obj)))
args.output.parent.mkdir(parents=True, exist_ok=True)
with args.output.open("wb") as output:
    for accepted, data in cases:
        output.write(struct.pack("<II", accepted, len(data)))
        output.write(data)
print(f"{len(cases)} package cases -> {args.output}")
