use fingolfin::{codegen, compiler, eval::Evaluator, pattern::Value, source};
use std::{
    fs,
    path::Path,
    process::Command,
    sync::atomic::{AtomicU64, Ordering},
};
static NEXT: AtomicU64 = AtomicU64::new(0);
fn check(text: &str) -> compiler::CheckedProgram {
    compiler::check_source(
        "native.fg",
        text,
        &source::load(Path::new("models/fixture")).unwrap(),
    )
    .unwrap()
}
fn run(source: &str) -> String {
    let dir = std::env::temp_dir().join(format!(
        "fg-native-{}-{}",
        std::process::id(),
        NEXT.fetch_add(1, Ordering::Relaxed)
    ));
    fs::create_dir(&dir).unwrap();
    fs::write(dir.join("program.c"), source).unwrap();
    let compiled = Command::new("clang")
        .args([
            "-std=c11",
            "-O2",
            "-Wall",
            "-Wextra",
            "-Werror",
            "-fsanitize=undefined",
            "-fno-sanitize-recover=all",
        ])
        .arg(dir.join("program.c"))
        .arg("-o")
        .arg(dir.join("program"))
        .output()
        .unwrap();
    assert!(
        compiled.status.success(),
        "{}\n{source}",
        String::from_utf8_lossy(&compiled.stderr)
    );
    let output = Command::new(dir.join("program")).output().unwrap();
    assert!(
        output.status.success(),
        "{}",
        String::from_utf8_lossy(&output.stderr)
    );
    let result = String::from_utf8(output.stdout).unwrap();
    fs::remove_dir_all(dir).unwrap();
    result
}
#[test]
fn generated_arithmetic_and_control_flow_match_reference() {
    let p = check(
        "fn helper(x:i64)->i64{return x*3;} fn run(x:i64)->i64 {var n=x; if x>5 {let n=helper(x); if n>100 {return n-7;}} else {n=n+2;} return n/2;} fn small(x:u8)->u8{return x*3;} fn short(z:i64)->bool{return z==0 || 1/z>0;}",
    );
    let generated = codegen::generate(&p).unwrap();
    let mut c = generated.source + "\n#include <stdio.h>\nint main(void) {\n";
    let mut expected = String::new();
    for input in -20..100 {
        for name in ["run", "short"] {
            let id = format!("program::{name}");
            let result = Evaluator::new(&p, 10000, 64)
                .call(&id, vec![Value::Integer(input)])
                .unwrap()
                .unwrap();
            let value = match result {
                Value::Integer(n) => n,
                Value::Bool(b) => i128::from(b),
                _ => panic!(),
            };
            expected.push_str(&format!("{value} 0\n"));
            c.push_str(&format!("{{FgExecContext ctx={{10000,0,64,0}}; long long n={}(&ctx,{input}); printf(\"%lld %u\\n\",n,ctx.error); if(ctx.depth) return 1;}}\n",generated.symbols[&id]));
        }
    }
    for input in 0..=255 {
        let result =
            Evaluator::new(&p, 10000, 64).call("program::small", vec![Value::Integer(input)]);
        let line = if let Ok(Some(Value::Integer(n))) = result {
            format!("{n} 0\n")
        } else {
            "0 2\n".into()
        };
        expected.push_str(&line);
        c.push_str(&format!("{{FgExecContext ctx={{10000,0,64,0}}; unsigned n={}(&ctx,{input}); printf(\"%u %u\\n\",n,ctx.error);}}\n",generated.symbols["program::small"]));
    }
    c.push_str("return 0;}\n");
    assert_eq!(run(&c), expected);
}
#[test]
fn generated_code_checks_extremes_division_budget_and_depth() {
    let p = check(
        "fn mul(a:u64,b:u64)->u64{return a*b;} fn div(a:i64,b:i64)->i64{return a/b;} fn rem(a:i64,b:i64)->i64{return a%b;} fn neg(a:i64)->i64{return -a;} fn root()->i64{return div(8,2);}",
    );
    let g = codegen::generate(&p).unwrap();
    let mut c = g.source + "\n#include <assert.h>\nint main(void){\n";
    for (name, args, error) in [
        ("mul", "UINT64_MAX,UINT64_MAX", 2),
        ("div", "INT64_MIN,-1", 2),
        ("div", "1,0", 2),
        ("neg", "INT64_MIN", 2),
        ("rem", "INT64_MIN,-1", 0),
    ] {
        c.push_str(&format!("{{FgExecContext ctx={{1000,0,64,0}}; (void){}(&ctx,{args});assert(ctx.error=={error});assert(ctx.depth==0);}}\n",g.symbols[&format!("program::{name}")]));
    }
    for (budget, depth, error) in [(0, 64, 1), (1000, 1, 4), (1000, 64, 0)] {
        c.push_str(&format!("{{FgExecContext ctx={{{budget},0,{depth},0}}; (void){}(&ctx);assert(ctx.error=={error});assert(ctx.depth==0);}}\n",g.symbols["program::root"]));
    }
    c.push_str("return 0;}");
    run(&c);
}
#[test]
fn generated_struct_enum_pointer_and_entry_binding_code_compiles() {
    let model = source::load(Path::new("models/fixture")).unwrap();
    let mut text = include_str!("fixtures/valid_observation.fg").to_owned();
    text.push_str("enum Mode:u8 {low=0,high=1} struct S {flag:bool,mode:Mode,n:u32} fn copy(s:S)->S{return s;} fn ptr(p:*u8)->*u8{return p;}");
    let p = compiler::check_source("native.fg", &text, &model).unwrap();
    let g = codegen::generate(&p).unwrap();
    let st = fingolfin::abi::identifier("program::S");
    let flag = fingolfin::abi::identifier("flag");
    let mode = fingolfin::abi::identifier("mode");
    let mut c = g.source + "\n#include <assert.h>\nint main(void){\n";
    c.push_str(&format!("{st} s={{0}}; FgExecContext ctx={{10000,0,64,0}}; s={copy}(&ctx,s); assert(ctx.error==0); s.{flag}=2; s={copy}(&ctx,s); assert(ctx.error==3); ctx.error=0; s.{flag}=0;s.{mode}=2;s={copy}(&ctx,s);assert(ctx.error==3);",copy=g.symbols["program::copy"]));
    let cb = fingolfin::abi::identifier("fixture::types.CbHandle");
    c.push_str(&format!("ctx.error=0; {cb} cb={{0}}; {}(&ctx,0,cb,UINT32_MAX,16); assert(ctx.error==2); assert(ctx.depth==0); ctx.error=0; uint8_t byte=1; assert({}(&ctx,&byte)==&byte); assert(ctx.error==0);",g.symbols["program::push"],g.symbols["program::ptr"]));
    c.push_str("return 0;}");
    run(&c);
}

#[test]
fn unsupported_direct_array_abi_is_not_silently_decayed() {
    let p = check("fn copy(a:[u8;2])->[u8;2]{return a;}");
    assert!(codegen::generate(&p).is_err());
}
