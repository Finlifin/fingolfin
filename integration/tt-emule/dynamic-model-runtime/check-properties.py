#!/usr/bin/env python3
"""Compile a real .fg pattern and test the C tree provider against its gate."""

import argparse, json, subprocess
from pathlib import Path

ROOT = Path(__file__).resolve().parents[3]
source = Path(__file__).resolve().parent
p = argparse.ArgumentParser(description=__doc__)
for name in ("compiler", "runtime-build", "output"):
    p.add_argument("--" + name, type=Path, required=True)
a = p.parse_args()
out = a.output.resolve()
out.mkdir(parents=True, exist_ok=False)
commands = [
    [
        str(a.compiler.resolve()),
        "emit-adapter",
        str(source / "property-probe.fg"),
        "--model",
        str(ROOT / "models/tt_emule"),
        "--function",
        "program::observe",
        "--output",
        str(out / "probe.adapter.h"),
    ],
    [
        "cc",
        "-std=c11",
        "-Wall",
        "-Wextra",
        "-Werror",
        "-I" + str(out),
        "-I" + str(source),
        "-I" + str(ROOT / "runtime/include"),
        "-I" + str(ROOT / "sdk/include"),
        str(source / "properties-test.c"),
        str(source / "properties.c"),
        str(ROOT / "runtime/src/objects.c"),
        str(a.runtime_build.resolve() / "libfingolfin.a"),
        "-pthread",
        "-o",
        str(out / "properties-test"),
    ],
    [str(out / "properties-test")],
]
with (out / "test.log").open("w") as log:
    for cmd in commands:
        log.write(json.dumps(cmd) + "\n")
        log.flush()
        subprocess.run(cmd, stdout=log, stderr=subprocess.STDOUT, check=True)
(out / "summary.json").write_text(json.dumps(dict(passed=True, commands=commands), indent=2) + "\n")
print(out / "summary.json")
