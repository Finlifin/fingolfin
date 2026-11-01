//! Native C11 backend for typed function bodies. Entry bindings are explicit
//! arguments; snapshot planning and VM frame adapters are not generated here.
use crate::{
    abi,
    compiler::CheckedProgram,
    ir::{self, Binary, Expression, ExpressionKind as E, Statement, StatementKind as S, Unary},
    syntax::{ItemKind, Literal},
    types::Ty,
};
use std::{collections::BTreeMap, fmt::Write};
pub struct Output {
    pub source: String,
    pub symbols: BTreeMap<String, String>,
}
pub fn symbol(id: &str) -> String {
    format!("fg_exec_{}", abi::identifier(id))
}
struct Generator<'a> {
    program: &'a CheckedProgram,
    module: &'a ir::Module,
    out: String,
    next: u64,
}
impl Generator<'_> {
    fn line(&mut self, s: impl AsRef<str>) {
        writeln!(self.out, "{}", s.as_ref()).unwrap();
    }
    fn fresh(&mut self) -> String {
        let n = self.next;
        self.next += 1;
        format!("t{n}")
    }
    fn step(&mut self) {
        self.line("if (!fgc_step(ctx)) goto fg_fail;");
    }
    fn temp(&mut self, ty: &Ty) -> Result<String, String> {
        if matches!(ty, Ty::Array(..)) {
            return Err(
                "direct array expression requires a struct wrapper in the C backend".into(),
            );
        }
        let name = self.fresh();
        self.line(format!("{};", abi::declaration(ty, &name)));
        Ok(name)
    }
    fn validate(&mut self, name: &str, ty: &Ty) {
        self.step();
        match ty {
            Ty::Bool => self.line(format!("if ({name}>1) {{ctx->error=3;goto fg_fail;}}")),
            Ty::Enum(id) => self.line(format!(
                "if (!{}_valid({name})) {{ctx->error=3;goto fg_fail;}}",
                abi::identifier(id)
            )),
            Ty::Struct(id) => {
                for (field, ty) in &self.program.types.fields[id] {
                    self.validate(&format!("({name}).{}", abi::identifier(field)), ty);
                }
            }
            Ty::Array(inner, n) => {
                let i = self.fresh();
                self.line(format!("for (uint64_t {i}=0;{i}<UINT64_C({n});++{i}) {{"));
                self.validate(&format!("({name})[{i}]"), inner);
                self.line("}");
            }
            _ => {}
        }
    }
    fn expression(&mut self, e: &Expression) -> Result<Option<String>, String> {
        self.step();
        if let E::Helper {
            intrinsic,
            arguments,
        } = &e.kind
        {
            let mut args = Vec::new();
            for arg in arguments {
                args.push(self.value(arg)?);
            }
            args.resize(5, "0".into());
            self.line(format!(
                "if (fg_helper_call_{}(ctx,{})) {{ctx->error=6;goto fg_fail;}}",
                intrinsic.id(),
                args.join(",")
            ));

            return Ok(None);
        }
        if let E::Call { target, arguments } = &e.kind {
            let mut args = vec!["ctx".to_owned()];
            for arg in arguments {
                args.push(self.value(arg)?);
            }
            let call = format!("{}({})", symbol(target), args.join(","));
            let result = if let Some(ty) = &e.ty {
                let t = self.temp(ty)?;
                self.line(format!("{t}={call};"));
                Some(t)
            } else {
                self.line(format!("{call};"));
                None
            };
            self.line("if (ctx->error) goto fg_fail;");
            return Ok(result);
        }
        let ty = e.ty.as_ref().ok_or("void expression")?;
        let result = self.temp(ty)?;
        match &e.kind {
            E::Constant(l) => {
                let value = match l {
                    Literal::Integer(n) => abi::constant(*n),
                    Literal::Bool(b) => u8::from(*b).to_string(),
                    Literal::Variant(name) => {
                        let Ty::Enum(id) = ty else {
                            return Err("enum literal without enum type".into());
                        };
                        let ItemKind::Enum { variants, .. } =
                            &self.program.index.symbols[id].declaration.value.kind
                        else {
                            return Err("missing enum".into());
                        };
                        abi::constant(
                            variants
                                .iter()
                                .find(|v| v.value.0 == *name)
                                .ok_or("unknown enum variant")?
                                .value
                                .1,
                        )
                    }
                    Literal::String(_) => return Err("string output not implemented".into()),
                };
                self.line(format!("{result}={value};"));
            }
            E::Local(id) => self.line(format!("{result}=v{id};")),
            E::Field { base, name, .. } => {
                let base = self.value(base)?;
                self.line(format!("{result}=({base}).{};", abi::identifier(name)));
            }
            E::And(left, right) | E::Or(left, right) => {
                let left = self.value(left)?;
                self.line(format!("{result}={left};"));
                let test = if matches!(e.kind, E::And(..)) {
                    result.clone()
                } else {
                    format!("!{result}")
                };
                self.line(format!("if ({test}) {{"));
                let right = self.value(right)?;
                self.line(format!("{result}={right};"));
                self.line("}");
            }
            E::Unary { op, operand } => {
                let value = self.value(operand)?;
                match op {
                Unary::Not=>self.line(format!("{result}=!{value};")),
                Unary::Negate=>self.line(format!("if (__builtin_sub_overflow(0,{value},&{result})) {{ctx->error=2;goto fg_fail;}}"))
            }
            }
            E::Binary { op, left, right } => {
                let a = self.value(left)?;
                let b = self.value(right)?;
                match op {
                    Binary::Multiply => {
                        // A volatile bound prevents LLVM from folding the guard
                        // back into umul.with.overflow and an unsupported BPF
                        // __multi3 runtime call. Keep the BPF symbol test.

                        let Some(Ty::Integer { signed, bits }) = left.ty else {
                            return Err("integer multiply requires type".into());
                        };
                        let ma = self.fresh();
                        let mb = self.fresh();
                        let bound = self.fresh();
                        let product = self.fresh();
                        if signed {
                            self.line(format!(
                                "uint64_t {ma}={a}<0 ? UINT64_C(0)-(uint64_t){a} : (uint64_t){a};"
                            ));
                            self.line(format!(
                                "uint64_t {mb}={b}<0 ? UINT64_C(0)-(uint64_t){b} : (uint64_t){b};"
                            ));
                            self.line(format!(
                                "volatile uint64_t {bound}=(({a}<0)!=({b}<0)) ? {} : {};",
                                abi::constant(1i128 << (bits - 1)),
                                abi::constant((1i128 << (bits - 1)) - 1)
                            ));
                        } else {
                            self.line(format!(
                                "uint64_t {ma}={a}, {mb}={b}; volatile uint64_t {bound}={};",
                                abi::constant((1i128 << bits) - 1)
                            ));
                        }
                        self.line(format!(
                            "if ({mb} && {ma}>{bound}/{mb}) {{ctx->error=2;goto fg_fail;}}"
                        ));
                        self.line(format!("uint64_t {product}={ma}*{mb};"));
                        if signed {
                            self.line(format!("{result}=(({a}<0)!=({b}<0)) ? ({product} ? -((int64_t)({product}-1))-1 : 0) : (int64_t){product};"));
                        } else {
                            self.line(format!("{result}={product};"));
                        }
                    }
                    Binary::Add | Binary::Subtract => {
                        let name = match op {
                            Binary::Add => "add",
                            Binary::Subtract => "sub",
                            _ => "mul",
                        };
                        self.line(format!("if (__builtin_{name}_overflow({a},{b},&{result})) {{ctx->error=2;goto fg_fail;}}"));
                    }
                    Binary::Divide | Binary::Remainder => {
                        self.line(format!("if ({b}==0) {{ctx->error=2;goto fg_fail;}}"));
                        let operator = if *op == Binary::Divide { "/" } else { "%" };
                        if let Some(Ty::Integer { signed: true, bits }) = left.ty {
                            if *op == Binary::Divide {
                                let min = abi::constant(-(1i128 << (bits - 1)));
                                self.line(format!(
                                    "if ({a}=={min} && {b}==-1) {{ctx->error=2;goto fg_fail;}}"
                                ));
                            }
                            let ma = self.fresh();
                            let mb = self.fresh();
                            let q = self.fresh();
                            self.line(format!(
                                "uint64_t {ma}={a}<0 ? UINT64_C(0)-(uint64_t){a} : (uint64_t){a};"
                            ));
                            self.line(format!(
                                "uint64_t {mb}={b}<0 ? UINT64_C(0)-(uint64_t){b} : (uint64_t){b};"
                            ));
                            self.line(format!("uint64_t {q}={ma}{operator}{mb};"));
                            let sign = if *op == Binary::Divide {
                                format!("(({a}<0)!=({b}<0))")
                            } else {
                                format!("({a}<0)")
                            };
                            self.line(format!("{result}={sign} ? ({q} ? -((int64_t)({q}-1))-1 : 0) : (int64_t){q};"));
                        } else {
                            self.line(format!("{result}={a}{operator}{b};"));
                        }
                    }
                    _ => {
                        let operator = match op {
                            Binary::Equal => "==",
                            Binary::NotEqual => "!=",
                            Binary::Less => "<",
                            Binary::LessEqual => "<=",
                            Binary::Greater => ">",
                            Binary::GreaterEqual => ">=",
                            _ => unreachable!(),
                        };
                        self.line(format!("{result}=({a}{operator}{b});"));
                    }
                }
            }
            E::Call { .. } | E::Helper { .. } => unreachable!(),
        }
        Ok(Some(result))
    }
    fn value(&mut self, e: &Expression) -> Result<String, String> {
        self.expression(e)?.ok_or("void value".into())
    }
    fn block(&mut self, body: &[Statement]) -> Result<(), String> {
        for stmt in body {
            self.step();
            match &stmt.kind {
                S::Initialize { local, value } | S::Assign { local, value } => {
                    let v = self.value(value)?;
                    self.line(format!("v{local}={v};"));
                }
                S::Evaluate(e) => {
                    if let Some(v) = self.expression(e)? {
                        self.line(format!("(void){v};"));
                    }
                }
                S::Return(e) => {
                    let v = e.as_ref().map(|e| self.value(e)).transpose()?;
                    self.line("--ctx->depth;");
                    self.line(format!("return {};", v.unwrap_or_default()));
                }
                S::If {
                    condition,
                    then_body,
                    else_body,
                } => {
                    let c = self.value(condition)?;
                    self.line(format!("if ({c}) {{"));
                    self.block(then_body)?;
                    self.line("} else {");
                    self.block(else_body)?;
                    self.line("}");
                }
            }
        }
        Ok(())
    }
    fn signature(&self, id: &str, f: &ir::Function) -> String {
        let mut args = vec!["FgExecContext *ctx".into()];
        for slot in f.parameters.iter().chain(f.bindings.values()) {
            args.push(abi::declaration(
                &f.locals[*slot as usize].ty,
                &format!("v{slot}"),
            ));
        }
        let name = format!("{}({})", symbol(id), args.join(","));
        f.returns
            .as_ref()
            .map(|ty| abi::declaration(ty, &name))
            .unwrap_or_else(|| format!("void {name}"))
    }
    fn function(&mut self, id: &str, f: &ir::Function) -> Result<(), String> {
        self.line(format!("{} {{", self.signature(id, f)));
        let zero = if let Some(ty) = &f.returns {
            self.line(format!("{}={{0}};", abi::declaration(ty, "fg_zero")));
            "fg_zero"
        } else {
            ""
        };
        self.line(format!("if (!ctx) return {zero};"));
        self.line(format!("if (ctx->error) return {zero};"));
        self.line(format!("if (!fgc_step(ctx)) return {zero};"));
        self.line(format!(
            "if (ctx->depth>=ctx->max_depth) {{ctx->error=4;return {zero};}}"
        ));
        self.line("++ctx->depth;");
        for (i, local) in f.locals.iter().enumerate() {
            if !f.parameters.contains(&(i as u32)) && !f.bindings.values().any(|v| *v == i as u32) {
                self.line(format!(
                    "{};",
                    abi::declaration(&local.ty, &format!("v{i}"))
                ));
            }
            self.line(format!("(void)&v{i};"));
        }
        for slot in f.parameters.iter().chain(f.bindings.values()) {
            self.validate(&format!("v{slot}"), &f.locals[*slot as usize].ty);
        }
        self.block(&f.body)?;
        if f.returns.is_some() {
            self.line("ctx->error=3;");
        }
        self.line("goto fg_fail;");
        self.line(format!("fg_fail: --ctx->depth; return {zero};"));
        self.line("}");
        Ok(())
    }
}
pub fn generate(program: &CheckedProgram) -> Result<Output, String> {
    let module = ir::lower(program)?;
    let header = abi::generate(&program.index, &program.types)?;
    let mut g = Generator {
        program,
        module: &module,
        out: header.source.replace("#pragma once\n", ""),
        next: 0,
    };
    g.line("/* error: 0=ok, 1=budget, 2=arithmetic, 3=invalid value, 4=depth */\ntypedef struct { uint64_t remaining; uint32_t depth, max_depth, error; } FgExecContext;\nstatic inline int fgc_step(FgExecContext *ctx) { if (!ctx->remaining) {ctx->error=1;return 0;} --ctx->remaining;return 1; }");
    g.line("#ifndef __bpf__\nextern uint64_t fg_native_helper(uint32_t, void*, uint64_t, uint64_t, uint64_t, uint64_t, uint64_t);\n#endif");
    for id in [1, 2, 3, 4] {
        g.line(format!("#ifdef __bpf__\nstatic uint64_t (*const fg_helper_{id})(uint64_t,uint64_t,uint64_t,uint64_t,uint64_t)=(void*){id};\n#define fg_helper_call_{id}(ctx,a,b,c,d,e) fg_helper_{id}(a,b,c,d,e)\n#else\n#define fg_helper_call_{id}(ctx,a,b,c,d,e) fg_native_helper({id},ctx,a,b,c,d,e)\n#endif"));
    }
    for (id, f) in &g.module.functions {
        g.line(format!("{};", g.signature(id, f)));
    }
    for (id, f) in &g.module.functions {
        g.function(id, f)?;
    }
    Ok(Output {
        source: g.out,
        symbols: module
            .functions
            .keys()
            .map(|id| (id.clone(), symbol(id)))
            .collect(),
    })
}

