#!/usr/bin/env python3
"""Verify the combined 39-hook runtime across six real emulator scenarios."""

import argparse, collections, json, os, subprocess, sys
from pathlib import Path

ROOT = Path(__file__).resolve().parents[3]
p = argparse.ArgumentParser(description=__doc__)
for name in ("artifacts", "metal", "build", "reference", "output"):
    p.add_argument("--" + name, type=Path, required=True)
a = p.parse_args()
out = a.output.resolve()
out.mkdir(parents=True, exist_ok=False)
art = a.artifacts.resolve()
manifest = json.loads((art / "manifest.json").read_text())
summary = []
hits = set()
if len(manifest) != 39 or {p["hook"] for p in manifest} != set(range(1, 40)):
    raise RuntimeError("incomplete bundle")
cases = [
    ("cb", "CB_Boundary_Wraparound_NoViolation"),
    ("copy", "TensixTestSingleCoreMultiTileBankedDramReaderDataCopyDramWriter"),
    ("noc", "Fingolfin_NocTransferAndSemaphore_Observed"),
    ("arithmetic", "Fingolfin_ArithmeticAndActivation_Observed"),
    ("layout", "Fingolfin_LayoutRoundTrip_Observed"),
    ("multicast", "Fingolfin_MulticastTwoReceivers_Observed"),
]
for label, test in cases:
    ref = json.loads((a.reference / f"{label}-trace.json").read_text())
    if ref["dropped"] or not ref["events"]:
        raise RuntimeError("invalid reference")
    counts = collections.Counter(
        e["hook"]
        for e in ref["events"]
        if e["x"] == 0 and e["y"] == 0 and (e["hook"] > 6 or e["args"][0] == "0")
    )
    env = os.environ.copy()
    env.update(LD_PRELOAD=str(art / "runtime.so"), TT_EMULE_JIT_CACHE_DIR=str(out / f"{label}-jit"))
    for point in manifest:
        hook = point["hook"]
        prefix = "FG_CB_MODEL" if point["group"] == "cb" else "FG_" + point["group"].upper()
        env[f"{prefix}_PACKAGE_{hook}"] = point["package"]
        env[f"{prefix}_REPORT_{hook}"] = str(out / f"{label}-{hook}.json")
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
        multiplier = (
            1
            if point["group"] == "cb"
            else (
                len(point["slots"]) + 1
                if point["group"] == "compute"
                else point["records_per_event"]
            )
        )
        expected = dict(
            seen=len(ref["events"]),
            selected=count,
            dispatches=count,
            errors=0,
            enter=count // 2,
            exit=count // 2,
            matched_args=count,
            records=count * multiplier,
        )
        for key, value in expected.items():
            if data[key] != value:
                raise RuntimeError(f"{label}/{hook}: {key}: {data[key]} != {value}")
        if count:
            hits.add(hook)
        summary.append(
            dict(
                scenario=label,
                hook=hook,
                group=point["group"],
                name=point["name"],
                passed=True,
                **data,
            )
        )
if hits != set(range(1, 40)):
    raise RuntimeError(f"unhit hooks: {set(range(1,40))-hits}")
(out / "summary.json").write_text(json.dumps(summary, indent=2) + "\n")
print(out / "summary.json")
