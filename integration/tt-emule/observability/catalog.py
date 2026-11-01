#!/usr/bin/env python3
"""Inventory the probes actually installed in a pinned tt-emule working tree."""

import argparse, hashlib, json, re, subprocess
from pathlib import Path

PIN = "07f1bd8301544403c8bc1faa4038f6cbf69909f1"
p = argparse.ArgumentParser(description=__doc__)
p.add_argument("--emule", type=Path, required=True)
p.add_argument("--output", type=Path, required=True)
a = p.parse_args()
root = a.emule.resolve()
if subprocess.check_output(["git", "-C", str(root), "rev-parse", "HEAD"], text=True).strip() != PIN:
    raise RuntimeError("unexpected emulator revision")
points = {}
scopes = 0
pattern = re.compile(
    r"^(?:inline|ALWI|FORCE_INLINE)\s+(?:void|uint32_t)\s+(\w+)\s*\([^;{}]*?\)\s*\{\s*\n\s*FG_TRACE_SCOPE\((\d+),\s*(\w+),\s*(\w+),\s*(\w+)\);",
    re.M,
)
for path in sorted((root / "include/jit_hw/api").rglob("*.h")):
    text = path.read_text()
    relative = str(path.relative_to(root))
    scopes += len(re.findall(r"\bFG_TRACE_SCOPE\s*\(", text))
    for m in pattern.finditer(text):
        name, number, *fields = m.groups()
        hook = int(number)
        category = (
            "cb" if path.name == "cb_api.h" else "noc" if "/dataflow/" in relative else "compute"
        )
        point = points.setdefault(
            hook,
            dict(
                id=hook,
                name=name,
                category=category,
                source=relative,
                source_sha256=hashlib.sha256(text.encode()).hexdigest(),
                variants=[],
            ),
        )
        if (point["name"], point["source"]) != (name, relative):
            raise RuntimeError(f"conflicting hook ID {hook}")
        point["variants"].append(
            dict(
                line=text.count("\n", 0, m.start()) + 1,
                signature=" ".join(m.group().split("FG_TRACE_SCOPE")[0].split()),
                fields=[None if x == "0" else x for x in fields],
            )
        )
if set(points) != set(range(1, 40)) or len({p["name"] for p in points.values()}) != 39:
    raise RuntimeError("incomplete or ambiguous installed probe set")
variants = sum(len(p["variants"]) for p in points.values())
if variants != 51 or variants != scopes:
    raise RuntimeError("missing or unsupported probe implementation")
if a.output.exists():
    raise RuntimeError("output already exists")
a.output.write_text(
    json.dumps(
        dict(
            source_revision=PIN,
            source_state="instrumented working tree",
            points=[points[i] for i in sorted(points)],
        ),
        indent=2,
    )
    + "\n"
)
print(
    f'{len(points)} hooks, {sum(len(p["variants"]) for p in points.values())} installed variants: {a.output}'
)
