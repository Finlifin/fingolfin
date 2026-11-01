#!/usr/bin/env python3
"""Exercise NOC scalar widths, including every u8 value and high-bit u64 addresses."""

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
for event in json.load(open(sys.argv[2])): f(*event)
"""
for case in (
    "values",
    "wide_a",
    "wide_b",
    "wide_c",
    "byte_a",
    "byte_b",
    "byte_c",
    "phase",
    "ignored",
):
    env = os.environ.copy()
    events = []
    expectations = {}
    for point in manifest:
        hook = point["hook"]
        env[f"FG_NOC_PACKAGE_{hook}"] = str(art / str(hook) / "push.fgo")
        env[f"FG_NOC_REPORT_{hook}"] = str(out / f"{case}-{hook}.json")
        kinds = {slot: kind for (_, kind), slot in zip(point["params"], point["slots"])}
        error = 0
        count = 0
        if case == "values":
            iterations = 256 if "u8" in kinds.values() else 6
            count = iterations * 2
            for n in range(iterations):
                values = [0, 0, 0]
                for slot, kind in kinds.items():
                    if kind == "u8":
                        values[slot] = n
                    elif kind == "u32":
                        values[slot] = [0, 1, 2147483648, 4294967295, 17, 1234567][(n + slot) % 6]
                    else:
                        values[slot] = [
                            0,
                            1,
                            4294967296,
                            9223372036854775808,
                            18446744073709551615,
                            0xFEDCBA9876543210,
                        ][(n + slot) % 6]
                events.extend([[hook, phase, 0, 0, *values] for phase in (0, 1)])
        elif case.startswith(("wide_", "byte_")):
            slot = "abc".index(case[-1])
            value = 4294967296 if case.startswith("wide_") else 256
            error = int(slot in kinds and value > (1 << int(kinds[slot][1:])) - 1)
            count = 1 - error
            values = [0, 0, 0]
            values[slot] = value
            events.append([hook, 0, 0, 0, *values])
        elif case == "phase":
            events.append([hook, 2, 0, 0, 0, 0, 0])
            error = 1
        else:
            events.extend([[hook, 0, 1, 0, 0, 0, 0], [hook, 0, 0, 1, 0, 0, 0]])
        expectations[hook] = (count, error)
    eventfile = out / f"{case}-inputs.json"
    eventfile.write_text(json.dumps(events))
    subprocess.run(
        [sys.executable, "-c", code, str(art / "runtime.so"), str(eventfile)], env=env, check=True
    )
    for point in manifest:
        hook = point["hook"]
        data = json.loads((out / f"{case}-{hook}.json").read_text())
        count, error = expectations[hook]
        for key, value in dict(
            selected=count,
            dispatches=count,
            errors=error,
            matched_args=count,
            records=count * point["records_per_event"],
        ).items():
            if data[key] != value:
                raise RuntimeError(f"{case}/{hook}: {key}: {data[key]} != {value}")
        results.append(dict(case=case, hook=hook, passed=True, **data))
(out / "summary.json").write_text(json.dumps(results, indent=2) + "\n")
print(out / "summary.json")
