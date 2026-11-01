#!/usr/bin/env python3
"""Compile test fixtures offline; runtime CMake never invokes Rust/Cargo."""

import argparse
import json
import subprocess
from pathlib import Path

ROOT = Path(__file__).resolve().parents[2]
p = argparse.ArgumentParser(description=__doc__)
p.add_argument("--compiler", type=Path, required=True)
p.add_argument("--output", type=Path, required=True)
a = p.parse_args()
a.output.mkdir(parents=True, exist_ok=False)
with (a.output / "commands.log").open("w") as log:
    for name, folder, function in [
        ("record", "state-helpers", "record"),
        ("aggregate", "aggregate-helpers", "aggregate"),
    ]:
        package = a.output / (name + ".fgo")
        cmd = [
            str(a.compiler.resolve()),
            "build",
            str(ROOT / "integration" / folder / (name + ".fg")),
            "--model",
            str(ROOT / "models/fixture"),
            "--function",
            "program::" + function,
            "--output",
            str(package.resolve()),
        ]
        log.write(json.dumps(cmd) + "\n")
        log.flush()
        result = subprocess.run(cmd, capture_output=True, text=True, check=True)
        log.write(result.stdout + result.stderr)
        subprocess.run(
            [str(a.compiler.resolve()), "explain", str(package.resolve())],
            check=True,
            stdout=log,
            stderr=log,
        )
        entry = json.loads(package.read_text())["manifest"]["entries"]["program::" + function]
        (a.output / (name + ".o")).write_bytes(bytes(entry["elf"]))
        (a.output / (name + ".h")).write_text(entry["frame_header"])
        (a.output / (name + ".schema")).write_bytes(bytes(entry["frame_schema"]))

        subprocess.run(
            [
                str(a.compiler.resolve()),
                "emit-adapter",
                str(ROOT / "integration" / folder / (name + ".fg")),
                "--model",
                str(ROOT / "models/fixture"),
                "--function",
                "program::" + function,
                "--output",
                str((a.output / (name + ".adapter.h")).resolve()),
            ],
            check=True,
            stdout=log,
            stderr=log,
        )
