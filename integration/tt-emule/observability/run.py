#!/usr/bin/env python3
"""Run real CB, data movement, semaphore and arithmetic scenarios, preserving trace + gtest evidence."""

import argparse
import json
import os
from pathlib import Path
import shutil
import subprocess
import sys

p = argparse.ArgumentParser(description=__doc__)
p.add_argument("--metal", type=Path, required=True)
p.add_argument("--build", type=Path, required=True)
p.add_argument("--catalog", type=Path, required=True)
p.add_argument("--output", type=Path, required=True)
a = p.parse_args()
out = a.output.resolve()
out.mkdir(parents=True, exist_ok=False)
source = Path(__file__).resolve().parent
shutil.copyfile(a.catalog, out / "catalog.json")
subprocess.run(
    [
        "cc",
        "-std=c11",
        "-Wall",
        "-Wextra",
        "-Werror",
        "-shared",
        "-fPIC",
        str(source / "trace.c"),
        "-pthread",
        "-o",
        str(out / "trace.so"),
    ],
    check=True,
)
traces = []
results = []
points = {p["id"]: p["name"] for p in json.loads(a.catalog.read_text())["points"]}
required = {
    "layout": {"tilize_block", "untilize_block"},
    "multicast": {"noc_async_write_multicast", "noc_semaphore_wait", "noc_semaphore_inc"},
    "cb": {"cb_reserve_back", "cb_push_back", "cb_wait_front", "cb_pop_front"},
    "copy": {"copy_tile", "pack_tile", "tile_regs_acquire", "tile_regs_release"},
    "noc": {
        "noc_async_read",
        "noc_async_write",
        "noc_async_read_one_packet",
        "noc_async_write_one_packet",
        "noc_inline_dw_write",
        "noc_semaphore_set",
        "noc_semaphore_inc",
        "noc_semaphore_wait",
        "noc_semaphore_wait_min",
        "noc_async_atomic_barrier",
    },
    "arithmetic": {
        "add_tiles",
        "sub_tiles",
        "mul_tiles",
        "exp_tile",
        "recip_tile",
        "sqrt_tile",
        "relu_tile",
        "relu_max_tile",
        "relu_min_tile",
        "leaky_relu_tile",
        "pack_tile_block",
    },
}

for label, test in [
    ("cb", "CB_Boundary_Wraparound_NoViolation"),
    ("copy", "TensixTestSingleCoreMultiTileBankedDramReaderDataCopyDramWriter"),
    ("noc", "Fingolfin_NocTransferAndSemaphore_Observed"),
    ("arithmetic", "Fingolfin_ArithmeticAndActivation_Observed"),
    ("layout", "Fingolfin_LayoutRoundTrip_Observed"),
    ("multicast", "Fingolfin_MulticastTwoReceivers_Observed"),
]:
    env = os.environ.copy()
    trace = out / f"{label}-trace.json"
    env.update(
        LD_PRELOAD=str(out / "trace.so"),
        FG_TRACE_REPORT=str(trace),
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
        ],
        env=env,
        check=True,
    )
    data = json.loads(trace.read_text())
    if not data["events"] or data["dropped"]:
        raise RuntimeError(f"{label}: empty or incomplete trace")
    seen = {points[e["hook"]] for e in data["events"] if e["phase"] == 0}
    if required[label] - seen:
        raise RuntimeError(f"{label}: missing required points: {sorted(required[label]-seen)}")
    if label == "multicast":
        cores = {(e["x"], e["y"]) for e in data["events"] if e["phase"] == 0}
        if not {(0, 0), (1, 0), (2, 0)} <= cores:
            raise RuntimeError("multicast: missing sender or receiver core trace")
    stacks = {}
    for sequence, e in enumerate(data["events"], 1):
        if e["sequence"] != sequence or e["phase"] not in (0, 1):
            raise RuntimeError(f"{label}: invalid event sequence/phase")
        stack = stacks.setdefault(e["thread"], [])
        key = (e["hook"], e["x"], e["y"], e["args"])
        if e["phase"] == 0:
            stack.append(key)
        elif not stack or stack.pop() != key:
            raise RuntimeError(f"{label}: mismatched scope exit")
    if any(stacks.values()):
        raise RuntimeError(f"{label}: unclosed scopes")
    traces.extend(["--trace", str(trace)])
    results.extend(["--result", str(out / f"{label}-test" / "summary.json")])
subprocess.run(
    [
        sys.executable,
        str(source / "report.py"),
        "--catalog",
        str(out / "catalog.json"),
        *traces,
        *results,
        "--output",
        str(out / "index.html"),
    ],
    check=True,
)
print(out / "index.html")
