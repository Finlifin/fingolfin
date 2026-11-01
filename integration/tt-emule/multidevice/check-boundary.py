#!/usr/bin/env python3
"""Reject unknown device IDs, ambiguous legacy events and duplicate launch members."""

import argparse, json, os, subprocess, sys
from pathlib import Path

sys.path.insert(0, str(Path(__file__).resolve().parents[1] / "dynamic-model-runtime"))
from treecheck import verify_tree

p = argparse.ArgumentParser(description=__doc__)
p.add_argument("--artifacts", type=Path, required=True)
p.add_argument("--output", type=Path, required=True)
a = p.parse_args()
art = a.artifacts.resolve()
out = a.output.resolve()
out.mkdir(parents=True, exist_ok=False)
code = """import ctypes,sys
lib=ctypes.CDLL(sys.argv[1]);case=sys.argv[2]
b=lib.fg_tt_launch_begin_many;b.argtypes=[ctypes.POINTER(ctypes.c_uint64),ctypes.c_size_t];b.restype=ctypes.c_uint64
e=lib.fg_tt_launch_end;e.argtypes=[ctypes.c_uint64];e.restype=None
v=lib.fg_tt_scope_event_v2;v.argtypes=[ctypes.c_uint32,ctypes.c_uint32,ctypes.c_uint64,ctypes.c_uint32,ctypes.c_uint32]+[ctypes.c_uint64]*3;v.restype=None
old=lib.fg_tt_scope_event;old.argtypes=[ctypes.c_uint32]*4+[ctypes.c_uint64]*3;old.restype=None
if case=='wide':
 token=b((ctypes.c_uint64*3)(0,1<<32,1<<63),3);assert token==1
 for device in (0,1<<32,1<<63): v(2,0,device,0,0,0,1,0)
 e(token)
 sys.exit(0)
if case=='duplicate': assert b((ctypes.c_uint64*2)(0,0),2)==0
else:
 token=b((ctypes.c_uint64*2)(0,1),2);assert token==1
 if case=='unknown': v(2,0,2,0,0,0,1,0)
 else: old(2,0,0,0,0,1,0)
 e(token)
# The next valid scope must recover and keep same-coordinate devices separate.
token=b((ctypes.c_uint64*2)(1,0),2);assert token
v(2,0,0,0,0,0,1,0);v(2,0,1,0,0,0,1,0);e(token)
"""
rows = []
for case in ("unknown", "legacy", "duplicate", "wide"):
    report = out / f"{case}.json"
    env = os.environ.copy()
    env.update(FG_DYNAMIC_PACKAGES=str(art), FG_DYNAMIC_REPORT=str(report))
    subprocess.run([sys.executable, "-c", code, str(art / "runtime.so"), case], env=env, check=True)
    data = json.loads(report.read_text())
    verify_tree(data)
    launches = 1 if case in ("duplicate", "wide") else 2
    for key, value in dict(
        begins=launches,
        ends=launches,
        errors=0 if case == "wide" else 1,
        dispatches=3 if case == "wide" else 2,
        records=3 if case == "wide" else 2,
        active_launch=0,
        live_mounts=0,
    ).items():
        if data[key] != value:
            raise RuntimeError(f"{case}: {key} {data[key]} != {value}")
    expected_devices = {0, 1 << 32, 1 << 63} if case == "wide" else {0, 1}
    if {b["device"] for b in data["bindings"]} != expected_devices or len(
        {b["slot"] for b in data["bindings"]}
    ) != len(expected_devices):
        raise RuntimeError("device identity alias")
    if any(b["matched_context"] != int(b["device"] == 0) for b in data["bindings"]):
        raise RuntimeError("typed device value was lost or truncated")
    rows.append(dict(case=case, passed=True, **data))
(out / "summary.json").write_text(json.dumps(rows, indent=2) + "\n")
print(out / "summary.json")
