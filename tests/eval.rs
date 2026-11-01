use fingolfin::{
    compiler::{self, CheckedProgram},
    eval::{Error, Evaluator},
    pattern::Value,
    source,
};
use std::{collections::BTreeMap, path::Path};
fn check(src: &str) -> CheckedProgram {
    compiler::check_source(
        "eval.fg",
        src,
        &source::load(Path::new("models/fixture")).unwrap(),
    )
    .unwrap()
}
#[test]
fn calls_branches_assignment_shadowing_and_early_return() {
    let p = check(
        "fn double(x:u32)->u32{return x*2;} fn run(x:u32)->u32{var n=x; if x>3 { let n=double(x); if n>10 {return n;} } else { n=n+1; } return n;}",
    );
    for (input, expected) in [(2, 3), (4, 4), (6, 12)] {
        let mut e = Evaluator::new(&p, 1000, 16);
        assert_eq!(
            e.call("program::run", vec![Value::Integer(input)]).unwrap(),
            Some(Value::Integer(expected))
        );
        assert!(e.consumed() > 0);
    }
}
#[test]
fn short_circuit_does_not_execute_failing_rhs() {
    let p = check(
        "fn div(z:i64)->bool{return 1/z>0;} fn and(x:bool)->bool{return x && div(0);} fn either(x:bool)->bool{return x || div(0);}",
    );
    let mut e = Evaluator::new(&p, 1000, 16);
    assert_eq!(
        e.call("program::and", vec![Value::Bool(false)]).unwrap(),
        Some(Value::Bool(false))
    );
    assert_eq!(
        e.call("program::either", vec![Value::Bool(true)]).unwrap(),
        Some(Value::Bool(true))
    );
    assert_eq!(
        e.call("program::and", vec![Value::Bool(true)]),
        Err(Error::Arithmetic)
    );
}
#[test]
fn width_overflow_and_invalid_inputs_are_errors() {
    let p = check(
        "fn add(x:u8)->u8{return x+1;} fn neg(x:i8)->i8{return -x;} fn min()->i64{return -9223372036854775808;}",
    );
    let mut e = Evaluator::new(&p, 1000, 16);
    for (f, n) in [("add", 255), ("add", 256), ("add", -1), ("neg", -128)] {
        assert!(
            e.call(&format!("program::{f}"), vec![Value::Integer(n)])
                .is_err()
        );
    }
    assert_eq!(
        e.call("program::min", vec![]).unwrap(),
        Some(Value::Integer(i64::MIN as i128))
    );
    assert!(e.call("program::add", vec![]).is_err());
    assert!(e.call("program::add", vec![Value::Bool(true)]).is_err());
}
#[test]
fn structs_and_enums_validate_at_boundary_and_preserve_values() {
    let p = check(
        "enum Mode:u8 { low=0, high=1 } struct S { n:u32, mode:Mode } fn get(s:S)->u32 {return s.n;} fn high(m:Mode)->bool{return m==.high;} fn copy(s:S)->S{return s;}",
    );
    let s = Value::Record(BTreeMap::from([
        ("n".into(), Value::Integer(42)),
        ("mode".into(), Value::Variant("high".into())),
    ]));
    let mut e = Evaluator::new(&p, 1000, 16);
    assert_eq!(
        e.call("program::get", vec![s.clone()]).unwrap(),
        Some(Value::Integer(42))
    );
    assert_eq!(e.call("program::copy", vec![s.clone()]).unwrap(), Some(s));
    assert_eq!(
        e.call("program::high", vec![Value::Variant("high".into())])
            .unwrap(),
        Some(Value::Bool(true))
    );
    assert!(
        e.call("program::high", vec![Value::Variant("bad".into())])
            .is_err()
    );
    assert!(
        e.call("program::get", vec![Value::Record(BTreeMap::new())])
            .is_err()
    );
}
#[test]
fn budgets_and_call_depth_bound_execution() {
    let p = check("fn leaf()->u32{return 1;} fn root()->u32{return leaf();}");
    assert_eq!(
        Evaluator::new(&p, 0, 16).call("program::root", vec![]),
        Err(Error::BudgetExceeded)
    );
    assert_eq!(
        Evaluator::new(&p, 1000, 1).call("program::root", vec![]),
        Err(Error::DepthExceeded)
    );
    let mut e = Evaluator::new(&p, 1000, 2);
    assert_eq!(
        e.call("program::root", vec![]).unwrap(),
        Some(Value::Integer(1))
    );
    let used = e.consumed();
    assert_eq!(
        Evaluator::new(&p, used - 1, 2).call("program::root", vec![]),
        Err(Error::BudgetExceeded)
    );
    assert!(
        Evaluator::new(&p, used, 2)
            .call("program::root", vec![])
            .is_ok()
    );
}
