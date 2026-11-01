#!/usr/bin/env python3
"""Build six independently typed CB hook adapters, linked to the C runtime."""

import argparse
import hashlib
import json
from pathlib import Path
import subprocess

ROOT = Path(__file__).resolve().parents[3]
p = argparse.ArgumentParser(description=__doc__)
for name in ("compiler", "runtime-build", "ubpf-build", "output"):
    p.add_argument("--" + name, type=Path, required=True)
a = p.parse_args()
out = a.output.resolve()
out.mkdir(parents=True, exist_ok=False)
compiler = str(a.compiler.resolve())
runtime = a.runtime_build.resolve()
ubpf = a.ubpf_build.resolve()
names = [
    "cb_reserve_back",
    "cb_push_back",
    "cb_wait_front",
    "cb_pop_front",
    "get_write_ptr",
    "get_read_ptr",
]
objects = []
manifest = []
with (out / "build.log").open("w") as log:

    def run(command):
        log.write(json.dumps(command) + "\n")
        log.flush()
        subprocess.run(command, stdout=log, stderr=subprocess.STDOUT, check=True)

    for hook, name in enumerate(names, 1):
        folder = out / str(hook)
        folder.mkdir()
        count = hook <= 4
        params = "call: Observation, core: Core, cb_id: u32" + (", n: u32" if count else "")
        condition = (
            "core.device == 0 && core.x == 0 && core.y == 0 && cb_id == 0 && call.thread > 0"
            + (" && n > 0" if count else "")
        )
        program = folder / "observe.fg"
        program.write_text(
            "use tt_emule.types.{Observation, Core};\n"
            + f"fn observe on tt_emule.device.core.cb.{name}({params}) {{\n"
            + " if call.phase == 0 { builtin.counter_add(0, 1); } else { builtin.counter_add(1, 1); }\n"
            + f" if {condition} {{ builtin.counter_add(2, 1); }}\n"
            + " builtin.emit(call.phase, call.sequence);\n}\n"
        )
        for command, filename in [("build", "push.fgo"), ("emit-adapter", "push.adapter.h")]:
            run(
                [
                    compiler,
                    command,
                    str(program),
                    "--model",
                    str(ROOT / "models/tt_emule"),
                    "--function",
                    "program::observe",
                    "--output",
                    str(folder / filename),
                ]
            )
        obj = folder / "adapter.o"
        objects.append(str(obj))
        run(
            [
                "cc",
                "-c",
                "-fPIC",
                "-std=c11",
                "-Wall",
                "-Wextra",
                "-Werror",
                "-I",
                str(ROOT / "agent/include"),
                "-I",
                str(ROOT / "runtime/include"),
                "-I",
                str(ROOT / "sdk/include"),
                "-I",
                str(folder),
                f"-DFG_MODEL_HOOK={hook}",
                f"-DFG_MODEL_HAS_COUNT={int(count)}",
                f"-DFG_MODEL_CALLBACK=fg_cb_model_{hook}",
                f'-DFG_MODEL_PACKAGE_ENV="FG_CB_MODEL_PACKAGE_{hook}"',
                f'-DFG_MODEL_REPORT_ENV="FG_CB_MODEL_REPORT_{hook}"',
                str(ROOT / "integration/tt-emule/model-runtime/runtime.c"),
                "-o",
                str(obj),
            ]
        )
        manifest.append({"hook": hook, "name": name, "has_count": count})
    driver = out / "driver.c"
    signature = (
        "uint32_t hook,uint32_t phase,uint32_t x,uint32_t y,uint64_t a,uint64_t b,uint64_t c"
    )
    driver.write_text(
        "#include <stdint.h>\n"
        + "".join(f"void fg_cb_model_{i}({signature});\n" for i in range(1, 7))
        + f"void fg_tt_scope_event({signature}) {{\n"
        + "".join(f" fg_cb_model_{i}(hook,phase,x,y,a,b,c);\n" for i in range(1, 7))
        + "}\n"
    )
    run(
        [
            "cc",
            "-shared",
            "-fPIC",
            str(driver),
            *objects,
            str(runtime / "libfingolfin_agent.a"),
            str(runtime / "libfingolfin.a"),
            str(ubpf / "lib/libubpf.a"),
            "-ljson-c",
            "-lcrypto",
            "-pthread",
            "-lm",
            "-o",
            str(out / "runtime.so"),
        ]
    )
(out / "manifest.json").write_text(json.dumps(manifest, indent=2) + "\n")
paths = (
    [p for p in out.rglob("*") if p.is_file()]
    + [Path(__file__).resolve(), ROOT / "integration/tt-emule/model-runtime/runtime.c"]
    + list((ROOT / "models/tt_emule").rglob("*.fg"))
)
(out / "artifacts.json").write_text(
    json.dumps({str(p): hashlib.sha256(p.read_bytes()).hexdigest() for p in paths}, indent=2) + "\n"
)
print(out)
