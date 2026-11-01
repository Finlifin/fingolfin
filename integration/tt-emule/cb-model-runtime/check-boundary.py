#!/usr/bin/env python3
"""Synthetic tests for the two CB adapter frame layouts and scalar validation."""

import argparse, json, os, subprocess, sys
from pathlib import Path

p = argparse.ArgumentParser(description=__doc__)
p.add_argument("--artifacts", type=Path, required=True)
p.add_argument("--output", type=Path, required=True)
a = p.parse_args()
art = a.artifacts.resolve()
out = a.output.resolve()
out.mkdir(parents=True, exist_ok=False)
code = """import ctypes,json,sys
lib=ctypes.CDLL(sys.argv[1]); f=lib.fg_tt_scope_event
f.argtypes=[ctypes.c_uint32]*4+[ctypes.c_uint64]*3; f.restype=None
for event in json.loads(sys.argv[2]): f(*event)
"""
results = []
for case in ("metadata", "invalid_phase", "wide_pages", "ignored"):
    env = os.environ.copy()
    events = []
    for hook in range(1, 7):
        env[f"FG_CB_MODEL_PACKAGE_{hook}"] = str(art / str(hook) / "push.fgo")
        env[f"FG_CB_MODEL_REPORT_{hook}"] = str(out / f"{case}-{hook}.json")
        if case == "metadata":
            events += [[hook, phase, 0, 0, 0, b, 0] for b in (0, 7) for phase in (0, 1)]
        elif case == "invalid_phase":
            events.append([hook, 2, 0, 0, 0, 1, 0])
        elif case == "wide_pages":
            events.append([hook, 0, 0, 0, 0, 4294967296, 0])
        else:
            events += [[hook, 0, 1, 0, 0, 1, 0], [hook, 0, 0, 0, 1, 1, 0]]
    subprocess.run(
        [sys.executable, "-c", code, str(art / "runtime.so"), json.dumps(events)],
        env=env,
        check=True,
    )
    for hook in range(1, 7):
        actual = json.loads((out / f"{case}-{hook}.json").read_text())
        count = 4 if case == "metadata" else int(case == "wide_pages" and hook >= 5)
        errors = int(case == "invalid_phase" or (case == "wide_pages" and hook <= 4))
        matched = 2 if case == "metadata" and hook <= 4 else count
        for key, value in dict(
            selected=count, dispatches=count, records=count, errors=errors, matched_args=matched
        ).items():
            if actual[key] != value:
                raise RuntimeError(f"{case}/{hook}: {key}: {actual[key]} != {value}")
        results.append(dict(case=case, hook=hook, passed=True, **actual))
(out / "summary.json").write_text(json.dumps(results, indent=2) + "\n")
print(out / "summary.json")