/// Function invocation frame for BPF. Hook pattern bindings remain explicit;
/// callers must establish matching/lifecycle before invoking an entry body.
pub struct BpfOutput {
    pub source: String,
    pub header: String,
    pub function: String,
}
pub fn generate_bpf(program: &CheckedProgram, function: &str) -> Result<BpfOutput, String> {
    let module = ir::lower(program)?;
    let f = module
        .functions
        .get(function)
        .ok_or("unknown BPF function")?;
    let mut source = generate(program)?.source;
    // Inline function bodies to avoid depending on BPF's five-register call ABI
    // for native by-value structs and arbitrarily many source parameters.
    for (id, f) in &module.functions {
        let generator = Generator {
            program,
            module: &module,
            out: String::new(),
            next: 0,
        };
        let signature = generator.signature(id, f);
        source = source.replace(
            &signature,
            &format!("static __attribute__((always_inline)) inline {signature}"),
        );
    }
    source = source.replace(
        "#if !defined(__linux__) || !defined(__x86_64__) || __BYTE_ORDER__ != __ORDER_LITTLE_ENDIAN__",
        "#if (!defined(__bpf__) && (!defined(__linux__) || !defined(__x86_64__))) || __BYTE_ORDER__ != __ORDER_LITTLE_ENDIAN__",
    );
    let mut frame = String::from(
        "\n/* Invocation frame v1; zero-initialize padding. */\nstruct FgBpfFrame { uint64_t steps; uint32_t max_depth; uint32_t error;\n",
    );
    let slots: Vec<_> = f.parameters.iter().chain(f.bindings.values()).collect();
    for (n, slot) in slots.iter().enumerate() {
        writeln!(
            frame,
            "{};",
            abi::declaration(&f.locals[**slot as usize].ty, &format!("arg{n}"))
        )
        .unwrap();
    }
    if let Some(ty) = &f.returns {
        writeln!(frame, "{};", abi::declaration(ty, "result")).unwrap();
    }
    frame.push_str("};\n");
    let layout = crate::frame::Layout::new(program, f)?;
    for (n, offset) in layout.offsets.iter().enumerate() {
        let name = if n < slots.len() {
            format!("arg{n}")
        } else {
            "result".into()
        };
        writeln!(
            frame,
            "_Static_assert(offsetof(struct FgBpfFrame,{name})=={offset}, \"frame field offset\");"
        )
        .unwrap();
    }
    writeln!(
        frame,
        "_Static_assert(sizeof(struct FgBpfFrame)=={}, \"frame size\");",
        layout.size()
    )
    .unwrap();
    writeln!(
        frame,
        "_Static_assert(_Alignof(struct FgBpfFrame)=={}, \"frame alignment\");",
        layout.alignment()
    )
    .unwrap();
    let header = abi::generate(&program.index, &program.types)?.source + &frame;
    source.push_str(&frame);
    source.push_str("\nuint64_t entry(struct FgBpfFrame *frame, uint64_t length) {\nif (!frame || length!=sizeof(*frame)) return 5;\nFgExecContext ctx={frame->steps,0,frame->max_depth,0};\n");
    let mut args = vec!["&ctx".to_string()];
    args.extend((0..slots.len()).map(|n| format!("frame->arg{n}")));
    writeln!(
        source,
        "{}{}({});",
        if f.returns.is_some() {
            "frame->result="
        } else {
            ""
        },
        symbol(function),
        args.join(",")
    )
    .unwrap();
    source.push_str("frame->steps=ctx.remaining; frame->error=ctx.error; return ctx.error;\n}\n");
    Ok(BpfOutput {
        source,
        header,
        function: function.into(),
    })
}

