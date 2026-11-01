use fingolfin::{
    resolve, sema,
    source::{LoadedPackage, Manifest, Module, Package},
    syntax, types,
};
use std::collections::BTreeMap;
fn check(source: &str) -> Result<sema::CheckedBodies, Vec<syntax::Diagnostic>> {
    let ast = syntax::parse("body.fg", source).unwrap();
    let package = LoadedPackage {
        manifest: Manifest {
            availability: vec![],
            package: Package {
                name: "test".into(),
                version: "0.1.0".into(),
                source: "src".into(),
            },
        },
        modules: BTreeMap::from([(
            "".into(),
            Module {
                name: "".into(),
                file: "body.fg".into(),
                source_hash: String::new(),
                ast,
            },
        )]),
        source_hash: String::new(),
    };
    let index = resolve::collect(&package)?;
    let types = types::check(&index)?;
    sema::check(&index, &types)
}
#[test]
fn typed_calls_mutation_and_total_returns() {
    let result = check("fn add(x: u32, y: u32) -> u32 { return x+y; } fn f(n: u32) -> u32 { var x: u32 = 0; if n > 0 { x = add(n,1); } else { x = 2; } return x; }").unwrap();
    assert!(result.calls["test::f"].contains("test::add"));
}
#[test]
fn enum_context_struct_reads_and_signed_minimum() {
    check("enum E: u8 { a=0, b=1 } struct S { x: u32 } fn choose() -> E { return .b; } fn read(s: S) -> u32 { return s.x; } fn min() -> i8 { return - 128; }").unwrap();
}
#[test]
fn invalid_bodies_are_rejected() {
    for (source, expected) in [
        ("fn f() { let x = missing; }", "unknown variable"),
        ("fn f() { let x=0; x=1; }", "immutable"),
        ("fn f() { var x: u8=256; }", "does not fit"),
        ("fn f(x: u32) { let x=0; }", "duplicate local"),
        ("fn f() { if true { let x=0; } x=1; }", "unknown variable"),
        ("fn f() -> u32 { if true { return 1; } }", "not all paths"),
        ("fn f() { return 1; }", "return value"),
        ("fn f() -> u32 { return; }", "return value"),
        ("fn f() { if 1 {} }", "integer context"),
        ("fn f() {} fn g() { let x=f(); }", "void value"),
        ("fn f(x: u8) {} fn g() { f(1,2); }", "argument count"),
        ("fn f(x: u8) {} fn g() { f(true); }", "type mismatch"),
        ("fn f() { return; let x=1; }", "unreachable"),
        ("fn f() { let x=1/0; }", "division by zero"),
        ("fn f() { let x=true+false; }", "arithmetic"),
        ("fn f() { let x=true < false; }", "comparison"),
        ("fn f(x: u32) { let n=-x; }", "signed integer"),
        (
            "struct S { x: u8 } fn f(s: *S) { let x=s.x; }",
            "implicit pointer",
        ),
        (
            "struct S { x: u8 } fn f(s: S) { s.x=1; }",
            "writes are forbidden",
        ),
        (
            "struct S { x: u8 } fn f(s: S) { let x=s.nope; }",
            "unknown field",
        ),
        (
            "hook root.hit(); fn watch on root.hit() {} fn f() { watch(); }",
            "hook entries",
        ),
    ] {
        let errors = check(source).unwrap_err();
        assert!(
            errors.iter().any(|e| e.message.contains(expected)),
            "{source}: {errors:?}"
        );
        assert!(
            errors
                .iter()
                .all(|e| e.file == "body.fg" && e.span.end <= source.len())
        );
    }
}
#[test]
fn direct_and_mutual_recursion_fail() {
    for source in ["fn f() { f(); }", "fn f() { g(); } fn g() { f(); }"] {
        assert!(
            check(source)
                .unwrap_err()
                .iter()
                .any(|e| e.message.contains("recursive"))
        );
    }
}
#[test]
fn independent_errors_are_batched_and_branch_locals_do_not_escape() {
    let errors = check("fn f() { a(); b(); } fn g() { let x=z; }").unwrap_err();
    assert_eq!(errors.len(), 3);
    check("fn f(x: u8) -> u8 { if x==0 { let x: u8=1; return x; } else { return x; } }").unwrap();
}
#[test]
fn undeclared_pattern_objects_never_silently_pass() {
    let errors = check("hook root.hit(); fn watch on root.hit() where root: {} {}").unwrap_err();
    assert!(
        errors
            .iter()
            .any(|e| e.message.contains("unknown model object"))
    );
}

#[test]
fn nested_patterns_bind_typed_values_into_helpers() {
    check("object root { id: u32 } object root.child { value: u8 } hook root.child.hit(); fn consume(n: u8) {} fn watch on root.child.hit() where root: { id: 0, child: { value: 1..=9 as n } } { consume(n); }").unwrap();
}
#[test]
fn pattern_branches_and_bindings_are_checked() {
    let prelude = "object root { id: u8, enabled: bool } hook root.hit(); ";
    for (entry, expected) in [
        (
            "fn w on root.hit() where root: { missing: 0 } {}",
            "unknown scalar",
        ),
        (
            "fn w on root.hit() where root: { id: 256 } {}",
            "does not fit",
        ),
        (
            "fn w on root.hit() where root: { enabled: 0..2 } {}",
            "range requires",
        ),
        (
            "fn w on root.hit() where root: { id: 0 as n } or root: { id: 1 } {}",
            "or branches",
        ),
        (
            "fn w on root.hit() where not root: { id: 0 as n } { let x=n; }",
            "unknown variable",
        ),
        (
            "fn w on root.hit() where root: { id: 0, id: 1 } {}",
            "duplicate pattern field",
        ),
        (
            "fn w on root.hit() where root: { id: 0 as n, enabled: true as n } {}",
            "duplicate pattern binding",
        ),
    ] {
        let errors = check(&format!("{prelude}{entry}")).unwrap_err();
        assert!(
            errors.iter().any(|e| e.message.contains(expected)),
            "{entry}: {errors:?}"
        );
    }
    check(&format!("{prelude}fn w on root.hit() where root: {{ id: 0 as n }} or root: {{ id: 1 as n }} {{ let x: u8=n; }}")).unwrap();
}
