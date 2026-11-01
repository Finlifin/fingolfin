#!/usr/bin/env python3
"""Build independently typed compute adapters, checking every variant's captured slots."""

import argparse, hashlib, json, re, subprocess
from pathlib import Path

ROOT = Path(__file__).resolve().parents[3]
p = argparse.ArgumentParser(description=__doc__)
for name in ("compiler", "runtime-build", "ubpf-build", "catalog", "output"):
    p.add_argument("--" + name, type=Path, required=True)
a = p.parse_args()
out = a.output.resolve()
out.mkdir(parents=True, exist_ok=False)
points = [p for p in json.loads(a.catalog.read_text())["points"] if p["category"] == "compute"]
assert len(points) == 19
model = (ROOT / "models/tt_emule/src/hooks/compute.fg").read_text()
objects = []
manifest = []
with (out / "build.log").open("w") as log:

    def run(cmd):
        log.write(json.dumps(cmd) + "\n")
        log.flush()
        subprocess.run(cmd, stdout=log, stderr=subprocess.STDOUT, check=True)

    for point in points:
        hook = point["id"]
        name = point["name"]
        folder = out / str(hook)
        folder.mkdir()
        match = re.search(
            r"pub hook (tt_emule\.device\.core\.compute\." + name + r")\(([^;]+)\);", model
        )
        if not match:
            raise ValueError(name)
        path, signature = match.groups()
        params = [x.strip().split(": ") for x in signature.split(",")][2:]
        slots = []
        for param, kind in params:
            if kind != "u32":
                raise ValueError((name, param, kind))
            positions = []
            for variant in point["variants"]:
                fields = ["ntiles" if f == "num_tiles" else f for f in variant["fields"]]
                if fields.count(param) != 1:
                    raise ValueError(f"{name}: missing/ambiguous captured parameter {param}")
                positions.append(fields.index(param))
            if len(set(positions)) != 1:
                raise ValueError(f"{name}: variant mapping differs for {param}")
            slots.append(positions[0])
        program = folder / "observe.fg"
        program.write_text(
            "use tt_emule.types.{Observation, Core};\n"
            + f"fn observe on {path}({signature}) {{\n"
            + " if call.phase == 0 { builtin.counter_add(0, 1); } else { builtin.counter_add(1, 1); }\n"
            + " if core.device == 0 && core.x == 0 && core.y == 0 && call.thread > 0 { builtin.counter_add(2, 1); }\n"
            + " builtin.emit(call.phase, call.sequence);\n"
            + "".join(f" builtin.emit({param}, call.sequence);\n" for param, _ in params)
            + "}\n"
        )
        for cmd, filename in [("build", "push.fgo"), ("emit-adapter", "push.adapter.h")]:
            run(
                [
                    str(a.compiler.resolve()),
                    cmd,
                    str(program),
                    "--model",
                    str(ROOT / "models/tt_emule"),
                    "--function",
                    "program::observe",
                    "--output",
                    str(folder / filename),
                ]
            )
        scalar = "uint64_t a,uint64_t b,uint64_t c"
        unused = "(void)a;(void)b;(void)c;"
        valid = " && ".join("abc"[slot] + "<=UINT32_MAX" for slot in slots) or "1"
        policy = f"#define FG_MODEL_FIELDS {len(slots)}u\n"
        policy += f"static int fg_model_valid({scalar}) {{ {unused} return {valid}; }}\n"
        policy += (
            f"static void fg_model_values(uint32_t *values,{scalar}) {{ (void)values;{unused}"
            + "".join(f'values[{i}]=(uint32_t){"abc"[slot]};' for i, slot in enumerate(slots))
            + "}\n"
        )
        policy += (
            f"static void fg_model_fill(struct FgNativeFrame *frame,{scalar}) {{ (void)frame;{unused}"
            + "".join(f'frame->arg{i+2}=(uint32_t){"abc"[slot]};' for i, slot in enumerate(slots))
            + "}\n"
        )
        (folder / "policy.h").write_text(policy)
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
                f"-DFG_MODEL_CALLBACK=fg_compute_model_{hook}",
                f'-DFG_MODEL_PACKAGE_ENV="FG_COMPUTE_PACKAGE_{hook}"',
                f'-DFG_MODEL_REPORT_ENV="FG_COMPUTE_REPORT_{hook}"',
                str(Path(__file__).with_name("runtime.c")),
                "-o",
                str(obj),
            ]
        )
        manifest.append(dict(hook=hook, name=name, params=params, slots=slots))
    driver = out / "driver.c"
    signature = (
        "uint32_t hook,uint32_t phase,uint32_t x,uint32_t y,uint64_t a,uint64_t b,uint64_t c"
    )
    driver.write_text(
        "#include <stdint.h>\n"
        + "".join(f'void fg_compute_model_{p["id"]}({signature});\n' for p in points)
        + f"void fg_tt_scope_event({signature}) {{\n"
        + "".join(f'fg_compute_model_{p["id"]}(hook,phase,x,y,a,b,c);\n' for p in points)
        + "}\n"
    )
    run(
        [
            "cc",
            "-shared",
            "-fPIC",
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
    )
(out / "manifest.json").write_text(json.dumps(manifest, indent=2) + "\n")
(out / "catalog.json").write_bytes(a.catalog.read_bytes())
paths = (
    [p for p in out.rglob("*") if p.is_file()]
    + [Path(__file__).resolve(), Path(__file__).with_name("runtime.c")]
    + list((ROOT / "models/tt_emule").rglob("*.fg"))
)
(out / "artifacts.json").write_text(
    json.dumps({str(p): hashlib.sha256(p.read_bytes()).hexdigest() for p in paths}, indent=2) + "\n"
)
print(out)
