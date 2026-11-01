#!/usr/bin/env python3
"""Check all six CB model bindings against real scenario call counts."""

import argparse
import json
import os
from pathlib import Path
import subprocess
import sys

p = argparse.ArgumentParser(description=__doc__)
for name in ("artifacts", "metal", "build", "output"):
    p.add_argument("--" + name, type=Path, required=True)
a = p.parse_args()
out = a.output.resolve()
out.mkdir(parents=True, exist_ok=False)
art = a.artifacts.resolve()
source = Path(__file__).resolve().parent
cases = [
    ("cb", "CB_Boundary_Wraparound_NoViolation", [12, 12, 8, 8, 2, 0]),
    (
        "copy",
        "TensixTestSingleCoreMultiTileBankedDramReaderDataCopyDramWriter",
        [648, 648, 648, 648, 324, 0],
    ),
    ("noc", "Fingolfin_NocTransferAndSemaphore_Observed", [2, 2, 2, 2, 2, 2]),
    ("arithmetic", "Fingolfin_ArithmeticAndActivation_Observed", [2, 2, 2, 2, 2, 0]),
    ("layout", "Fingolfin_LayoutRoundTrip_Observed", [2, 2, 2, 2, 2, 0]),
    ("multicast", "Fingolfin_MulticastTwoReceivers_Observed", [2, 2, 2, 2, 2, 0]),
]
summary = []
for label, test, counts in cases:
    env = os.environ.copy()
    env.update(LD_PRELOAD=str(art / "runtime.so"), TT_EMULE_JIT_CACHE_DIR=str(out / f"{label}-jit"))
    for hook in range(1, 7):
        env[f"FG_CB_MODEL_PACKAGE_{hook}"] = str(art / str(hook) / "push.fgo")
        env[f"FG_CB_MODEL_REPORT_{hook}"] = str(out / f"{label}-{hook}.json")
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
    for hook, count in enumerate(counts, 1):
        data = json.loads((out / f"{label}-{hook}.json").read_text())
        for key, value in dict(
            selected=count,
            dispatches=count,
            errors=0,
            enter=count // 2,
            exit=count // 2,
            matched_args=count,
            records=count,
        ).items():
            if data[key] != value:
                raise RuntimeError(f"{label}/{hook}: {key}: {data[key]} != {value}")
        summary.append(dict(scenario=label, hook=hook, passed=True, **data))
(out / "summary.json").write_text(json.dumps(summary, indent=2) + "\n")
print(out / "summary.json")
