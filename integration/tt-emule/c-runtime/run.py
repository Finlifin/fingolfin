#!/usr/bin/env python3
"""Validate the real CB -> SDK -> uBPF generated .fg / C runtime integration against gtest results."""

import argparse
import hashlib
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
        "--probe", type=Path, required=True, help="Directory containing runtime.so and record.fgo"
    )
    parser.add_argument("--output", type=Path, required=True)
    args = parser.parse_args()
    output = args.output.resolve()
    output.mkdir(parents=True, exist_ok=False)
    artifacts = {
        name: hashlib.sha256((args.probe.resolve() / name).read_bytes()).hexdigest()
        for name in ["runtime.so", "record.fgo", "record.adapter.h"]
    }
    (output / "artifacts.json").write_text(json.dumps(artifacts, indent=2))
    baseline = Path(__file__).resolve().parents[1] / "baseline.py"
    results = []
    for label, test, counts in [
        ("exact", "MeshDeviceFixture.CB_Reservation_ExactCapacity_NoViolation", (1, 2, 0)),
        ("wrap", "MeshDeviceFixture.CB_Boundary_Wraparound_NoViolation", (5, 4, 2)),
    ]:
        for mode in ["on", "off", "miss", "dual", "toggle"]:
            destination = output / f"{label}-{mode}"
            env = os.environ.copy()
            env.update(
                {
                    "LD_PRELOAD": str(args.probe.resolve() / "runtime.so"),
                    "FG_CB_PACKAGE": str(args.probe.resolve() / "record.fgo"),
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
            active = (
                seen
                if mode in ["on", "dual"]
                else (3 if label == "wrap" else 1) if mode == "toggle" else 0
            )
            multiplier = 2 if mode == "dual" else 1
            expected_push = (
                push
                if mode in ["on", "dual"]
                else (3 if label == "wrap" else 2) if mode == "toggle" else 0
            )
            expected_pop = (
                pop
                if mode in ["on", "dual"]
                else (1 if label == "wrap" else 0) if mode == "toggle" else 0
            )
            generation = 2 if mode == "off" else 3 if mode == "toggle" and label == "wrap" else 1
            expected = {
                "seen": seen,
                "frames": active,
                "dispatches": active * multiplier,
                "records": active * multiplier,
                "errors": 0,
                "push_pages": expected_push * multiplier,
                "pop_pages": expected_pop * multiplier,
                "route_generations": [generation, generation],
            }
            if observed != expected:
                raise RuntimeError(f"{label}-{mode}: expected {expected}, got {observed}")
            results.append({"case": label, "mode": mode, **observed})
            (output / "probe-summary.json").write_text(json.dumps(results, indent=2))
            print(f"PASS {label}-{mode}: {observed}", flush=True)


if __name__ == "__main__":
    main()
