use fingolfin::{compiler, source};
use std::path::Path;
const PROGRAM: &str = "use fixture.types.{ObsCall,CbHandle}; fn pages_plus_one(p: u32) -> u32 { return p+1; } fn watch on fixture.device.core.cb.post_push(call: *ObsCall, cb: CbHandle, pages: u32) where fixture: { device: { core: { cb: { id: 16 as id } } } } { let n=pages_plus_one(pages); let selected: u32=id; }";
#[test]
fn standalone_program_retains_model_identity_and_typechecks_bindings() {
    let model = source::load(Path::new("models/fixture")).unwrap();
    let checked = compiler::check_source("watch.fg", PROGRAM, &model).unwrap();
    assert_eq!(checked.entries, vec!["program::watch"]);
    assert_eq!(
        checked.index.resolve("", "CbHandle").unwrap().id,
        "fixture::types.CbHandle"
    );
    assert_eq!(checked.model_source_hash, model.source_hash);
    assert_eq!(checked.program_source_hash.len(), 64);
    assert!(checked.bodies.calls["program::watch"].contains("program::pages_plus_one"));
}
#[test]
fn program_cannot_inject_schema_or_replace_model_types() {
    let model = source::load(Path::new("models/fixture")).unwrap();
    for program in [
        "hook injected.hit();",
        "object injected { id: u32 }",
        "struct CbHandle { id: u64 } use fixture.types.ObsCall; fn watch on fixture.device.core.cb.post_push(call: *ObsCall, cb: CbHandle, pages: u32) {}",
    ] {
        assert!(compiler::check_source("bad.fg", program, &model).is_err());
    }
}
#[test]
fn model_helpers_cannot_capture_program_names() {
    let mut model = source::load(Path::new("models/fixture")).unwrap();
    model.modules.get_mut("").unwrap().ast.items.extend(
        fingolfin::syntax::parse("model.fg", "pub fn helper() { missing(); }")
            .unwrap()
            .items,
    );
    let errors = compiler::check_source("watch.fg", "fn missing() {}", &model).unwrap_err();
    assert!(errors.iter().any(|e| e.message.contains("unresolved")));
}
#[test]
fn private_model_symbols_remain_private() {
    let mut model = source::load(Path::new("models/fixture")).unwrap();
    model.modules.get_mut("types").unwrap().ast.items[0]
        .value
        .public = false;
    assert!(compiler::check_source("watch.fg", PROGRAM, &model).is_err());
}
#[test]
fn qualified_model_function_keeps_its_own_lexical_scope() {
    let mut model = source::load(Path::new("models/fixture")).unwrap();
    model.modules.get_mut("").unwrap().ast.items.extend(fingolfin::syntax::parse("model.fg","fn inner(x: u32) -> u32 { return x; } pub fn helper(x: u32) -> u32 { return inner(x); }").unwrap().items);
    let result = compiler::check_source(
        "watch.fg",
        "fn inner(x: bool) {} fn f() { let x=fixture.helper(1); }",
        &model,
    )
    .unwrap();
    assert!(result.bodies.calls["fixture::helper"].contains("fixture::inner"));
}
