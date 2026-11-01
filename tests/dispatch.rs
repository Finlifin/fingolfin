use fingolfin::{
    compiler,
    eval::{Error, Evaluator},
    pattern::Value,
    source, syntax,
};
use std::{collections::BTreeMap, path::Path};
fn program(body: &str) -> compiler::CheckedProgram {
    let mut model = source::load(Path::new("models/fixture")).unwrap();
    model.modules.get_mut("").unwrap().ast.items.extend(
        syntax::parse(
            "extra.fg",
            "pub object root { a:u8, b:u8 } pub hook root.hit(n:u32); pub hook root.other(n:u32);",
        )
        .unwrap()
        .items,
    );
    compiler::check_source("dispatch.fg", body, &model).unwrap()
}
fn snapshot(a: Value, b: Value) -> BTreeMap<String, Value> {
    BTreeMap::from([(
        "root".into(),
        Value::Record(BTreeMap::from([("a".into(), a), ("b".into(), b)])),
    )])
}
const PROGRAM: &str = "fn add(x:u32)->u32{return x+1;} fn watch on root.hit(n:u32) where root: {a: _ as x} or root: {b: _ as x} {let v:u8=x; let next=add(n);} fn second on root.hit(n:u32) {} fn unrelated on root.other(n:u32) { let z:u32=0; let boom=n/z; }";
#[test]
fn dispatch_routes_entries_and_deduplicates_binding_rows() {
    let p = program(PROGRAM);
    for (a, b, count) in [(1, 1, 2), (1, 2, 3)] {
        let result = Evaluator::new(&p, 10000, 16)
            .dispatch(
                "root.hit",
                vec![Value::Integer(4)],
                &snapshot(Value::Integer(a), Value::Integer(b)),
            )
            .unwrap();
        assert!(!result.incomplete);
        assert_eq!(result.invocations.len(), count);
        assert!(
            result
                .invocations
                .iter()
                .all(|i| i.entry != "program::unrelated")
        );
        assert!(
            result
                .invocations
                .iter()
                .filter(|i| i.entry == "program::watch")
                .all(|i| i.bindings.contains_key("x"))
        );
    }
}
#[test]
fn unknown_snapshot_keeps_known_matches_and_reports_incomplete() {
    let p = program(PROGRAM);
    let result = Evaluator::new(&p, 10000, 16)
        .dispatch(
            "root.hit",
            vec![Value::Integer(4)],
            &snapshot(Value::Integer(1), Value::Unknown),
        )
        .unwrap();
    assert!(result.incomplete);
    assert_eq!(result.invocations.len(), 2);
    let result = Evaluator::new(&p, 10000, 16)
        .dispatch("root.hit", vec![Value::Integer(4)], &BTreeMap::new())
        .unwrap();
    assert!(result.incomplete);
    assert_eq!(result.invocations.len(), 1);
}
#[test]
fn malformed_snapshot_and_arguments_do_not_become_nonmatches() {
    let p = program(PROGRAM);
    for value in [Value::Integer(256), Value::Bool(true)] {
        assert!(matches!(
            Evaluator::new(&p, 10000, 16).dispatch(
                "root.hit",
                vec![Value::Integer(4)],
                &snapshot(value, Value::Integer(1))
            ),
            Err(Error::InvalidValue(_))
        ));
    }
    assert!(
        Evaluator::new(&p, 10000, 16)
            .dispatch("missing", vec![], &BTreeMap::new())
            .is_err()
    );
    assert!(
        Evaluator::new(&p, 10000, 16)
            .dispatch("root.hit", vec![Value::Integer(-1)], &BTreeMap::new())
            .is_err()
    );
}
#[test]
fn snapshot_matching_and_all_entries_share_budget() {
    let p = program(PROGRAM);
    let objects = snapshot(Value::Integer(1), Value::Integer(2));
    let mut evaluator = Evaluator::new(&p, 10000, 16);
    evaluator
        .dispatch("root.hit", vec![Value::Integer(4)], &objects)
        .unwrap();
    let used = evaluator.consumed();
    assert_eq!(
        Evaluator::new(&p, used - 1, 16).dispatch("root.hit", vec![Value::Integer(4)], &objects),
        Err(Error::BudgetExceeded)
    );
    assert!(
        Evaluator::new(&p, used, 16)
            .dispatch("root.hit", vec![Value::Integer(4)], &objects)
            .is_ok()
    );
}
#[test]
fn pointer_and_array_values_are_opaque_and_shape_checked() {
    let p = program("fn ptr(p:*u8)->*u8{return p;} fn arr(a:[u8;2])->[u8;2]{return a;}");
    let mut e = Evaluator::new(&p, 1000, 16);
    assert_eq!(
        e.call("program::ptr", vec![Value::Pointer(123)]).unwrap(),
        Some(Value::Pointer(123))
    );
    let array = Value::Array(vec![Value::Integer(1), Value::Integer(2)]);
    assert_eq!(
        e.call("program::arr", vec![array.clone()]).unwrap(),
        Some(array)
    );
    assert!(e.call("program::arr", vec![Value::Array(vec![])]).is_err());
    assert!(
        e.call(
            "program::arr",
            vec![Value::Array(vec![Value::Integer(256), Value::Integer(0)])]
        )
        .is_err()
    );
    assert!(e.call("program::ptr", vec![Value::Integer(123)]).is_err());
}
