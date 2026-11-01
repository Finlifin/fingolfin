#!/usr/bin/env python3
"""Offline rebuild and verify the prepared, pinned tt-emule integration."""

import argparse, hashlib, json, shutil, subprocess, sys, time
from pathlib import Path

ROOT = Path(__file__).resolve().parents[2]
SOURCE = Path(__file__).resolve().parent
p = argparse.ArgumentParser(description=__doc__)
for name in ("compiler", "metal", "emule", "metal-build", "ubpf-source", "output"):
    p.add_argument("--" + name, type=Path, required=True)
p.add_argument("--jobs", type=int, default=2)
a = p.parse_args()
if a.jobs < 1:
    p.error("--jobs must be positive")
inputs = {
    name: str(getattr(a, name.replace("-", "_")).resolve())
    for name in ("compiler", "metal", "emule", "metal-build", "ubpf-source")
}
pins = {
    "metal": "d48d09dee19de51f694a52fdf75d569950d38ceb",
    "emule": "07f1bd8301544403c8bc1faa4038f6cbf69909f1",
    "ubpf-source": "8d2dd3580ab80f9b4a79a9d2fa2420aa38fc7521",
}
for name, pin in pins.items():
    actual = subprocess.check_output(
        ["git", "-C", inputs[name], "rev-parse", "HEAD"], text=True
    ).strip()
    if actual != pin:
        raise RuntimeError(f"{name}: unexpected revision {actual}")
if not Path(inputs["compiler"]).is_file():
    raise RuntimeError("compiler missing")
cache = {}
for line in (Path(inputs["metal-build"]) / "CMakeCache.txt").read_text().splitlines():
    if "=" in line and not line.startswith(("#", "//")):
        key, value = line.split("=", 1)
        cache[key.split(":", 1)[0]] = value
for key, name in [("CMAKE_HOME_DIRECTORY", "metal"), ("TT_EMULE_PATH", "emule")]:
    if not cache.get(key) or str(Path(cache[key]).resolve()) != inputs[name]:
        raise RuntimeError(f"host build {key} does not match --{name}")
if cache.get("TT_METAL_USE_EMULE") != "ON":
    raise RuntimeError("host build is not configured for emulator")

out = a.output.resolve()
out.mkdir(parents=True, exist_ok=False)
(out / "inputs.json").write_text(
    json.dumps(
        dict(
            paths=inputs,
            pins=pins,
            jobs=a.jobs,
            compiler_sha256=hashlib.sha256(Path(inputs["compiler"]).read_bytes()).hexdigest(),
        ),
        indent=2,
    )
    + "\n"
)
stages = []


def run(label, cmd):
    print(f"[{len(stages)+1}] {label}", flush=True)
    start = time.monotonic()
    log = out / (label + ".log")
    with log.open("w") as f:
        f.write(json.dumps(cmd) + "\n")
        f.flush()
        result = subprocess.run(cmd, stdout=f, stderr=subprocess.STDOUT)
    stages.append(
        dict(
            stage=label,
            command=cmd,
            exit_code=result.returncode,
            seconds=time.monotonic() - start,
            log=log.name,
        )
    )
    (out / "stages.json").write_text(json.dumps(stages, indent=2) + "\n")
    if result.returncode:
        raise RuntimeError(f"{label} failed; see {log}")


def script(label, path, *args):
    run(label, [sys.executable, str(SOURCE / path), *map(str, args)])


# Keep the current worktree unchanged. Record actual patches, not just HEAD IDs.
for name in pins:
    diff = subprocess.check_output(["git", "-C", inputs[name], "diff", "HEAD", "--"], text=True)
    (out / (name + "-working-tree.patch")).write_text(diff)
