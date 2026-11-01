#!/usr/bin/env python3
"""Synthetic failure/recovery tests for the dynamic controller, not emulator API tests."""

import argparse, json, os, subprocess, sys
from pathlib import Path

sys.path.insert(0, str(Path(__file__).resolve().parents[1] / "dynamic-model-runtime"))
from treecheck import verify_tree

ROOT = Path(__file__).resolve().parents[3]
source = Path(__file__).resolve().parent
p = argparse.ArgumentParser(description=__doc__)
for name in ("artifacts", "runtime-build", "ubpf-build", "output"):
    p.add_argument("--" + name, type=Path, required=True)
a = p.parse_args()
art = a.artifacts.resolve()
out = a.output.resolve()
out.mkdir(parents=True, exist_ok=False)
objects = [str(art / "policy.o")] + [str(art / str(i) / "adapter.o") for i in range(1, 40)]
limited = out / "limited.so"
cmd = [
    "cc",
    "-shared",
    "-fPIC",
    "-std=c11",
    "-Wall",
    "-Wextra",
    "-Werror",
    "-DFG_DYNAMIC_LIMIT=2",
    "-DFG_DYNAMIC_TESTING",
    "-I" + str(source),
    "-I" + str(ROOT / "agent/include"),
    "-I" + str(ROOT / "runtime/include"),
    "-I" + str(ROOT / "sdk/include"),
    str(art / "table.c"),
    str(source / "runtime.c"),
    str(source / "properties.c"),
    str(ROOT / "runtime/src/objects.c"),
    *objects,
    str(a.runtime_build.resolve() / "libfingolfin_agent.a"),
    str(a.runtime_build.resolve() / "libfingolfin.a"),
    str(a.ubpf_build.resolve() / "lib/libubpf.a"),
    "-ljson-c",
    "-lcrypto",
    "-pthread",
    "-lm",
    "-o",
    str(limited),
]
subprocess.run(cmd, check=True)
code = """import ctypes,os,sys
lib=ctypes.CDLL(sys.argv[1]);case=sys.argv[2];packages=sys.argv[3]
begin=lib.fg_tt_launch_begin;begin.argtypes=[ctypes.c_uint64];begin.restype=ctypes.c_uint64
end=lib.fg_tt_launch_end;end.argtypes=[ctypes.c_uint64];end.restype=None
f=lib.fg_tt_scope_event;f.argtypes=[ctypes.c_uint32]*4+[ctypes.c_uint64]*3;f.restype=None
one=begin(0);assert one==1
f(1,0,0,0,0,1,0)
if case=='busy':
 assert lib.fg_dynamic_test_hold()==0
 end(one)
 assert lib.fg_dynamic_test_retained()==1
 lib.fg_dynamic_test_release()
elif case=='package':
 os.environ['FG_DYNAMIC_PACKAGES']=packages+'/missing'
 f(2,0,0,0,0,1,0)
elif case=='capacity':
 f(2,0,0,0,0,1,0)
 f(3,0,0,0,0,1,0)
else:
 f(2,0,0,0,0,4294967296,0)
# A failed launch skips later observations without repeatedly loading/executing.
if case!='busy':
 f(1,1,0,0,0,1,0);f(2,1,0,0,0,1,0)
end(one)
os.environ['FG_DYNAMIC_PACKAGES']=packages
two=begin(0);assert two==2
f(1,0,0,0,0,1,0);f(1,1,0,0,0,1,0)
end(two)
"""
results = []
for case in ("package", "capacity", "width", "busy"):
    report = out / f"{case}.json"
    env = os.environ.copy()
    env.update(FG_DYNAMIC_PACKAGES=str(art), FG_DYNAMIC_REPORT=str(report))
    library = limited if case in ("capacity", "busy") else art / "runtime.so"
    subprocess.run([sys.executable, "-c", code, str(library), case, str(art)], env=env, check=True)
    data = json.loads(report.read_text())
    verify_tree(data)
    expected_calls = 4 if case == "capacity" else 3
    for key, value in dict(
        begins=2,
        ends=2,
        errors=1,
        skipped_events=0 if case == "busy" else 2,
        active_launch=0,
        live_mounts=0,
        dispatches=expected_calls,
        records=expected_calls,
        discarded_records=0,
        history_dropped=0,
    ).items():
        if data[key] != value:
            raise RuntimeError(f"{case}: {key}: {data[key]} != {value}")
    new = [b for b in data["bindings"] if b["launch"] == 2]
    if (
        len(new) != 1
        or new[0]["dispatches"] != 2
        or new[0]["generation"] != 2
        or data["stale_routes"] < 1
    ):
        raise RuntimeError("recovery reused stale state")
    results.append(dict(case=case, passed=True, **data))
(out / "summary.json").write_text(json.dumps(results, indent=2) + "\n")
print(out / "summary.json")
