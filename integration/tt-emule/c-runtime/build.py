#!/usr/bin/env python3
"""Build the generated .fg / pure-C CB integration from prepared PIC libraries."""

import argparse
import hashlib
import json
from pathlib import Path
import subprocess

ROOT = Path(__file__).resolve().parents[3]
p = argparse.ArgumentParser(description=__doc__)
p.add_argument("--compiler", type=Path, required=True)
p.add_argument("--runtime-build", type=Path, required=True)
p.add_argument("--ubpf-build", type=Path, required=True)
p.add_argument("--output", type=Path, required=True)
a = p.parse_args()
output = a.output.resolve()
output.mkdir(parents=True, exist_ok=False)
source = Path(__file__).resolve().parent
commands = [
    [
        str(a.compiler.resolve()),
        "build",
        str(source / "record.fg"),
        "--model",
        str(ROOT / "models/fixture"),
        "--function",
        "program::record",
        "--output",
        str(output / "record.fgo"),
    ],
    [
        str(a.compiler.resolve()),
        "emit-adapter",
        str(source / "record.fg"),
        "--model",
        str(ROOT / "models/fixture"),
        "--function",
        "program::record",
        "--output",
        str(output / "record.adapter.h"),
    ],
    [
        "cc",
        "-shared",
        "-fPIC",
        "-std=c11",
        "-Wall",
        "-Wextra",
        "-Werror",
        "-I",
        str(ROOT / "agent/include"),
        "-I",
        str(ROOT / "runtime/include"),
        "-I",
        str(ROOT / "sdk/include"),
        "-I",
        str(output),
        str(source / "runtime.c"),
        str(a.runtime_build.resolve() / "libfingolfin_agent.a"),
        str(a.runtime_build.resolve() / "libfingolfin.a"),
        str(a.ubpf_build.resolve() / "lib/libubpf.a"),
        "-ljson-c",
        "-lcrypto",
        "-pthread",
        "-lm",
        "-o",
        str(output / "runtime.so"),
    ],
]
with (output / "build.log").open("w") as log:
    for command in commands:
        log.write(json.dumps(command) + "\n")
        log.flush()
        subprocess.run(command, check=True, stdout=log, stderr=subprocess.STDOUT)
paths = [
    output / "record.fgo",
    output / "record.adapter.h",
    output / "runtime.so",
    source / "record.fg",
    source / "runtime.c",
    a.runtime_build.resolve() / "libfingolfin_agent.a",
    a.runtime_build.resolve() / "libfingolfin.a",
    a.ubpf_build.resolve() / "lib/libubpf.a",
]
(output / "artifacts.json").write_text(
    json.dumps(
        {str(path): hashlib.sha256(path.read_bytes()).hexdigest() for path in paths}, indent=2
    )
)
print(output)
