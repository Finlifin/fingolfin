use fingolfin::{
    pattern::{self, Error, Value},
    syntax::{self, ItemKind, Node, Pattern},
};
use std::collections::BTreeMap;
fn pattern(text: &str) -> Node<Pattern> {
    let source = format!("fn watch on root.hit() where {text} {{}}");
    let file = syntax::parse("pattern.fg", &source).unwrap();
    let ItemKind::Function(function) = &file.items[0].value.kind else {
        panic!()
    };
    function.pattern.clone().unwrap()
}
fn objects(fields: &[(&str, Value)]) -> BTreeMap<String, Value> {
    BTreeMap::from([(
        "root".into(),
        Value::Record(
            fields
                .iter()
                .map(|(k, v)| (k.to_string(), v.clone()))
                .collect(),
        ),
    )])
}
#[test]
fn range_bounds_bind_exact_snapshot_values() {
    for (range, values) in [("1..3", vec![1, 2]), ("1..=3", vec![1, 2, 3])] {
        let p = pattern(&format!("root: {{ id: {range} as x }}"));
        for n in 0..5 {
            let result =
                pattern::evaluate(&p, &objects(&[("id", Value::Integer(n))]), 100).unwrap();
            assert_eq!(result.matches.len(), usize::from(values.contains(&n)));
            if let Some(row) = result.matches.first() {
                assert_eq!(row["x"], Value::Integer(n));
            }
            assert!(!result.incomplete);
        }
    }
}
#[test]
fn unknown_is_not_false_and_negation_does_not_export() {
    let p = pattern("not root: { id: 1 as n }");
    assert!(
        pattern::evaluate(&p, &objects(&[]), 100)
            .unwrap()
            .incomplete
    );
    let result = pattern::evaluate(&p, &objects(&[("id", Value::Integer(2))]), 100).unwrap();
    assert_eq!(result.matches, vec![BTreeMap::new()]);
    assert!(!result.incomplete);
    assert!(
        pattern::evaluate(&p, &objects(&[("id", Value::Integer(1))]), 100)
            .unwrap()
            .matches
            .is_empty()
    );
}
#[test]
fn known_false_dominates_unknown_in_conjunction() {
    for text in ["root: { missing: 0, id: 1 }", "root: { id: 1, missing: 0 }"] {
        let result =
            pattern::evaluate(&pattern(text), &objects(&[("id", Value::Integer(2))]), 100).unwrap();
        assert!(result.matches.is_empty());
        assert!(!result.incomplete);
    }
}
#[test]
fn or_unions_distinct_rows_and_deduplicates_identical_bindings() {
    let p = pattern("root: { a: _ as x } or root: { b: _ as x }");
    for (a, b, rows) in [(1, 1, 1), (1, 2, 2)] {
        let result = pattern::evaluate(
            &p,
            &objects(&[("a", Value::Integer(a)), ("b", Value::Integer(b))]),
            100,
        )
        .unwrap();
        assert_eq!(result.matches.len(), rows);
        assert!(!result.incomplete);
    }
    let result = pattern::evaluate(&p, &objects(&[("a", Value::Integer(1))]), 100).unwrap();
    assert_eq!(result.matches.len(), 1);
    assert!(result.incomplete);
}
#[test]
fn nested_bindings_are_local_and_budget_and_type_errors_are_visible() {
    let p = pattern("root: { child: { id: 7 as n } }");
    let context = objects(&[(
        "child",
        Value::Record(BTreeMap::from([("id".into(), Value::Integer(7))])),
    )]);
    assert_eq!(
        pattern::evaluate(&p, &context, 100).unwrap().matches[0]["n"],
        Value::Integer(7)
    );
    assert_eq!(
        pattern::evaluate(&p, &context, 0),
        Err(Error::BudgetExceeded)
    );
    assert!(matches!(
        pattern::evaluate(&p, &objects(&[("child", Value::Bool(true))]), 100),
        Err(Error::InvalidSnapshot(_))
    ));
    let p = pattern("root: { a: _ as x, b: _ as x }");
    assert!(matches!(
        pattern::evaluate(
            &p,
            &objects(&[("a", Value::Integer(1)), ("b", Value::Integer(1))]),
            100
        ),
        Err(Error::ConflictingBinding(_))
    ));
}
