#!/usr/bin/env python3
"""Verify exact parameter transport, u32 boundaries and core/phase filtering."""

import argparse, json, os, subprocess, sys
from pathlib import Path

p = argparse.ArgumentParser(description=__doc__)
p.add_argument("--artifacts", type=Path, required=True)
p.add_argument("--output", type=Path, required=True)
a = p.parse_args()
out = a.output.resolve()
out.mkdir(parents=True, exist_ok=False)
art = a.artifacts.resolve()
manifest = json.loads((art / "manifest.json").read_text())
results = []
code = """import ctypes,json,sys
lib=ctypes.CDLL(sys.argv[1]); f=lib.fg_tt_scope_event
f.argtypes=[ctypes.c_uint32]*4+[ctypes.c_uint64]*3;f.restype=None
for event in json.loads(sys.argv[2]): f(*event)
"""
for case in ("values", "wide_a", "wide_b", "wide_c", "phase", "ignored"):
    env = os.environ.copy()
    events = []
    for point in manifest:
        hook = point["hook"]
        env[f"FG_COMPUTE_PACKAGE_{hook}"] = str(art / str(hook) / "push.fgo")
        env[f"FG_COMPUTE_REPORT_{hook}"] = str(out / f"{case}-{hook}.json")
        if case == "values":
            events.extend(
                [
                    [hook, phase, 0, 0, *values]
                    for values in ([0, 17, 4294967295], [42, 0, 99])
                    for phase in (0, 1)
                ]
            )
        elif case.startswith("wide_"):
            values = [0, 0, 0]
            values["abc".index(case[-1])] = 4294967296
            events.append([hook, 0, 0, 0, *values])
        elif case == "phase":
            events.append([hook, 2, 0, 0, 0, 0, 0])
        else:
            events.extend([[hook, 0, 1, 0, 0, 0, 0], [hook, 0, 0, 1, 0, 0, 0]])
    subprocess.run(
        [sys.executable, "-c", code, str(art / "runtime.so"), json.dumps(events)],
        env=env,
        check=True,
    )
    for point in manifest:
        hook = point["hook"]
        data = json.loads((out / f"{case}-{hook}.json").read_text())
        fields = len(point["slots"])
        error = int(
            case == "phase"
            or (case.startswith("wide_") and "abc".index(case[-1]) in point["slots"])
        )
        count = 4 if case == "values" else int(case.startswith("wide_") and not error)
        for key, value in dict(
            selected=count,
            dispatches=count,
            errors=error,
            matched_args=count,
            records=count * (fields + 1),
        ).items():
            if data[key] != value:
                raise RuntimeError(f"{case}/{hook}: {key}: {data[key]} != {value}")
        results.append(dict(case=case, hook=hook, passed=True, **data))
(out / "summary.json").write_text(json.dumps(results, indent=2) + "\n")
print(out / "summary.json")
