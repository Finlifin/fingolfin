use fingolfin::{
    compiler,
    gate::{self, Facts, Property, Reason, Stage, Stamp},
    pattern::{self, Value},
    sdk::ObjectKey,
    source,
    syntax::{self, ItemKind},
    util::budget::Budget,
};
use std::{collections::BTreeMap, path::Path};
fn program(pattern: &str) -> compiler::CheckedProgram {
    let mut model = source::load(Path::new("models/fixture")).unwrap();
    model.modules.get_mut("").unwrap().ast.items.extend(
        syntax::parse(
            "extra.fg",
            "pub object root {a:i8,b:i8} pub object root.child {id:i8} pub hook root.hit();",
        )
        .unwrap()
        .items,
    );
    compiler::check_source(
        "gate.fg",
        &format!("fn watch on root.hit() where {pattern} {{}}"),
        &model,
    )
    .unwrap()
}
fn stamp() -> Stamp {
    Stamp {
        configuration: 1,
        object: ObjectKey {
            run: 1,
            launch: 2,
            slot: 3,
            generation: 4,
        },
    }
}
fn property(fields: &[&str]) -> Property {
    Property {
        object: "root".into(),
        fields: fields.iter().map(|s| s.to_string()).collect(),
    }
}
fn snapshot(a: i128, b: i128) -> BTreeMap<String, Value> {
    BTreeMap::from([(
        "root".into(),
        Value::Record(BTreeMap::from([
            ("a".into(), Value::Integer(a)),
            ("b".into(), Value::Integer(b)),
            (
                "child".into(),
                Value::Record(BTreeMap::from([("id".into(), Value::Integer(a))])),
            ),
        ])),
    )])
}
#[test]
fn staged_gates_never_reject_reference_matches() {
    let patterns = [
        "root: {a: 1, b: 2}",
        "root: {a: 1} or root: {b: 2}",
        "not root: {a: 1, b: 2}",
        "not root: {a: 0..=2}",
        "root: {a: _ as n} or root: {b: _ as n}",
        "root: {child: {id: 1}}",
    ];
    let stages = [
        Stage::Attachment,
        Stage::Object,
        Stage::Update,
        Stage::Invocation,
    ];
    for text in patterns {
        let p = program(text);
        let ItemKind::Function(f) = &p.index.symbols["program::watch"].declaration.value.kind
        else {
            panic!()
        };
        for mask in 0..4 {
            let mut availability = BTreeMap::new();
            availability.insert(property(&[]), Stage::Attachment);
            if mask & 1 != 0 {
                availability.insert(property(&["a"]), Stage::Object);
            }
            if mask & 2 != 0 {
                availability.insert(property(&["b"]), Stage::Update);
            }
            let plan = gate::compile(&p, "program::watch", &availability).unwrap();
            for a in -1..=3 {
                for b in -1..=3 {
                    let objects = snapshot(a, b);
                    let reference =
                        pattern::evaluate(f.pattern.as_ref().unwrap(), &objects, 1000).unwrap();
                    let facts = Facts::new(stamp(), objects);
                    for stage in stages {
                        let decision =
                            plan.evaluate(&facts, stamp(), stage, &mut Budget::new(1000));
                        if !reference.matches.is_empty() {
                            assert!(decision.allow, "false negative: {text}, {a},{b}, {stage:?}");
                        }
                        if stage == Stage::Invocation {
                            assert_eq!(
                                decision.allow,
                                !reference.matches.is_empty(),
                                "final stage differs: {text}"
                            );
                        }
                    }
                }
            }
        }
    }
}
#[test]
fn missing_invalid_or_exhausted_facts_fail_open_with_reason() {
    let p = program("not root: {a: 1}");
    let plan = gate::compile(&p, "program::watch", &BTreeMap::new()).unwrap();
    let missing = Facts::new(stamp(), BTreeMap::new());
    assert_eq!(
        plan.evaluate(&missing, stamp(), Stage::Invocation, &mut Budget::new(100))
            .reason,
        Reason::Unknown
    );
    let facts = Facts::new(stamp(), snapshot(1, 0));
    let decision = plan.evaluate(&facts, stamp(), Stage::Invocation, &mut Budget::new(0));
    assert!(decision.allow);
    assert_eq!(decision.reason, Reason::BudgetExceeded);
    let invalid = Facts::new(
        stamp(),
        BTreeMap::from([("root".into(), Value::Bool(false))]),
    );
    assert_eq!(
        plan.evaluate(&invalid, stamp(), Stage::Invocation, &mut Budget::new(100))
            .reason,
        Reason::InvalidFacts
    );
}
#[test]
fn invalidation_and_identity_reuse_cannot_retain_false_gate() {
    let p = program("root: {a: 1}");
    let plan = gate::compile(&p, "program::watch", &BTreeMap::new()).unwrap();
    let mut facts = Facts::new(stamp(), snapshot(0, 0));
    assert!(
        !plan
            .evaluate(&facts, stamp(), Stage::Invocation, &mut Budget::new(100))
            .allow
    );
    let mut variants = vec![];
    let mut s = stamp();
    s.configuration += 1;
    variants.push(s);
    let mut s = stamp();
    s.object.run += 1;
    variants.push(s);
    let mut s = stamp();
    s.object.launch += 1;
    variants.push(s);
    let mut s = stamp();
    s.object.slot += 1;
    variants.push(s);
    let mut s = stamp();
    s.object.generation += 1;
    variants.push(s);
    for current in variants {
        let d = plan.evaluate(&facts, current, Stage::Invocation, &mut Budget::new(100));
        assert!(d.allow);
        assert_eq!(d.reason, Reason::StaleIdentity);
    }
    facts.invalidate(property(&["a"]));
    let d = plan.evaluate(&facts, stamp(), Stage::Invocation, &mut Budget::new(100));
    assert!(d.allow);
    assert_eq!(d.reason, Reason::Unknown);
}
#[test]
fn default_stage_is_invocation_and_plan_is_serializable() {
    let p = program("root: {a: 1}");
    let plan = gate::compile(&p, "program::watch", &BTreeMap::new()).unwrap();
    assert!(plan.reads.iter().all(|r| r.stage == Stage::Invocation));
    let bytes = serde_json::to_vec(&plan).unwrap();
    let loaded: gate::Plan = serde_json::from_slice(&bytes).unwrap();
    let facts = Facts::new(stamp(), snapshot(0, 0));
    assert!(
        loaded
            .evaluate(&facts, stamp(), Stage::Object, &mut Budget::new(100))
            .allow
    );
    assert!(
        !loaded
            .evaluate(&facts, stamp(), Stage::Invocation, &mut Budget::new(100))
            .allow
    );
    assert!(
        fingolfin::ir::lower(&p).unwrap().functions["program::watch"]
            .gate
            .reads
            .len()
            >= 2
    );
}

#[test]
fn shared_route_closes_only_when_every_subscriber_is_ineligible() {
    let p = program("root: {a: 1}");
    let plan = gate::compile(&p, "program::watch", &BTreeMap::new()).unwrap();
    let no = Facts::new(stamp(), snapshot(0, 0));
    let yes = Facts::new(stamp(), snapshot(1, 0));
    assert!(!gate::route(&[], Stage::Invocation, &mut Budget::new(1000)).allow);
    assert!(
        !gate::route(
            &[(&plan, &no, stamp()), (&plan, &no, stamp())],
            Stage::Invocation,
            &mut Budget::new(1000)
        )
        .allow
    );
    let mixed = gate::route(
        &[(&plan, &no, stamp()), (&plan, &yes, stamp())],
        Stage::Invocation,
        &mut Budget::new(1000),
    );
    assert!(mixed.allow);
    assert!(!mixed.subscribers[0].allow);
    assert!(mixed.subscribers[1].allow);
    assert!(
        gate::route(
            &[(&plan, &no, stamp())],
            Stage::Invocation,
            &mut Budget::new(0)
        )
        .allow
    );
}
