#!/usr/bin/env python3
"""Prepare pinned, reviewable API-scope instrumentation; never silently repatch."""

import argparse
import hashlib
import json
from pathlib import Path
import re
import subprocess

PIN = "07f1bd8301544403c8bc1faa4038f6cbf69909f1"
p = argparse.ArgumentParser(description=__doc__)
p.add_argument("--emule", type=Path, required=True)
p.add_argument("--output", type=Path, required=True)
p.add_argument("--apply", action="store_true")
a = p.parse_args()
root = a.emule.resolve()
assert (
    subprocess.check_output(["git", "-C", str(root), "rev-parse", "HEAD"], text=True).strip() == PIN
)
out = a.output.resolve()
out.mkdir(parents=True, exist_ok=False)
groups = {
    "cb_api.h": [
        "cb_reserve_back",
        "cb_push_back",
        "cb_wait_front",
        "cb_pop_front",
        "get_write_ptr",
        "get_read_ptr",
    ],
    "dataflow/dataflow_api.h": [
        "noc_async_read",
        "noc_async_write",
        "noc_async_read_barrier",
        "noc_async_write_barrier",
        "noc_async_writes_flushed",
        "noc_async_atomic_barrier",
        "noc_semaphore_inc",
        "noc_semaphore_set",
        "noc_semaphore_wait",
        "noc_semaphore_wait_min",
        "noc_async_write_multicast",
        "noc_async_read_one_packet",
        "noc_async_write_one_packet",
        "noc_inline_dw_write",
    ],
    "compute/reg_api.h": [
        "tile_regs_acquire",
        "tile_regs_commit",
        "tile_regs_wait",
        "tile_regs_release",
    ],
    "compute/common.h": [
        "add_tiles",
        "sub_tiles",
        "mul_tiles",
        "pack_tile",
        "pack_tile_block",
        "copy_tile",
    ],
    "compute/tilize.h": ["tilize_block"],
    "compute/untilize.h": ["untilize_block"],
    "compute/eltwise_unary/exp.h": ["exp_tile"],
    "compute/eltwise_unary/recip.h": ["recip_tile"],
    "compute/eltwise_unary/sqrt.h": ["sqrt_tile"],
    "compute/eltwise_unary/relu.h": [
        "relu_tile",
        "relu_max_tile",
        "relu_min_tile",
        "leaky_relu_tile",
    ],
}
# Select named scalar arguments from each exact source signature; do not persist
# host semaphore pointers. Unknown/unavailable arguments are explicit in catalog.
preferred = {
    "cb": ["cb_id", "n"],
    "noc_async_read": ["src_noc_addr", "dst_local_l1_addr", "size"],
    "noc_async_write": ["src_local_l1_addr", "dst_noc_addr", "size"],
    "noc_async_write_multicast": ["src_local_l1_addr", "dst_mcast_noc_addr", "size"],
    "noc_semaphore_inc": ["noc_addr", "incr", "noc"],
    "noc_inline_dw_write": ["addr", "val", "be"],
    "binary": ["icb0", "icb1", "idst"],
    "copy_tile": ["icb", "itile", "idst"],
    "pack_tile": ["idst", "ocb", "output_offset"],
    "pack_tile_block": ["ifrom_dst", "ocb", "ntiles"],
}
catalog, edits = [], {}
for file, names in groups.items():
    relative = "include/jit_hw/api/" + file
    path = root / relative
    source = path.read_text()
    assert "FG_TRACE_SCOPE" not in source, f"already patched: {path}"
    insertions = []
    for name in names:
        hook = len(catalog) + 1
        pattern = (
            r"^(?:inline|ALWI|FORCE_INLINE)\s+(?:void|uint32_t)\s+" + name + r"\s*\([^;{}]*?\)\s*\{"
        )
        matches = list(re.finditer(pattern, source, re.M))
        assert matches, (file, name)
        variants = []
        for match in matches:
            sig = re.sub(r"/\*.*?\*/", "", match.group(), flags=re.S)
            params = preferred.get(name)
            if params is None:
                if file == "cb_api.h":
                    params = preferred["cb"]
                elif name.startswith("noc_async_read_one"):
                    params = preferred["noc_async_read"]
                elif name.startswith("noc_async_write_one"):
                    params = preferred["noc_async_write"]
                elif name in ["add_tiles", "sub_tiles", "mul_tiles"]:
                    params = preferred["binary"]
                elif name in ["tilize_block", "untilize_block"]:
                    params = ["icb", "ntiles", "ocb"]
                elif name.startswith("noc_semaphore_"):
                    params = ["val", "min_val", "noc"]
                elif name.startswith("noc_"):
                    params = ["noc"]
                else:
                    params = ["idst", "param0", "slope"]
            fields = [x if re.search(r"\b" + x + r"\b", sig) else None for x in params]
            if name == "tilize_block" and fields[1] is None and re.search(r"\bnum_tiles\b", sig):
                fields[1] = "num_tiles"
            fields = (fields + [None] * 3)[:3]
            line = source.count("\n", 0, match.start()) + 1
            variants.append(
                {"line": line, "signature": " ".join(match.group().split()), "fields": fields}
            )
            args = ", ".join(x or "0" for x in fields)
            insertions.append((match.end(), f"\n    FG_TRACE_SCOPE({hook}, {args});"))
        catalog.append(
            {
                "id": hook,
                "name": name,
                "category": (
                    "cb"
                    if file == "cb_api.h"
                    else "noc" if file.startswith("dataflow") else "compute"
                ),
                "source": relative,
                "source_sha256": hashlib.sha256(source.encode()).hexdigest(),
                "variants": variants,
            }
        )
    for position, text in sorted(insertions, reverse=True):
        source = source[:position] + text + source[position:]
    source = '#include "jit_hw/fg_trace.h"\n' + source
    edits[relative] = source
header = Path(__file__).with_name("fg_trace.h").read_text()
edits["include/jit_hw/fg_trace.h"] = header
(out / "catalog.json").write_text(json.dumps({"source_revision": PIN, "points": catalog}, indent=2))
import difflib

patch = ""
for relative, content in edits.items():
    original = (root / relative).read_text() if (root / relative).exists() else ""
    patch += "".join(
        difflib.unified_diff(
            original.splitlines(True),
            content.splitlines(True),
            fromfile="a/" + relative if original else "/dev/null",
            tofile="b/" + relative,
        )
    )
(out / "hooks.patch").write_text(patch)
if a.apply:
    subprocess.run(
        ["git", "-C", str(root), "apply", "--check", str(out / "hooks.patch")], check=True
    )
    subprocess.run(["git", "-C", str(root), "apply", str(out / "hooks.patch")], check=True)
print(
    f'{len(catalog)} observation points, {sum(len(p["variants"]) for p in catalog)} source variants; {out}'
)
