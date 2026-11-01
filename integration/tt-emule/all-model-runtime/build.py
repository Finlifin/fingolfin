#!/usr/bin/env python3
"""Link all 39 independently typed model adapters into one observer runtime."""

import argparse, hashlib, json, subprocess
from pathlib import Path

p = argparse.ArgumentParser(description=__doc__)
for name in ("cb", "compute", "noc", "runtime-build", "ubpf-build", "output"):
    p.add_argument("--" + name, type=Path, required=True)
a = p.parse_args()
out = a.output.resolve()
out.mkdir(parents=True, exist_ok=False)
points = []
objects = []
input_hashes = {}
for group, ids in [
    ("cb", set(range(1, 7))),
    ("compute", set(range(21, 40))),
    ("noc", set(range(7, 21))),
]:
    root = getattr(a, group).resolve()
    manifest = json.loads((root / "manifest.json").read_text())
    hashes = json.loads((root / "artifacts.json").read_text())
    if {x["hook"] for x in manifest} != ids or len(manifest) != len(ids):
        raise RuntimeError(f"{group}: unexpected hooks")
    for point in manifest:
        hook = point["hook"]
        folder = root / str(hook)
        for name in ("adapter.o", "push.fgo", "push.adapter.h"):
            path = folder / name
            digest = hashlib.sha256(path.read_bytes()).hexdigest()
            if hashes.get(str(path)) != digest:
                raise RuntimeError(f"changed input: {path}")
            input_hashes[str(path)] = digest
        objects.append(str(folder / "adapter.o"))
        points.append(
            dict(
                point,
                group=group,
                package=str(folder / "push.fgo"),
                callback=f"fg_{group}_model_{hook}",
            )
        )
points.sort(key=lambda p: p["hook"])
signature = "uint32_t hook,uint32_t phase,uint32_t x,uint32_t y,uint64_t a,uint64_t b,uint64_t c"
driver = out / "driver.c"
driver.write_text(
    "#include <stdint.h>\n"
    + "".join(f'void {p["callback"]}({signature});\n' for p in points)
    + f"void fg_tt_scope_event({signature}) {{\n"
    + "".join(f'{p["callback"]}(hook,phase,x,y,a,b,c);\n' for p in points)
    + "}\n"
)
cmd = [
    "cc",
    "-shared",
    "-fPIC",
    "-Wall",
    "-Wextra",
    "-Werror",
    str(driver),
    *objects,
    str(a.runtime_build.resolve() / "libfingolfin_agent.a"),
    str(a.runtime_build.resolve() / "libfingolfin.a"),
    str(a.ubpf_build.resolve() / "lib/libubpf.a"),
    "-ljson-c",
    "-lcrypto",
    "-pthread",
    "-lm",
    "-o",
    str(out / "runtime.so"),
]
with (out / "build.log").open("w") as log:
    log.write(json.dumps(cmd) + "\n")
    log.flush()
    subprocess.run(cmd, stdout=log, stderr=subprocess.STDOUT, check=True)
(out / "manifest.json").write_text(json.dumps(points, indent=2) + "\n")
for path in [
    driver,
    out / "runtime.so",
    a.runtime_build.resolve() / "libfingolfin_agent.a",
    a.runtime_build.resolve() / "libfingolfin.a",
    a.ubpf_build.resolve() / "lib/libubpf.a",
]:
    input_hashes[str(path)] = hashlib.sha256(path.read_bytes()).hexdigest()
(out / "artifacts.json").write_text(json.dumps(input_hashes, indent=2) + "\n")
print(out)
