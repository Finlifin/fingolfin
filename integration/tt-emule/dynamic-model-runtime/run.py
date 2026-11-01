#!/usr/bin/env python3
"""Verify all 39 hooks with per-object Mounts and real launch teardown."""

import argparse, collections, json, os, subprocess, sys
from pathlib import Path

sys.path.insert(0, str(Path(__file__).resolve().parents[1] / "dynamic-model-runtime"))
from treecheck import verify_tree

ROOT = Path(__file__).resolve().parents[3]
p = argparse.ArgumentParser(description=__doc__)
for name in ("artifacts", "metal", "build", "output"):
    p.add_argument("--" + name, type=Path, required=True)
p.add_argument(
    "--reference", type=Path, default=ROOT / "reports/tt-emule-all-model-runtime-1/reference"
)
a = p.parse_args()
out = a.output.resolve()
out.mkdir(parents=True, exist_ok=False)
art = a.artifacts.resolve()
summary = []
hits = set()
manifest = {p["hook"]: p for p in json.loads((art / "manifest.json").read_text())}
cases = [
    ("cb", "CB_Boundary_Wraparound_NoViolation"),
    ("copy", "TensixTestSingleCoreMultiTileBankedDramReaderDataCopyDramWriter"),
    ("noc", "Fingolfin_NocTransferAndSemaphore_Observed"),
    ("arithmetic", "Fingolfin_ArithmeticAndActivation_Observed"),
    ("layout", "Fingolfin_LayoutRoundTrip_Observed"),
    ("multicast", "Fingolfin_MulticastTwoReceivers_Observed"),
    ("repeat", "Fingolfin_RepeatedLaunchIdentity_Observed"),
    ("exception", "Fingolfin_LaunchExceptionRecovery_Observed"),
]
for label, test in cases:
    report = out / f"{label}.json"
    env = os.environ.copy()
    env.update(
        FG_DYNAMIC_PACKAGES=str(art),
        FG_DYNAMIC_REPORT=str(report),
        LD_PRELOAD=str(art / "runtime.so"),
        TT_EMULE_JIT_CACHE_DIR=str(out / f"{label}-jit"),
    )
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
            f"MeshDeviceFixture.{test}",
            "--timeout",
            "30",
        ],
        env=env,
        check=True,
    )
    data = json.loads(report.read_text())
    verify_tree(data)
    launches = 2 if label in ("repeat", "exception") else 6 if label == "copy" else 1
    if (
        data["active_launch"]
        or data["live_mounts"]
        or data["skipped_events"]
        or data["discarded_records"]
        or data["history_dropped"]
        or data["errors"]
        or data["begins"] != launches
        or data["ends"] != launches
        or data["events"] != data["dispatches"]
        or data["detached_routes"] != len(data["bindings"])
    ):
        raise RuntimeError(f"{label}: invalid lifetime/dispatch evidence")
    ref = json.loads(
        (
            a.reference
            / ("noc-trace.json" if label in ("repeat", "exception") else f"{label}-trace.json")
        ).read_text()
    )
    if ref["dropped"] or not ref["events"]:
        raise RuntimeError("invalid reference")
    expected = collections.Counter(
        (e["hook"], e["x"], e["y"], int(e["args"][0]) if e["hook"] <= 6 else 0)
        for e in ref["events"]
    )
    if data["events"] != len(ref["events"]) * (2 if label in ("repeat", "exception") else 1):
        raise RuntimeError(f"{label}: wrong event count")
    aggregate = collections.Counter()
    for launch in range(1, launches + 1):
        rows = [b for b in data["bindings"] if b["launch"] == launch]
        actual = collections.Counter()
        for b in rows:
            actual[b["hook"], b["x"], b["y"], b["local"]] += b["dispatches"]
            hits.add(b["hook"])
            point = manifest[b["hook"]]
            multiplier = (
                1
                if point["group"] == "cb"
                else (
                    len(point["slots"]) + 1
                    if point["group"] == "compute"
                    else point["records_per_event"]
                )
            )
            if b["records"] != b["dispatches"] * multiplier:
                raise RuntimeError("record count mismatch")
        if not rows:
            raise RuntimeError(f"{label}/{launch}: empty launch")
        aggregate.update(actual)
        if label != "copy" and actual != expected:
            raise RuntimeError(f"{label}/{launch}: object/hook count mismatch")
    if label == "copy" and aggregate != expected:
        raise RuntimeError("copy: object/hook count mismatch across six launches")
    if label in ("repeat", "exception"):
        old = {(b["hook"], b["slot"]): b for b in data["bindings"] if b["launch"] == 1}
        new = {(b["hook"], b["slot"]): b for b in data["bindings"] if b["launch"] == 2}
        if old.keys() != new.keys() or data["stale_routes"] != len(new):
            raise RuntimeError("missing stale route rejection")
        if any(
            new[k]["generation"] != old[k]["generation"] + 1
            or new[k]["dispatches"] != old[k]["dispatches"]
            for k in old
        ):
            raise RuntimeError("generation/state isolation failed")
    summary.append(dict(scenario=label, passed=True, **data))
if hits != set(range(1, 40)):
    raise RuntimeError("incomplete 39-hook coverage")
(out / "summary.json").write_text(json.dumps(summary, indent=2) + "\n")
print(out / "summary.json")
