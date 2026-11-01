use fingolfin::{
    compiler,
    ir::{self, ExpressionKind as E, StatementKind as S},
    source,
    syntax::Literal,
    types::Ty,
};
use std::path::Path;
fn lower(src: &str) -> ir::Module {
    let model = source::load(Path::new("models/fixture")).unwrap();
    ir::lower(&compiler::check_source("ir.fg", src, &model).unwrap()).unwrap()
}
#[test]
fn shadowed_locals_have_unique_ids_and_initializer_reads_outer_scope() {
    let ir = lower(
        "fn f(x:u32)->u32 {var n=x; if x>0 {let n=n+1; let z=n;} else {let n=x;} n=n+2; return n;}",
    );
    let f = &ir.functions["program::f"];
    assert_eq!(f.parameters, vec![0]);
    assert_eq!(
        f.locals.iter().map(|l| l.name.as_str()).collect::<Vec<_>>(),
        vec!["x", "n", "n", "z", "n"]
    );
    let S::If {
        then_body,
        else_body,
        ..
    } = &f.body[1].kind
    else {
        panic!()
    };
    let S::Initialize { local, value } = &then_body[0].kind else {
        panic!()
    };
    assert_eq!(*local, 2);
    let E::Binary { left, .. } = &value.kind else {
        panic!()
    };
    assert!(matches!(left.kind, E::Local(1)));
    let S::Initialize { value, .. } = &then_body[1].kind else {
        panic!()
    };
    assert!(matches!(value.kind, E::Local(2)));
    assert!(matches!(else_body[0].kind, S::Initialize { local: 4, .. }));
    assert!(matches!(f.body[2].kind, S::Assign { local: 1, .. }));
    let S::Return(Some(value)) = &f.body[3].kind else {
        panic!()
    };
    assert!(matches!(value.kind, E::Local(1)));
}
#[test]
fn calls_are_canonical_and_short_circuit_and_signed_minimum_are_explicit() {
    let ir = lower(
        "fn good(x:bool)->bool{return x;} fn f(x:bool)->bool{return x && good(x) || false;} fn min()->i64{return -9223372036854775808;}",
    );
    let S::Return(Some(value)) = &ir.functions["program::f"].body[0].kind else {
        panic!()
    };
    let E::Or(left, _) = &value.kind else {
        panic!()
    };
    let E::And(_, right) = &left.kind else {
        panic!()
    };
    let E::Call { target, .. } = &right.kind else {
        panic!()
    };
    assert_eq!(target, "program::good");
    assert_eq!(right.ty, Some(Ty::Bool));
    let S::Return(Some(value)) = &ir.functions["program::min"].body[0].kind else {
        panic!()
    };
    assert!(matches!(value.kind,E::Constant(Literal::Integer(n)) if n==i64::MIN as i128));
}
#[test]
fn field_offsets_and_types_come_from_checked_layouts() {
    let ir = lower(
        "struct Inner { n:u32 } struct Outer { pad:u8, inner:Inner } fn f(o:Outer)->u32{return o.inner.n;}",
    );
    let S::Return(Some(value)) = &ir.functions["program::f"].body[0].kind else {
        panic!()
    };
    let E::Field { base, offset, .. } = &value.kind else {
        panic!()
    };
    assert_eq!(*offset, 0);
    assert!(matches!(base.kind, E::Field { offset: 4, .. }));
    assert_eq!(
        value.ty,
        Some(Ty::Integer {
            signed: false,
            bits: 32
        })
    );
}
#[test]
fn entry_bindings_are_typed_and_lowering_is_deterministic() {
    let model = source::load(Path::new("models/fixture")).unwrap();
    let p = compiler::check_source(
        "fixture.fg",
        include_str!("fixtures/valid_observation.fg"),
        &model,
    )
    .unwrap();
    let first = ir::lower(&p).unwrap();
    let second = ir::lower(&p).unwrap();
    assert_eq!(
        serde_json::to_vec(&first).unwrap(),
        serde_json::to_vec(&second).unwrap()
    );
    let f = &first.functions["program::push"];
    let slot = f.bindings["cb_id"];
    assert_eq!(
        f.locals[slot as usize].ty,
        Ty::Integer {
            signed: false,
            bits: 32
        }
    );
    assert!(f.pattern.is_some());
    assert_eq!(f.hook.as_deref(), Some("fixture.device.core.cb.post_push"));
    assert_eq!(first.model_source_hash, model.source_hash);
}
