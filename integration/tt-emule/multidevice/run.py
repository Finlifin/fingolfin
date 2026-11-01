#!/usr/bin/env python3
"""Verify two-device identity and a second run using the same kernel cache."""

import argparse, hashlib, json, os, subprocess, sys
from pathlib import Path

sys.path.insert(0, str(Path(__file__).resolve().parents[1] / "dynamic-model-runtime"))
from treecheck import verify_tree

ROOT = Path(__file__).resolve().parents[3]
p = argparse.ArgumentParser(description=__doc__)
for name in ("artifacts", "metal", "build", "output"):
    p.add_argument("--" + name, type=Path, required=True)
p.add_argument("--cache", type=Path)
p.add_argument("--property-gate", action="store_true")
p.add_argument("--expected-device", type=int, choices=(0, 1), default=0)
a = p.parse_args()
out = a.output.resolve()
out.mkdir(parents=True, exist_ok=False)
art = a.artifacts.resolve()
cache = (a.cache or out / "jit").resolve()
if not (art / "runtime.so").is_file():
    raise RuntimeError("dynamic runtime library missing")


def snapshot():
    return {
        p.name: dict(
            sha256=hashlib.sha256(p.read_bytes()).hexdigest(), mtime_ns=p.stat().st_mtime_ns
        )
        for p in cache.glob("*.so")
    }


initial = snapshot()
summary = []
previous = None
for label in ("first", "cached"):
    report = out / f"{label}.json"
    env = os.environ.copy()
    env.update(
        FG_DYNAMIC_PACKAGES=str(art),
        FG_DYNAMIC_REPORT=str(report),
        LD_PRELOAD=str(art / "runtime.so"),
        TT_EMULE_JIT_CACHE_DIR=str(cache),
    )
    env["FG_DYNAMIC_PROPERTY_GATE"] = "1" if a.property_gate else "0"
    subprocess.run(
        [
            sys.executable,
            str(ROOT / "integration/tt-emule/baseline.py"),
            "--metal",
            str(a.metal.resolve()),
            "--build",
            str(a.build.resolve()),
            "--output",
            str(out / f"{label}-test"),
            "--filter",
            "MeshDeviceFixture.Fingolfin_TwoDeviceIdentity_Observed",
            "--cluster-descriptor",
            str(
                a.metal.resolve()
                / "tt_metal/third_party/umd/tests/cluster_descriptor_examples/wormhole_N300.yaml"
            ),
            "--timeout",
            "60",
        ],
        env=env,
        check=True,
    )
    data = json.loads(report.read_text())
    verify_tree(data)
    for key, value in dict(
        begins=2,
        ends=2,
        events=48,
        dispatches=24 if a.property_gate else 48,
        records=24 if a.property_gate else 48,
        filtered=24 if a.property_gate else 0,
        property_gate=int(a.property_gate),
        errors=0,
        stale_routes=12,
        detached_routes=24,
        active_launch=0,
        live_mounts=0,
        skipped_events=0,
        discarded_records=0,
        history_dropped=0,
    ).items():
        if data[key] != value:
            raise RuntimeError(f"{label}: {key}: {data[key]} != {value}")
    for launch in (1, 2):
        rows = [b for b in data["bindings"] if b["launch"] == launch]
        if len(rows) != 12 or {(b["device"], b["hook"]) for b in rows} != {
            (d, h) for d in (0, 1) for h in range(1, 7)
        }:
            raise RuntimeError("missing device/hook")
        if any(
            b["matched_context"]
            != (2 if b["device"] == 0 and (not a.property_gate or a.expected_device == 0) else 0)
            for b in rows
        ):
            raise RuntimeError("typed device predicate mismatch")
        keys = {d: {(b["slot"], b["generation"]) for b in rows if b["device"] == d} for d in (0, 1)}
        if any(len(k) != 1 for k in keys.values()) or keys[0] & keys[1]:
            raise RuntimeError("devices share object identity")
        if any(
            b["dispatches"] != (0 if a.property_gate and b["device"] != a.expected_device else 2)
            or b["records"] != (0 if a.property_gate and b["device"] != a.expected_device else 2)
            or b["filtered"] != (2 if a.property_gate and b["device"] != a.expected_device else 0)
            or b["generation"] != launch
            or b["x"]
            or b["y"]
            or b["local"]
            for b in rows
        ):
            raise RuntimeError("device-local state mismatch")
    now = snapshot()
    if not now:
        raise RuntimeError("missing cache artifacts")
    if previous is not None and now != previous:
        raise RuntimeError("cached run rebuilt or changed DSOs")
    previous = now
    summary.append(
        dict(
            run=label,
            expected_device=a.expected_device if a.property_gate else None,
            passed=True,
            **data,
        )
    )
(out / "summary.json").write_text(json.dumps(summary, indent=2) + "\n")
(out / "cache.json").write_text(
    json.dumps(dict(initial=initial, final=previous, warm_unchanged=True), indent=2) + "\n"
)
print(out / "summary.json")