/// Generate a C11 SDK frame and binding encoder for an explicitly selected
/// function. Pattern matching and hook-ID assignment remain the model adapter's
/// responsibility; bindings are explicit fields after ordinary parameters.
pub fn generate_adapter(program: &CheckedProgram, function: &str) -> Result<String, String> {
    let module = ir::lower(program)?;
    let f = module
        .functions
        .get(function)
        .ok_or("unknown adapter function")?;
    let slots: Vec<_> = f.parameters.iter().chain(f.bindings.values()).collect();
    let layout = crate::frame::Layout::new(program, f)?;
    let schema = layout.schema_bytes()?;
    let guard = format!("FG_ADAPTER_{}_H", abi::identifier(function).to_uppercase());
    let mut out = format!("#ifndef {guard}\n#define {guard}\n");
    out.push_str(&generate_bpf(program, function)?.header);
    out.push_str("\n#include <string.h>\n#include \"fingolfin/binding.h\"\n");
    out.push_str("/* Explicit function adapter: one generated adapter per translation unit. */\nstruct FgNativeFrame { FgCallHeader header;\n");
    for (n, slot) in slots.iter().enumerate() {
        writeln!(
            out,
            "{};",
            abi::declaration(&f.locals[**slot as usize].ty, &format!("arg{n}"))
        )
        .unwrap();
    }
    out.push_str("};\nstatic inline FgStatus fg_generated_encode(const FgCallHeader *native, const FgFrameSchema *schema, void *output, size_t bytes) {\n");
    out.push_str("static const unsigned char expected_schema[]={");
    for byte in schema {
        write!(out, "{byte},").unwrap();
    }
    out.push_str("};\nif (!native || !schema || !output || native->abi_version!=FG_ABI_VERSION || native->frame_bytes!=sizeof(struct FgNativeFrame) || bytes!=sizeof(struct FgBpfFrame) || schema->bytes.size!=sizeof(expected_schema) || !schema->bytes.data || memcmp(schema->bytes.data,expected_schema,sizeof(expected_schema))) return FG_STATUS_INVALID;\n");
    // The binding owns disjoint scratch. Byte copies avoid alignment assumptions
    // and avoid putting potentially large native records on the C stack.
    out.push_str("/* Native input and output must not overlap; caller supplies valid typed values. */\nmemset(output,0,bytes);\n");
    for n in 0..slots.len() {
        writeln!(out, "const size_t arg{n}_bytes=sizeof(((struct FgNativeFrame *)0)->arg{n});\nmemcpy((unsigned char *)output+offsetof(struct FgBpfFrame,arg{n}), (const unsigned char *)native+offsetof(struct FgNativeFrame,arg{n}), arg{n}_bytes);").unwrap();
    }
    out.push_str("return fg_frame_schema_validate_inputs(schema,(FgBytes){output,bytes},FG_FRAME_INPUT_CHECK_BUDGET);\n}\n");
    let gate = crate::gate::lower_c(&f.gate)?;
    out.push_str("static inline FgGatePlan fg_generated_gate_plan(void) {\nstatic const FgGateNode nodes[]={\n");
    for n in gate.nodes {
        writeln!(
            out,
            "{{{},{},{},{},{}}},",
            n.op, n.left, n.right, n.test, n.stage
        )
        .unwrap();
    }
    out.push_str("};\nreturn (FgGatePlan){nodes,sizeof(nodes)/sizeof(nodes[0])};\n}\n");
    out.push_str("#include \"fingolfin/facts.h\"\nstatic inline FgStatus fg_generated_predicate(const void *facts, size_t bytes, uint32_t test, uint64_t *remaining, FgTruth *truth) {\n(void)facts;(void)bytes;(void)remaining;(void)truth;\nswitch(test) {\n");
    let pair = |n: i128| {
        format!(
            "{{UINT64_C({}),UINT64_C({})}}",
            n as u128 as u64,
            ((n as u128) >> 64) as u64
        )
    };
    for (index, descriptor) in gate.tests.iter().enumerate() {
        use crate::{gate::Test, syntax::Literal};
        writeln!(out, "case {index}: {{").unwrap();
        let mut kind = 0;
        let mut lo = 0;
        let mut hi = 0;
        let mut inclusive = false;
        let mut text: Option<&str> = None;
        let op = match &descriptor.test {
            Test::Record => 0,
            Test::Known => 1,
            Test::Range {
                lo: a,
                hi: b,
                inclusive: inc,
            } => {
                lo = *a;
                hi = *b;
                inclusive = *inc;
                3
            }
            Test::Literal(literal) => {
                match literal {
                    Literal::Integer(n) => {
                        kind = 3;
                        lo = *n;
                    }
                    Literal::Bool(b) => {
                        kind = 2;
                        lo = i128::from(*b);
                    }
                    Literal::String(s) => {
                        kind = 4;
                        text = Some(s);
                    }
                    Literal::Variant(s) => {
                        kind = 5;
                        text = Some(s);
                    }
                }
                2
            }
        };
        let (pointer, length) = if let Some(text) = text {
            out.push_str("static const unsigned char text[]={");
            for byte in text.as_bytes() {
                write!(out, "{byte},").unwrap();
            }
            out.push_str("0};\n");
            ("text", text.len())
        } else {
            ("NULL", 0)
        };
        writeln!(
            out,
            "static const FgFactTest predicate={{{op},{kind},{},{},{},{pointer},{length}}};",
            u8::from(inclusive),
            pair(lo),
            pair(hi)
        )
        .unwrap();
        writeln!(
            out,
            "return fg_fact_test(facts,bytes,{},&predicate,remaining,truth);\n}}",
            descriptor.read_index
        )
        .unwrap();
    }
    out.push_str("default:return FG_STATUS_INVALID;\n}\n}\n");
    if f.gate.reads.len() > 1024 {
        return Err("adapter exceeds 1024 property reads".into());
    }
    let c_string = |text: &str| {
        let mut literal = String::from("\"");
        for byte in text.bytes() {
            write!(literal, "\\{byte:03o}").unwrap();
        }
        literal.push('"');
        literal
    };
    out.push_str("static inline const FgPropertyRead *fg_generated_reads(size_t *count) {\n");
    for (i, read) in f.gate.reads.iter().enumerate() {
        if read.property.fields.len() > 32
            || read.property.object.len() > 4096
            || read.property.fields.iter().any(|name| name.len() > 4096)
        {
            return Err("adapter property path exceeds C limits".into());
        }
        if !read.property.fields.is_empty() {
            writeln!(out, "static const char *const fields_{i}[]={{").unwrap();
            for name in &read.property.fields {
                writeln!(out, "{},", c_string(name)).unwrap();
            }
            out.push_str("};\n");
        }
    }
    if f.gate.reads.is_empty() {
        out.push_str("if(count){*count=0;}\nreturn NULL;\n}\n");
    } else {
        out.push_str("static const FgPropertyRead reads[]={\n");
        for (i, read) in f.gate.reads.iter().enumerate() {
            let fields = if read.property.fields.is_empty() {
                "NULL".into()
            } else {
                format!("fields_{i}")
            };
            let stage = match read.stage {
                crate::gate::Stage::Attachment => 0,
                crate::gate::Stage::Object => 1,
                crate::gate::Stage::Update => 2,
                crate::gate::Stage::Invocation => 3,
            };
            writeln!(
                out,
                "{{{{{},{fields},{}}},{stage}}},",
                c_string(&read.property.object),
                read.property.fields.len()
            )
            .unwrap();
        }
        out.push_str("};\nif(count){*count=sizeof(reads)/sizeof(reads[0]);}\nreturn reads;\n}\n");
    }
    out.push_str("static inline FgStatus fg_generated_collect(FgGateStage stage,FgPropertyProvider provider,void *context,uint64_t *remaining,FgFactValue *output,size_t capacity) {\nsize_t count;const FgPropertyRead *reads=fg_generated_reads(&count);\nreturn fg_facts_collect(reads,count,stage,provider,context,remaining,output,capacity);\n}\n#endif\n");
    Ok(out)
}
