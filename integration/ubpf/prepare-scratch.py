#!/usr/bin/env python3
"""Create an isolated pinned uBPF checkout with the reviewed scratch patch."""

import argparse
from pathlib import Path
import subprocess

PIN = "8d2dd3580ab80f9b4a79a9d2fa2420aa38fc7521"
p = argparse.ArgumentParser()
p.add_argument("--source", type=Path, required=True)
p.add_argument("--output", type=Path, required=True)
a = p.parse_args()
source = a.source.resolve()
output = a.output.resolve()
if output.exists():
    p.error("output already exists; choose a new directory")
revision = subprocess.check_output(
    ["git", "-C", str(source), "rev-parse", "HEAD"], text=True
).strip()
if revision != PIN:
    p.error("source revision differs from pinned uBPF")
subprocess.run(
    ["git", "clone", "--no-hardlinks", "--no-checkout", str(source), str(output)], check=True
)
subprocess.run(["git", "-C", str(output), "checkout", "--detach", PIN], check=True)
patch = Path(__file__).resolve().parent / "patches" / "safe-scratch-v1.patch"
subprocess.run(["git", "-C", str(output), "apply", "--check", str(patch)], check=True)
subprocess.run(["git", "-C", str(output), "apply", str(patch)], check=True)
print(f"Prepared {PIN} + {patch.name} in {output}")
