use fingolfin::{
    compiler,
    eval::{Error, Evaluator},
    intrinsic::{Host, HostError, Record},
    pattern::Value,
    source,
    util::containers::Error as ContainerError,
};
use std::path::Path;
fn check(src: &str) -> compiler::CheckedProgram {
    compiler::check_source(
        "intrinsic.fg",
        src,
        &source::load(Path::new("models/fixture")).unwrap(),
    )
    .unwrap()
}
const SOURCE: &str = "fn record(slot:u32,n:u64){builtin.counter_add(slot,n);builtin.emit(7,n);}";
#[test]
fn checked_intrinsics_update_bounded_host_state_and_emit_records() {
    let p = check(SOURCE);
    let mut host = Host::new(1, 2).unwrap();
    host.declare_counter(0, 0).unwrap();
    let mut eval = Evaluator::new(&p, 10000, 64);
    eval.set_host(host);
    for n in [3, 5] {
        assert_eq!(
            eval.call(
                "program::record",
                vec![Value::Integer(0), Value::Integer(n)]
            )
            .unwrap(),
            None
        );
    }
    let host = eval.host_mut().unwrap();
    assert_eq!(host.counter(0), Some(8));
    assert_eq!(host.pop_output(), Some(Record { tag: 7, value: 3 }));
    assert_eq!(host.pop_output(), Some(Record { tag: 7, value: 5 }));
    assert_eq!(host.pop_output(), None);
}
#[test]
fn helper_errors_are_visible_and_stop_later_effects() {
    let p = check(SOURCE);
    let mut host = Host::new(1, 1).unwrap();
    host.declare_counter(0, u64::MAX).unwrap();
    let mut eval = Evaluator::new(&p, 10000, 64);
    eval.set_host(host);
    assert_eq!(
        eval.call(
            "program::record",
            vec![Value::Integer(0), Value::Integer(1)]
        ),
        Err(Error::Host(HostError::Container(ContainerError::Overflow)))
    );
    assert_eq!(eval.host_mut().unwrap().counter(0), Some(u64::MAX));
    assert_eq!(eval.host_mut().unwrap().pop_output(), None);
    assert_eq!(
        eval.call(
            "program::record",
            vec![Value::Integer(1), Value::Integer(1)]
        ),
        Err(Error::Host(HostError::MissingCounter(1)))
    );
    let mut host = Host::new(1, 0).unwrap();
    host.declare_counter(0, 0).unwrap();
    eval.set_host(host);
    assert!(matches!(
        eval.call(
            "program::record",
            vec![Value::Integer(0), Value::Integer(1)]
        ),
        Err(Error::Host(HostError::Container(
            ContainerError::Capacity { limit: 0 }
        )))
    ));
    // Each helper is transactional; already-completed earlier effects remain.
    assert_eq!(eval.host_mut().unwrap().counter(0), Some(1));
}
#[test]
fn absent_host_wrong_types_and_unlisted_intrinsics_are_rejected() {
    let p = check(SOURCE);
    assert!(matches!(
        Evaluator::new(&p, 1000, 64).call(
            "program::record",
            vec![Value::Integer(0), Value::Integer(1)]
        ),
        Err(Error::Unsupported(_))
    ));
    let model = source::load(Path::new("models/fixture")).unwrap();
    for source in [
        "fn f(){builtin.counter_add(0);}",
        "fn f(){builtin.counter_add(-1,1);}",
        "fn f(){builtin.emit(true,1);}",
        "fn f(){builtin.write_memory(0,1);}",
        "fn f(){let n=builtin.counter_add(0,1);}",
    ] {
        assert!(compiler::check_source("bad.fg", source, &model).is_err());
    }
}
#[test]
fn package_helper_allowlist_is_derived_from_elf_and_checked_on_read() {
    let p = check(SOURCE);
    let bytes =
        fingolfin::build::compile(&p, &["program::record".into()], Path::new("clang")).unwrap();
    let mut manifest = fingolfin::package::decode(&bytes).unwrap();
    assert_eq!(manifest.entries["program::record"].helpers, vec![1, 2]);
    manifest
        .entries
        .get_mut("program::record")
        .unwrap()
        .helpers
        .clear();
    assert!(fingolfin::package::encode(manifest).is_err());
}

fn aggregate_host() -> Host {
    let mut h = Host::with_capacities(0, 1, 1, 0).unwrap();
    h.declare_map(0, 2).unwrap();
    h.declare_histogram(0, vec![0, 10], 3).unwrap();
    h
}
#[test]
fn language_map_histogram_boundaries_capacity_and_overflow() {
    let p = check(include_str!(
        "../integration/aggregate-helpers/aggregate.fg"
    ));
    let mut e = Evaluator::new(&p, 10000, 64);
    e.set_host(aggregate_host());
    for (key, sample, weight) in [(7, i64::MIN as i128, 2), (9, 0, 3), (7, 10, 4)] {
        e.call(
            "program::aggregate",
            vec![
                Value::Integer(key),
                Value::Integer(sample),
                Value::Integer(weight),
            ],
        )
        .unwrap();
    }
    let h = e.host_mut().unwrap();
    assert_eq!(h.map(0).unwrap().get(&7).unwrap().value(), 6);
    assert_eq!(h.histogram(0).unwrap().counts(), &[2, 3, 4]);
    assert!(matches!(
        e.call(
            "program::aggregate",
            vec![Value::Integer(11), Value::Integer(0), Value::Integer(1)]
        ),
        Err(Error::Host(HostError::Container(
            ContainerError::Capacity { limit: 2 }
        )))
    ));
    assert!(matches!(
        e.call(
            "program::aggregate",
            vec![
                Value::Integer(7),
                Value::Integer(0),
                Value::Integer(u64::MAX as i128)
            ]
        ),
        Err(Error::Host(HostError::Container(ContainerError::Overflow)))
    ));
    assert_eq!(e.host_mut().unwrap().histogram(0).unwrap().total(), 9);
}
#[test]
fn aggregate_configuration_and_abi_validation_reject_invalid_inputs() {
    use fingolfin::intrinsic::{HostConfig, Intrinsic};
    let config: HostConfig =
        serde_json::from_str(include_str!("../integration/aggregate-helpers/config.json")).unwrap();
    let mut h = config.prepare().unwrap();
    assert_eq!(
        h.invoke_words(Intrinsic::MapAdd, [u64::MAX, 1, 1, 0, 0]),
        Err(HostError::InvalidArguments)
    );
    assert_eq!(
        h.invoke_words(Intrinsic::MapAdd, [0, 1, 1, 1, 0]),
        Err(HostError::InvalidArguments)
    );
    assert_eq!(
        h.invoke_values(
            Intrinsic::HistogramAdd,
            &[Value::Integer(0), Value::Integer(0), Value::Integer(-1)]
        ),
        Err(HostError::InvalidArguments)
    );
    assert!(serde_json::from_str::<HostConfig>(r#"{"unknown":1}"#).is_err());
    let duplicates: HostConfig =
        serde_json::from_str(r#"{"maps":[{"slot":0,"capacity":1},{"slot":0,"capacity":1}]}"#)
            .unwrap();
    assert!(duplicates.prepare().is_err());
    let p = check(include_str!(
        "../integration/aggregate-helpers/aggregate.fg"
    ));
    let bytes =
        fingolfin::build::compile(&p, &["program::aggregate".into()], Path::new("clang")).unwrap();
    assert_eq!(
        fingolfin::package::decode(&bytes).unwrap().entries["program::aggregate"].helpers,
        vec![3, 4]
    );
}
