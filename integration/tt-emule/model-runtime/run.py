#!/usr/bin/env python3
"""Validate the typed cb_push_back model adapter in six real emulator scenarios."""

import argparse
import json
import os
from pathlib import Path
import subprocess
import sys

p = argparse.ArgumentParser(description=__doc__)
p.add_argument("--artifacts", type=Path, required=True)
p.add_argument("--metal", type=Path, required=True)
p.add_argument("--build", type=Path, required=True)
p.add_argument("--output", type=Path, required=True)
a = p.parse_args()
out = a.output.resolve()
out.mkdir(parents=True, exist_ok=False)
artifacts = a.artifacts.resolve()
source = Path(__file__).resolve().parent
cases = [
    ("cb", "CB_Boundary_Wraparound_NoViolation", 12),
    ("copy", "TensixTestSingleCoreMultiTileBankedDramReaderDataCopyDramWriter", 648),
    ("noc", "Fingolfin_NocTransferAndSemaphore_Observed", 2),
    ("arithmetic", "Fingolfin_ArithmeticAndActivation_Observed", 2),
    ("layout", "Fingolfin_LayoutRoundTrip_Observed", 2),
    ("multicast", "Fingolfin_MulticastTwoReceivers_Observed", 2),
]
summary = []
for label, test, count in cases:
    report = out / f"{label}-report.json"
    env = os.environ.copy()
    env.update(
        LD_PRELOAD=str(artifacts / "runtime.so"),
        FG_MODEL_PACKAGE=str(artifacts / "push.fgo"),
        FG_MODEL_REPORT=str(report),
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
    expected = dict(
        selected=count,
        dispatches=count,
        errors=0,
        enter=count // 2,
        exit=count // 2,
        matched_args=count,
        records=count,
    )
    for key, value in expected.items():
        if data[key] != value:
            raise RuntimeError(f"{label}: {key} expected {value}, got {data[key]}")
    summary.append(dict(scenario=label, passed=True, **data))
(out / "summary.json").write_text(json.dumps(summary, indent=2) + "\n")
print(out / "summary.json")
