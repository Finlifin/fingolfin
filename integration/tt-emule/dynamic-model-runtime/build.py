#!/usr/bin/env python3
"""Generate immutable adapters for the launch-aware shared C controller."""

import argparse, hashlib, json, shutil, subprocess
from pathlib import Path

ROOT = Path(__file__).resolve().parents[3]
source = Path(__file__).resolve().parent
p = argparse.ArgumentParser(description=__doc__)
for name in ("cb", "compute", "noc", "runtime-build", "ubpf-build", "output"):
    p.add_argument("--" + name, type=Path, required=True)
p.add_argument("--compiler", type=Path, default=ROOT / "target/debug/fingolfin")
p.add_argument("--property-policy", type=Path, default=source / "property-probe.fg")
a = p.parse_args()
out = a.output.resolve()
out.mkdir(parents=True, exist_ok=False)
objects = []
manifest = []
shutil.copyfile(a.property_policy.resolve(), out / "property-policy.fg")
with (out / "build.log").open("w") as log:

    def run(cmd):
        log.write(json.dumps(cmd) + "\n")
        log.flush()
        subprocess.run(cmd, stdout=log, stderr=subprocess.STDOUT, check=True)

    run(
        [
            str(a.compiler.resolve()),
            "emit-adapter",
            str(out / "property-policy.fg"),
            "--model",
            str(ROOT / "models/tt_emule"),
            "--function",
            "program::observe",
            "--output",
            str(out / "probe.adapter.h"),
        ]
    )
    policy = out / "policy.o"
    objects.append(str(policy))
    run(
        [
            "cc",
            "-c",
            "-fPIC",
            "-std=c11",
            "-Wall",
            "-Wextra",
            "-Werror",
            "-I" + str(out),
            "-I" + str(ROOT / "agent/include"),
            "-I" + str(ROOT / "runtime/include"),
            "-I" + str(ROOT / "sdk/include"),
            str(source / "property-policy.c"),
            "-o",
            str(policy),
        ]
    )
    for group in ("cb", "compute", "noc"):
        root = getattr(a, group).resolve()
        points = json.loads((root / "manifest.json").read_text())
        hashes = json.loads((root / "artifacts.json").read_text())
        for point in points:
            hook = point["hook"]
            folder = out / str(hook)
            folder.mkdir()
            for name in ["push.fgo", "push.adapter.h"] + (["policy.h"] if group != "cb" else []):
                path = root / str(hook) / name
                if hashlib.sha256(path.read_bytes()).hexdigest() != hashes.get(str(path)):
                    raise RuntimeError(f"changed input {path}")
                shutil.copyfile(path, folder / name)
            text = '#include "adapter.h"\n#include "push.adapter.h"\n' + (
                '#include "policy.h"\n' if group != "cb" else ""
            )
            valid = (
                ("e->a<=UINT32_MAX" + (" && e->b<=UINT32_MAX" if hook <= 4 else ""))
                if group == "cb"
                else "fg_model_valid(e->a,e->b,e->c)"
            )
            text += f"static int valid(const FgModelEvent *e) {{ return {valid}; }}\n"
            text += "static void fill(void *output,const FgCallHeader *h,const FgModelEvent *e) { struct FgNativeFrame *f=output; memset(f,0,sizeof(*f));f->header=*h;f->arg0.fg_73657175656e6365=e->sequence;f->arg0.fg_746872656164=e->thread;f->arg0.fg_7068617365=e->phase;f->arg1.fg_646576696365=e->device;f->arg1.fg_78=e->x;f->arg1.fg_79=e->y;"
            text += (
                ("f->arg2=(uint32_t)e->a;" + ("f->arg3=(uint32_t)e->b;" if hook <= 4 else ""))
                if group == "cb"
                else "fg_model_fill(f,e->a,e->b,e->c);"
            )
            text += "}\nstatic size_t expected(FgRecord *r,const FgModelEvent *e) { r[0]=(FgRecord){e->phase,e->sequence};"
            if group == "cb":
                text += "return 1;"
            elif group == "compute":
                text += "uint32_t v[3]={0};fg_model_values(v,e->a,e->b,e->c);for(size_t i=0;i!=FG_MODEL_FIELDS;i++)r[i+1]=(FgRecord){v[i],e->sequence};return FG_MODEL_FIELDS+1;"
            else:
                text += "uint64_t v[FG_MODEL_FIELDS];uint32_t t[FG_MODEL_FIELDS];fg_model_records(v,t,e->sequence,e->a,e->b,e->c);for(size_t i=0;i!=FG_MODEL_FIELDS;i++)r[i+1]=(FgRecord){t[i],v[i]};return FG_MODEL_FIELDS+1;"
            text += (
                "}\nstatic int matched(const FgModelEvent *e) {return !e->device && !e->x && !e->y && e->thread"
                + ((" && !e->a" + (" && e->b>0" if hook <= 4 else "")) if group == "cb" else "")
                + ";}\n"
            )
            text += f"const FgModelAdapter fg_dyn_{hook}={{sizeof(struct FgNativeFrame),fg_generated_encode,valid,fill,expected,matched}};\n"
            c = folder / "adapter.c"
            c.write_text(text)
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
                    "-I" + str(source),
                    "-I" + str(ROOT / "agent/include"),
                    "-I" + str(ROOT / "runtime/include"),
                    "-I" + str(ROOT / "sdk/include"),
                    str(c),
                    "-o",
                    str(obj),
                ]
            )
            manifest.append(dict(point, group=group))
    if {p["hook"] for p in manifest} != set(range(1, 40)):
        raise RuntimeError("incomplete adapters")
    table = out / "table.c"
    table.write_text(
        '#include "adapter.h"\n'
        + "".join(f"extern const FgModelAdapter fg_dyn_{i};\n" for i in range(1, 40))
        + "const FgModelAdapter *fg_model_adapters[40]={0,"
        + ",".join(f"&fg_dyn_{i}" for i in range(1, 40))
        + "};\n"
    )
    run(
        [
            "cc",
            "-shared",
            "-fPIC",
            "-std=c11",
            "-Wall",
            "-Wextra",
            "-Werror",
            "-I" + str(source),
            "-I" + str(ROOT / "agent/include"),
            "-I" + str(ROOT / "runtime/include"),
            "-I" + str(ROOT / "sdk/include"),
            str(table),
            str(source / "runtime.c"),
            str(source / "properties.c"),
            str(ROOT / "runtime/src/objects.c"),
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
paths = (
    [p for p in out.rglob("*") if p.is_file()]
    + [p for p in source.iterdir() if p.is_file()]
    + [ROOT / "runtime/src/objects.c"]
)
(out / "artifacts.json").write_text(
    json.dumps({str(p): hashlib.sha256(p.read_bytes()).hexdigest() for p in paths}, indent=2) + "\n"
)
print(out)
