#!/usr/bin/env python3
"""Validate the real CB -> SDK -> uBPF technical spike against gtest results."""

import argparse
import json
import os
from pathlib import Path
import subprocess
import sys


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("--metal", type=Path, required=True)
    parser.add_argument("--build", type=Path, required=True)
    parser.add_argument(
        "--probe", type=Path, required=True, help="Directory containing runtime.so and program.o"
    )
    parser.add_argument("--output", type=Path, required=True)
    args = parser.parse_args()
    output = args.output.resolve()
    output.mkdir(parents=True, exist_ok=False)
    baseline = Path(__file__).resolve().parents[1] / "baseline.py"
    results = []
    for label, test, counts in [
        ("exact", "MeshDeviceFixture.CB_Reservation_ExactCapacity_NoViolation", (1, 2, 0)),
        ("wrap", "MeshDeviceFixture.CB_Boundary_Wraparound_NoViolation", (5, 4, 2)),
    ]:
        for mode in ["on", "off", "miss"]:
            destination = output / f"{label}-{mode}"
            env = os.environ.copy()
            env.update(
                {
                    "LD_PRELOAD": str(args.probe.resolve() / "runtime.so"),
                    "FG_CB_BPF": str(args.probe.resolve() / "program.o"),
                    "FG_CB_MODE": mode,
                    "FG_CB_REPORT": str(destination / "probe.json"),
                    "TT_EMULE_JIT_CACHE_DIR": str(destination / "jit-cache"),
                }
            )
            subprocess.run(
                [
                    sys.executable,
                    str(baseline),
                    "--metal",
                    str(args.metal),
                    "--build",
                    str(args.build),
                    "--output",
                    str(destination),
                    "--filter",
                    test,
                ],
                env=env,
                check=True,
            )
            observed = json.loads((destination / "probe.json").read_text())
            seen, push, pop = counts
            expected = {
                "seen": seen,
                "frames": seen if mode == "on" else 0,
                "vm": seen if mode == "on" else 0,
                "errors": 0,
                "push_pages": push if mode == "on" else 0,
                "pop_pages": pop if mode == "on" else 0,
            }
            if observed != expected:
                raise RuntimeError(f"{label}-{mode}: expected {expected}, got {observed}")
            results.append({"case": label, "mode": mode, **observed})
            (output / "probe-summary.json").write_text(json.dumps(results, indent=2))
            print(f"PASS {label}-{mode}: {observed}", flush=True)


if __name__ == "__main__":
    main()
