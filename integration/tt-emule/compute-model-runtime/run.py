#!/usr/bin/env python3
"""Run compute model bindings and compare counts to independent diagnostic traces."""

import argparse, collections, json, os, subprocess, sys
from pathlib import Path

ROOT = Path(__file__).resolve().parents[3]
p = argparse.ArgumentParser(description=__doc__)
for name in ("artifacts", "metal", "build", "output"):
    p.add_argument("--" + name, type=Path, required=True)
p.add_argument("--reference", type=Path, default=ROOT / "reports/tt-emule-observability-3")
a = p.parse_args()
out = a.output.resolve()
out.mkdir(parents=True, exist_ok=False)
art = a.artifacts.resolve()
manifest = json.loads((art / "manifest.json").read_text())
summary = []
hit = set()
for label, test in [
    ("copy", "TensixTestSingleCoreMultiTileBankedDramReaderDataCopyDramWriter"),
    ("arithmetic", "Fingolfin_ArithmeticAndActivation_Observed"),
    ("layout", "Fingolfin_LayoutRoundTrip_Observed"),
]:
    ref = json.loads((a.reference / f"{label}-trace.json").read_text())
    if ref["dropped"]:
        raise RuntimeError("incomplete reference trace")
    counts = collections.Counter(e["hook"] for e in ref["events"] if e["x"] == 0 and e["y"] == 0)
    env = os.environ.copy()
    env.update(LD_PRELOAD=str(art / "runtime.so"), TT_EMULE_JIT_CACHE_DIR=str(out / f"{label}-jit"))
    for point in manifest:
        hook = point["hook"]
        env[f"FG_COMPUTE_PACKAGE_{hook}"] = str(art / str(hook) / "push.fgo")
        env[f"FG_COMPUTE_REPORT_{hook}"] = str(out / f"{label}-{hook}.json")
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
    for point in manifest:
        hook = point["hook"]
        count = counts[hook]
        data = json.loads((out / f"{label}-{hook}.json").read_text())
        expected = dict(
            selected=count,
            dispatches=count,
            errors=0,
            enter=count // 2,
            exit=count // 2,
            matched_args=count,
            records=count * (len(point["slots"]) + 1),
        )
        for key, value in expected.items():
            if data[key] != value:
                raise RuntimeError(f"{label}/{hook}: {key}: {data[key]} != {value}")
        if count:
            hit.add(hook)
        summary.append(dict(scenario=label, hook=hook, name=point["name"], passed=True, **data))
    if not any(counts[p["hook"]] for p in manifest):
        raise RuntimeError(f"{label}: empty compute reference")
if hit != {p["hook"] for p in manifest}:
    raise RuntimeError("missing compute hook coverage")
(out / "summary.json").write_text(json.dumps(summary, indent=2) + "\n")
print(out / "summary.json")