# git diff omits this newly added header; preserve its exact implementation too.
shutil.copyfile(Path(inputs["emule"]) / "include/jit_hw/fg_trace.h", out / "emule-fg_trace.h")
script(
    "catalog",
    "observability/catalog.py",
    "--emule",
    inputs["emule"],
    "--output",
    out / "catalog.json",
)
ubpf = out / "ubpf-build"
runtime = out / "runtime-build"
run(
    "configure-ubpf",
    [
        "cmake",
        "-S",
        inputs["ubpf-source"],
        "-B",
        str(ubpf),
        "-DCMAKE_POSITION_INDEPENDENT_CODE=ON",
        "-DCMAKE_BUILD_TYPE=Release",
        "-DUBPF_ENABLE_TESTS=OFF",
        "-DUBPF_SKIP_EXTERNAL=ON",
        "-DUBPF_INSTALL_GIT_HOOKS=OFF",
    ],
)
run("build-ubpf", ["cmake", "--build", str(ubpf), "-j", str(a.jobs)])
run(
    "configure-runtime",
    [
        "cmake",
        "-S",
        str(ROOT / "runtime"),
        "-B",
        str(runtime),
        "-DCMAKE_POSITION_INDEPENDENT_CODE=ON",
        "-DCMAKE_BUILD_TYPE=Release",
        "-DBUILD_TESTING=OFF",
        "-DFG_WITH_PACKAGE=ON",
        "-DFG_WITH_UBPF=ON",
        "-DFG_UBPF_SCRATCH=ON",
        "-DFG_UBPF_ROOT=" + inputs["ubpf-source"],
        "-DFG_UBPF_BUILD=" + str(ubpf),
    ],
)
run("build-runtime", ["cmake", "--build", str(runtime), "-j", str(a.jobs)])
common = ["--compiler", inputs["compiler"], "--runtime-build", runtime, "--ubpf-build", ubpf]
for group in ("cb", "compute", "noc"):
    extra = [] if group == "cb" else ["--catalog", out / "catalog.json"]
    script(
        "build-" + group,
        group + "-model-runtime/build.py",
        *common,
        *extra,
        "--output",
        out / group,
    )
script(
    "build-dynamic",
    "dynamic-model-runtime/build.py",
    "--compiler",
    inputs["compiler"],
    "--cb",
    out / "cb",
    "--compute",
    out / "compute",
    "--noc",
    out / "noc",
    "--runtime-build",
    runtime,
    "--ubpf-build",
    ubpf,
    "--output",
    out / "dynamic",
)
run(
    "build-host-tests",
    ["cmake", "--build", inputs["metal-build"], "--target", "unit_tests_api", "-j", str(a.jobs)],
)
script(
    "reference",
    "observability/run.py",
    "--metal",
    inputs["metal"],
    "--build",
    inputs["metal-build"],
    "--catalog",
    out / "catalog.json",
    "--output",
    out / "reference",
)
script(
    "dynamic-tests",
    "dynamic-model-runtime/run.py",
    "--artifacts",
    out / "dynamic",
    "--metal",
    inputs["metal"],
    "--build",
    inputs["metal-build"],
    "--reference",
    out / "reference",
    "--output",
    out / "results",
)
script(
    "property-tests",
    "dynamic-model-runtime/check-properties.py",
    "--compiler",
    inputs["compiler"],
    "--runtime-build",
    runtime,
    "--output",
    out / "results/properties",
)
script(
    "recovery-tests",
    "dynamic-model-runtime/check-recovery.py",
    "--artifacts",
    out / "dynamic",
    "--runtime-build",
    runtime,
    "--ubpf-build",
    ubpf,
    "--output",
    out / "results/recovery",
)
script(
    "multidevice-tests",
    "multidevice/run.py",
    "--artifacts",
    out / "dynamic",
    "--metal",
    inputs["metal"],
    "--build",
    inputs["metal-build"],
    "--output",
    out / "results/multidevice",
)
script(
    "property-gate-tests",
    "multidevice/run.py",
    "--artifacts",
    out / "dynamic",
    "--metal",
    inputs["metal"],
    "--build",
    inputs["metal-build"],
    "--property-gate",
    "--output",
    out / "results/property-gate",
)
script(
    "device-boundaries",
    "multidevice/check-boundary.py",
    "--artifacts",
    out / "dynamic",
    "--output",
    out / "results/multidevice/boundary",
)
script("report", "dynamic-model-runtime/report.py", "--results", out / "results")
# Snapshot source and final report inputs for the verified run.
paths = []
for base in (ROOT / "runtime", ROOT / "agent", ROOT / "sdk", ROOT / "models/tt_emule", SOURCE):
    paths.extend(
        p
        for p in base.rglob("*")
        if p.is_file()
        and p.suffix in (".c", ".h", ".py", ".fg", ".toml", ".patch")
        and "__pycache__" not in p.parts
    )
paths.extend(
    [
        ROOT / "runtime/CMakeLists.txt",
        out / "results/summary.json",
        out / "results/recovery/summary.json",
        out / "dynamic/runtime.so",
    ]
)
(out / "provenance.json").write_text(
    json.dumps(
        {str(path): hashlib.sha256(path.read_bytes()).hexdigest() for path in paths}, indent=2
    )
    + "\n"
)
print(out / "results/index.html", flush=True)
