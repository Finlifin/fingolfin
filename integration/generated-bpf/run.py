#!/usr/bin/env python3
"""Compile .fg functions and compare safe uBPF execution with reference eval."""

import argparse
import json
from pathlib import Path
import random
import subprocess
import tomllib

ROOT = Path(__file__).resolve().parents[2]


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("--ubpf", type=Path, required=True)
    parser.add_argument("--build", type=Path, required=True)
    parser.add_argument("--output", type=Path, required=True)
    parser.add_argument("--clang", default="clang")
    args = parser.parse_args()
    args.output.mkdir(parents=True, exist_ok=False)
    log = (args.output / "commands.log").open("w")

    def run(command, **kw):
        command = list(map(str, command))
        log.write(json.dumps(command) + "\n")
        log.flush()
        result = subprocess.run(command, cwd=ROOT, text=True, capture_output=True, **kw)
        log.write(result.stdout + result.stderr)
        log.flush()
        if result.returncode:
            raise RuntimeError(f"command failed ({result.returncode}): {command}\n{result.stderr}")
        return result.stdout

    pin = tomllib.loads((ROOT / "integration/ubpf/upstream.lock.toml").read_text())["ubpf"][
        "revision"
    ]
    assert run(["git", "-C", args.ubpf, "rev-parse", "HEAD"]).strip() == pin
    run(["cargo", "build", "--offline"])
    cli = ROOT / "target/debug/fingolfin"
    source = ROOT / "integration/generated-bpf/arithmetic.fg"
    model = ROOT / "models/fixture"
    rng = random.Random(8431)
    summary = []
    for name in ["add", "mul", "div", "rem", "branch", "smul", "sadd", "sub", "short"]:
        folder = args.output / name
        folder.mkdir()
        run(
            [
                cli,
                "build",
                source,
                "--model",
                model,
                "--function",
                f"program::{name}",
                "--clang",
                args.clang,
                "--output",
                folder / "program.fgo",
            ]
        )
        run([cli, "explain", folder / "program.fgo", "--entry", f"program::{name}"])
        entry = json.loads((folder / "program.fgo").read_text())["manifest"]["entries"][
            f"program::{name}"
        ]
        (folder / "frame.h").write_text(entry["frame_header"])
        (folder / "program.o").write_bytes(bytes(entry["elf"]))
        run(
            [
                args.clang,
                "-std=c11",
                "-Wall",
                "-Wextra",
                "-Werror",
                "-I",
                folder,
                "-I",
                args.ubpf / "vm/inc",
                "-I",
                args.build / "vm",
                ROOT / "integration/generated-bpf/runner.c",
                args.build / "lib/libubpf.a",
                "-lm",
                "-o",
                folder / "runner",
            ]
        )
        signed = name in ("div", "rem", "smul", "sadd", "sub", "short")
        cases = [
            (
                rng.randrange(-1000, 1000) if signed else rng.randrange(0, 1000),
                rng.randrange(-1000, 1000) if signed else rng.randrange(0, 1000),
            )
            for _ in range(32)
        ]
        cases += (
            [(-(1 << 63), -1), (-(1 << 63), 1), ((1 << 63) - 1, 1), (-7, 3), (7, -3), (0, 0)]
            if signed
            else [(0, 0), ((1 << 64) - 1, 1), ((1 << 64) - 1, (1 << 64) - 1), (1 << 63, 2)]
        )
        rows = []
        expected = []
        for a, b in cases:
            ref = subprocess.run(
                [
                    str(cli),
                    "eval",
                    str(source),
                    "--model",
                    str(model),
                    "--function",
                    f"program::{name}",
                    "--args",
                    json.dumps([{"Integer": a}, {"Integer": b}]),
                ],
                cwd=ROOT,
                text=True,
                capture_output=True,
            )
            log.write(ref.stdout + ref.stderr)
            if ref.returncode:
                assert "Arithmetic" in ref.stderr or "InvalidValue" in ref.stderr, ref.stderr
                expected.append((2, 2, 0))
            else:
                tagged = json.loads(ref.stdout)["result"]
                value = int(tagged.get("Integer", tagged.get("Bool")))
                expected.append((0, 0, value & ((1 << 64) - 1)))
            rows.append(f"{a & ((1<<64)-1):x} {b & ((1<<64)-1):x} 10000 64 0")
        rows += ["1 1 0 64 0", "1 1 10000 0 0", "1 1 10000 64 1"]
        expected += [(1, 1, 0), (4, 4, 0), (5, 0, 0)]
        output = run([folder / "runner", folder / "program.o"], input="\n".join(rows) + "\n")
        actual = [(int(a), int(b), int(c, 16)) for a, b, c in map(str.split, output.splitlines())]
        assert actual == expected, (name, actual, expected)
        (folder / "cases.json").write_text(
            json.dumps({"inputs": rows, "expected": expected, "actual": actual}, indent=2) + "\n"
        )
        summary.append({"function": name, "cases": len(rows), "status": "passed"})
    (args.output / "summary.json").write_text(
        json.dumps({"ubpf_revision": pin, "results": summary}, indent=2) + "\n"
    )
    print(json.dumps(summary))


if __name__ == "__main__":
    main()
