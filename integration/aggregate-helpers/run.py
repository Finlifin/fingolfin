#!/usr/bin/env python3
"""Build .fg map/histogram helpers and execute in pinned uBPF safe interpreter."""

import argparse
import json
from pathlib import Path
import subprocess
import tomllib

ROOT = Path(__file__).resolve().parents[2]


def main():
    p = argparse.ArgumentParser(description=__doc__)
    for name in ("ubpf", "build", "output"):
        p.add_argument("--" + name, type=Path, required=True)
    a = p.parse_args()
    a.output.mkdir(parents=True, exist_ok=False)
    with (a.output / "commands.log").open("w") as log:

        def run(cmd):
            cmd = list(map(str, cmd))
            log.write(json.dumps(cmd) + "\n")
            log.flush()
            result = subprocess.run(cmd, cwd=ROOT, text=True, capture_output=True)
            log.write(result.stdout + result.stderr)
            log.flush()
            if result.returncode:
                raise RuntimeError(f"{cmd}: {result.stderr}")
            return result.stdout

        pin = tomllib.loads((ROOT / "integration/ubpf/upstream.lock.toml").read_text())["ubpf"][
            "revision"
        ]
        assert run(["git", "-C", a.ubpf, "rev-parse", "HEAD"]).strip() == pin
        run(["cargo", "build", "--offline"])
        cli = ROOT / "target/debug/fingolfin"
        run(
            [
                cli,
                "build",
                ROOT / "integration/aggregate-helpers/aggregate.fg",
                "--model",
                ROOT / "models/fixture",
                "--function",
                "program::aggregate",
                "--output",
                a.output / "program.fgo",
            ]
        )
        run([cli, "explain", a.output / "program.fgo"])
        entry = json.loads((a.output / "program.fgo").read_text())["manifest"]["entries"][
            "program::aggregate"
        ]
        assert entry["helpers"] == [3, 4]
        (a.output / "frame.h").write_text(entry["frame_header"])
        (a.output / "program.o").write_bytes(bytes(entry["elf"]))
        run(
            [
                "clang",
                "-std=c11",
                "-Wall",
                "-Wextra",
                "-Werror",
                "-I",
                a.output,
                "-I",
                a.ubpf / "vm/inc",
                "-I",
                a.build / "vm",
                ROOT / "integration/aggregate-helpers/runner.c",
                a.build / "lib/libubpf.a",
                "-lm",
                "-o",
                a.output / "runner",
            ]
        )
        result = run([a.output / "runner", a.output / "program.o"])
        assert result.startswith("PASS:")
        (a.output / "summary.json").write_text(
            json.dumps(
                {"ubpf_revision": pin, "status": "passed", "result": result.strip()}, indent=2
            )
            + "\n"
        )
        print(result.strip())


if __name__ == "__main__":
    main()
