#!/usr/bin/env python3
"""Reproduce the pinned Clang/eBPF/uBPF spike; source checkout is supplied explicitly."""

import argparse
from pathlib import Path
import subprocess
import tomllib


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("--ubpf", type=Path, required=True)
    parser.add_argument("--build", type=Path, required=True)
    parser.add_argument("--clang", default="clang")
    parser.add_argument("--log", type=Path, required=True)
    args = parser.parse_args()
    root = Path(__file__).resolve().parent
    pin = tomllib.loads((root / "upstream.lock.toml").read_text())["ubpf"]["revision"]
    actual = subprocess.check_output(
        ["git", "-C", str(args.ubpf), "rev-parse", "HEAD"], text=True
    ).strip()
    if actual != pin:
        parser.error(f"uBPF revision mismatch: {actual}")
    args.build.mkdir(parents=True, exist_ok=True)
    args.log.parent.mkdir(parents=True, exist_ok=True)
    with args.log.open("w") as log:

        def run(command):
            command = [str(arg) for arg in command]
            log.write(f"Command: {command!r}\n")
            log.flush()
            subprocess.run(command, stdout=log, stderr=subprocess.STDOUT, check=True)

        run(
            [
                "cmake",
                "-S",
                args.ubpf,
                "-B",
                args.build,
                "-G",
                "Ninja",
                "-DUBPF_ENABLE_TESTS=OFF",
                "-DUBPF_SKIP_EXTERNAL=ON",
                "-DUBPF_ENABLE_INSTALL=OFF",
                "-DUBPF_INSTALL_GIT_HOOKS=OFF",
                "-DCMAKE_BUILD_TYPE=Release",
            ]
        )
        run(["cmake", "--build", args.build, "-j", "2"])
        for source, output, flags in [
            ("program.bpf.c", "program.o", []),
            ("oob.bpf.c", "oob.o", []),
            ("program.bpf.c", "named.o", ["-DFG_NAMED_HELPER"]),
        ]:
            run(
                [
                    args.clang,
                    "-target",
                    "bpfel",
                    "-mcpu=v3",
                    "-O2",
                    "-g",
                    *flags,
                    "-c",
                    root / source,
                    "-o",
                    args.build / output,
                ]
            )
        run(
            [
                args.clang,
                "-std=c11",
                "-Wall",
                "-Wextra",
                "-Werror",
                "-I",
                args.ubpf / "vm/inc",
                "-I",
                args.build / "vm",
                root / "probe.c",
                args.build / "lib/libubpf.a",
                "-lm",
                "-o",
                args.build / "probe",
            ]
        )
        run(
            [
                args.build / "probe",
                args.build / "program.o",
                args.build / "oob.o",
                args.build / "named.o",
            ]
        )
    print(f"Probe passed; results and known limitation in {args.log}")


if __name__ == "__main__":
    main()
