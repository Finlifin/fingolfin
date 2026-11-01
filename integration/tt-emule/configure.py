#!/usr/bin/env python3
"""Configure the pinned C++ emulator integration (Python 3.11+)."""

import argparse
from pathlib import Path
import subprocess
import sys
import tomllib


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("--metal", type=Path, required=True)
    parser.add_argument("--emule", type=Path, required=True)
    parser.add_argument("--build", type=Path, required=True)
    parser.add_argument("--cc", default="clang-20")
    parser.add_argument("--cxx", default="clang++-20")
    parser.add_argument("--log", type=Path, required=True)
    parser.add_argument(
        "--sfpi-root", type=Path, help="Portable SFPI parent; requires portable-sfpi.patch"
    )
    args = parser.parse_args()
    pins = tomllib.loads(Path(__file__).with_name("upstream.lock.toml").read_text())
    for name, directory in [("tt_metal", args.metal), ("tt_emule", args.emule)]:
        actual = subprocess.check_output(
            ["git", "-C", str(directory), "rev-parse", "HEAD"], text=True
        ).strip()
        if actual != pins[name]["revision"]:
            parser.error(f"{name} revision mismatch: {actual}")
    for name in ["umd", "tracy"]:
        directory = args.metal / "tt_metal" / "third_party" / name
        actual = subprocess.check_output(
            ["git", "-C", str(directory), "rev-parse", "HEAD"], text=True
        ).strip()
        if actual != pins[name]["revision"]:
            parser.error(f"{name} submodule revision mismatch: {actual}")
    command = [
        "cmake",
        "-S",
        str(args.metal.resolve()),
        "-B",
        str(args.build.resolve()),
        "-G",
        "Ninja",
        f"-DCMAKE_C_COMPILER={args.cc}",
        f"-DCMAKE_CXX_COMPILER={args.cxx}",
        "-DCMAKE_LINKER_TYPE=LLD",
        "-DCMAKE_BUILD_TYPE=Release",
        "-DTT_METAL_USE_EMULE=ON",
        f"-DTT_EMULE_PATH={args.emule.resolve()}",
        "-DWITH_PYTHON_BINDINGS=OFF",
        "-DTT_METAL_BUILD_TESTS=ON",
        "-DTTNN_BUILD_TESTS=OFF",
        "-DENABLE_TRACY=OFF",
        "-DENABLE_DISTRIBUTED=OFF",
        "-DTT_INSTALL=OFF",
    ]
    if args.sfpi_root:
        cmake_source = (args.metal / "tt_metal/hw/CMakeLists.txt").read_text()
        if "if(FINGOLFIN_SFPI_ROOT)" not in cmake_source:
            parser.error("apply integration/tt-emule/portable-sfpi.patch before using --sfpi-root")
        if not (args.sfpi_root / "sfpi/compiler/bin/riscv-tt-elf-g++").is_file():
            parser.error("portable SFPI compiler is missing")
        command.append(f"-DFINGOLFIN_SFPI_ROOT={args.sfpi_root.resolve()}")
    args.log.parent.mkdir(parents=True, exist_ok=True)
    with args.log.open("w") as log:
        log.write(f"Command: {command!r}\n")
        log.flush()
        process = subprocess.Popen(
            command, stdout=subprocess.PIPE, stderr=subprocess.STDOUT, text=True
        )
        assert process.stdout is not None
        for line in process.stdout:
            sys.stdout.write(line)
            sys.stdout.flush()
            log.write(line)
        code = process.wait()
        log.write(f"\nExit code: {code}\n")
    return code


if __name__ == "__main__":
    sys.exit(main())
