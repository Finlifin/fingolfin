#!/usr/bin/env python3
"""Synthetic adapter boundary tests; these are not real emulator scenarios."""

import argparse
import json
import os
from pathlib import Path
import subprocess
import sys

p = argparse.ArgumentParser(description=__doc__)
p.add_argument("--artifacts", type=Path, required=True)
p.add_argument("--output", type=Path, required=True)
a = p.parse_args()
out = a.output.resolve()
out.mkdir(parents=True, exist_ok=False)
art = a.artifacts.resolve()
code = """import ctypes,json,sys
lib=ctypes.CDLL(sys.argv[1]); callback=lib.fg_tt_scope_event
callback.argtypes=[ctypes.c_uint32]*4+[ctypes.c_uint64]*3
callback.restype=None
for args in json.loads(sys.argv[2]): callback(*args)
"""
cases = {
    "metadata": (
        [
            [2, 0, 0, 0, 0, 0, 0],
            [2, 1, 0, 0, 0, 0, 0],
            [2, 0, 0, 0, 0, 7, 0],
            [2, 1, 0, 0, 0, 7, 0],
        ],
        {"errors": 0, "selected": 4, "matched_args": 2, "records": 4, "sequence_sum": 10},
    ),
    "ignored": (
        [[1, 0, 0, 0, 0, 1, 0], [2, 0, 1, 0, 0, 1, 0], [2, 0, 0, 0, 1, 1, 0]],
        {"errors": 0, "selected": 0, "dispatches": 0, "records": 0},
    ),
    "invalid_phase": ([[2, 2, 0, 0, 0, 1, 0]], {"errors": 1, "selected": 0, "dispatches": 0}),
    "wide_pages": ([[2, 0, 0, 0, 0, 4294967296, 0]], {"errors": 1, "selected": 0, "dispatches": 0}),
}
results = []
for name, (events, expected) in cases.items():
    report = out / f"{name}.json"
    env = os.environ.copy()
    env.update(FG_MODEL_PACKAGE=str(art / "push.fgo"), FG_MODEL_REPORT=str(report))
    subprocess.run(
        [sys.executable, "-c", code, str(art / "runtime.so"), json.dumps(events)],
        env=env,
        check=True,
    )
    actual = json.loads(report.read_text())
    for key, value in expected.items():
        if actual[key] != value:
            raise RuntimeError(f"{name}: {key}: {actual[key]} != {value}")
    results.append({"case": name, "passed": True, "report": actual})
(out / "summary.json").write_text(json.dumps(results, indent=2) + "\n")
print(out / "summary.json")
