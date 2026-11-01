#!/usr/bin/env python3
"""Build the C identity observer and verify real launch scopes and reused slots."""

import argparse, hashlib, json, os, subprocess, sys
from pathlib import Path

ROOT = Path(__file__).resolve().parents[3]
source = Path(__file__).resolve().parent
p = argparse.ArgumentParser(description=__doc__)
for name in ("metal", "build", "output"):
    p.add_argument("--" + name, type=Path, required=True)
a = p.parse_args()
out = a.output.resolve()
out.mkdir(parents=True, exist_ok=False)
cmd = [
    "cc",
    "-shared",
    "-fPIC",
    "-std=c11",
    "-Wall",
    "-Wextra",
    "-Werror",
    "-I" + str(ROOT / "runtime/include"),
    "-I" + str(ROOT / "sdk/include"),
    str(source / "runtime.c"),
    str(ROOT / "runtime/src/objects.c"),
    "-pthread",
    "-o",
    str(out / "runtime.so"),
]
subprocess.run(cmd, check=True)
summary = []
for label, test, launches, objects in [
    ("repeat", "Fingolfin_RepeatedLaunchIdentity_Observed", 2, 4),
    ("arithmetic", "Fingolfin_ArithmeticAndActivation_Observed", 1, 4),
    ("multicast", "Fingolfin_MulticastTwoReceivers_Observed", 1, 6),
]:
    report = out / f"{label}.json"
    env = os.environ.copy()
    env.update(
        FG_LIFECYCLE_RUN="71",
        FG_LIFECYCLE_REPORT=str(report),
        LD_PRELOAD=str(out / "runtime.so"),
        TT_EMULE_JIT_CACHE_DIR=str(out / f"{label}-jit"),
    )
    subprocess.run(
        [
            sys.executable,
            str(source.parent / "baseline.py"),
            "--metal",
            str(a.metal.resolve()),
            "--build",
            str(a.build.resolve()),
            "--output",
            str(out / f"{label}-test"),
            "--filter",
            f"MeshDeviceFixture.{test}",
            "--timeout",
            "30",
        ],
        env=env,
        check=True,
    )
    data = json.loads(report.read_text())
    if (
        data["begins"] != launches
        or data["ends"] != launches
        or data["errors"]
        or len(data["objects"]) != objects
        or not data["events"]
    ):
        raise RuntimeError(f"{label}: bad lifecycle evidence: {data}")
    if any(o["run"] != 71 for o in data["objects"]):
        raise RuntimeError("run identity mismatch")
    if label == "repeat":
        if data["events"] != 96 or data["stale_checks"] != 8:
            raise RuntimeError("repeat event/stale checks missing")
        old = {o["slot"]: o for o in data["objects"] if o["launch"] == 1}
        new = {o["slot"]: o for o in data["objects"] if o["launch"] == 2}
        if old.keys() != new.keys() or any(
            new[s]["generation"] != old[s]["generation"] + 1 for s in old
        ):
            raise RuntimeError("slots not reused with a new generation")
    if label == "arithmetic" and {o["kind"] for o in data["objects"]} != {1, 3}:
        raise RuntimeError("CB/compute identity missing")
    if label == "multicast" and {(o["x"], o["y"]) for o in data["objects"]} != {
        (0, 0),
        (1, 0),
        (2, 0),
    }:
        raise RuntimeError("multicore identity missing")
    summary.append(dict(scenario=label, passed=True, **data))
(out / "summary.json").write_text(json.dumps(summary, indent=2) + "\n")
files = [p for p in source.iterdir() if p.is_file()] + [
    ROOT / "runtime/src/objects.c",
    ROOT / "runtime/include/fingolfin/objects.h",
    out / "runtime.so",
]
(out / "provenance.json").write_text(
    json.dumps(
        {
            "command": cmd,
            "sha256": {str(p): hashlib.sha256(p.read_bytes()).hexdigest() for p in files},
        },
        indent=2,
    )
    + "\n"
)
print(out / "summary.json")
