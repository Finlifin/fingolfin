#!/usr/bin/env python3
"""Build typed NOC adapters with exact u8/u32/u64 argument verification."""

import argparse, hashlib, json, re, subprocess
from pathlib import Path

ROOT = Path(__file__).resolve().parents[3]
p = argparse.ArgumentParser(description=__doc__)
for name in ("compiler", "runtime-build", "ubpf-build", "catalog", "output"):
    p.add_argument("--" + name, type=Path, required=True)
a = p.parse_args()
out = a.output.resolve()
out.mkdir(parents=True, exist_ok=False)
points = [p for p in json.loads(a.catalog.read_text())["points"] if p["category"] == "noc"]
assert len(points) == 14
model = (ROOT / "models/tt_emule/src/hooks/noc.fg").read_text()
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
            r"pub hook (tt_emule\.device\.core\.noc\." + name + r")\(([^;]+)\);", model
        )
        if not match:
            raise ValueError(name)
        path, signature = match.groups()
        params = [x.strip().split(": ") for x in signature.split(",")][2:]
        slots = []
        for param, kind in params:
            if kind not in ("u8", "u32", "u64"):
                raise ValueError((name, param, kind))
            positions = []
            for variant in point["variants"]:
                fields = variant["fields"]
                if fields.count(param) != 1:
                    raise ValueError(f"{name}: missing/ambiguous captured parameter {param}")
                positions.append(fields.index(param))
            if len(set(positions)) != 1:
                raise ValueError(f"{name}: variant mapping differs for {param}")
            slots.append(positions[0])
        emits = ""
        expect = ""
        record_count = 0
        valid = []
        fills = ""
        for i, ((param, kind), slot) in enumerate(zip(params, slots)):
            source = "abc"[slot]
            fills += f"frame->arg{i+2}=(uint{kind[1:]}_t){source};"
            if kind != "u64":
                valid.append(f"{source}<=UINT{kind[1:]}_MAX")
            if kind == "u32":
                emits += f" builtin.emit({param}, call.sequence);\n"
                expect += (
                    f"values[{record_count}]=sequence;tags[{record_count}]=(uint32_t){source};"
                )
                record_count += 1
            elif kind == "u64":
                emits += f" builtin.emit({i+16}, {param});\n"
                expect += f"values[{record_count}]={source};tags[{record_count}]={i+16};"
                record_count += 1
            else:
                # Lossless eight-bit encoding avoids implicit casts in the strongly typed DSL.
                for bit in range(8):
                    tag = 32 + i * 8 + bit
                    emits += f" if {param} / {1<<bit} % 2 == 1 {{ builtin.emit({tag}, 1); }} else {{ builtin.emit({tag}, 0); }}\n"
                    expect += (
                        f"values[{record_count}]=({source}>>{bit})&1u;tags[{record_count}]={tag};"
                    )
                    record_count += 1
        program = folder / "observe.fg"
        program.write_text(
            "use tt_emule.types.{Observation, Core};\n"
            + f"fn observe on {path}({signature}) {{\n"
            + " if call.phase == 0 { builtin.counter_add(0, 1); } else { builtin.counter_add(1, 1); }\n"
            + " if core.device == 0 && core.x == 0 && core.y == 0 && call.thread > 0 { builtin.counter_add(2, 1); }\n"
            + " builtin.emit(call.phase, call.sequence);\n"
            + emits
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
        policy = f"#define FG_MODEL_FIELDS {record_count}u\n"
        policy += (
            f"static int fg_model_valid({scalar}) {{ {unused} return "
            + (" && ".join(valid) or "1")
            + "; }\n"
        )
        policy += f"static void fg_model_records(uint64_t *values,uint32_t *tags,uint64_t sequence,{scalar}) {{ (void)sequence;{unused}{expect} }}\n"
        policy += f"static void fg_model_fill(struct FgNativeFrame *frame,{scalar}) {{ {unused}{fills} }}\n"
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
                f"-DFG_MODEL_CALLBACK=fg_noc_model_{hook}",
                f'-DFG_MODEL_PACKAGE_ENV="FG_NOC_PACKAGE_{hook}"',
                f'-DFG_MODEL_REPORT_ENV="FG_NOC_REPORT_{hook}"',
                str(Path(__file__).with_name("runtime.c")),
                "-o",
                str(obj),
            ]
        )
        manifest.append(
            dict(
                hook=hook, name=name, params=params, slots=slots, records_per_event=record_count + 1
            )
        )
    driver = out / "driver.c"
    signature = (
        "uint32_t hook,uint32_t phase,uint32_t x,uint32_t y,uint64_t a,uint64_t b,uint64_t c"
    )
    driver.write_text(
        "#include <stdint.h>\n"
        + "".join(f'void fg_noc_model_{p["id"]}({signature});\n' for p in points)
        + f"void fg_tt_scope_event({signature}) {{\n"
        + "".join(f'fg_noc_model_{p["id"]}(hook,phase,x,y,a,b,c);\n' for p in points)
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
