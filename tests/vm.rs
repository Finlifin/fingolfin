#![cfg(feature = "ubpf")]
use fingolfin::{
    build, compiler,
    intrinsic::{Host, HostError},
    package, source,
    util::containers,
    vm::{Error, Vm},
};
use std::path::Path;
fn entry(file: &str, function: &str) -> package::Entry {
    let model = source::load(Path::new("models/fixture")).unwrap();
    let checked =
        compiler::check_source(file, &std::fs::read_to_string(file).unwrap(), &model).unwrap();
    let bytes = build::compile(&checked, &[function.into()], Path::new("clang")).unwrap();
    package::decode(&bytes)
        .unwrap()
        .entries
        .remove(function)
        .unwrap()
}
fn frame(args: &[u64]) -> Vec<u8> {
    let mut frame = vec![0; 16 + args.len() * 8];
    frame[..8].copy_from_slice(&10000u64.to_le_bytes());
    frame[8..12].copy_from_slice(&64u32.to_le_bytes());
    for (i, arg) in args.iter().enumerate() {
        frame[16 + i * 8..24 + i * 8].copy_from_slice(&arg.to_le_bytes());
    }
    frame
}
#[test]
fn actual_rust_host_counter_output_errors_and_reuse() {
    let entry = entry("integration/state-helpers/record.fg", "program::record");
    let mut vm = Vm::load(&entry, 10000, 32).unwrap();
    let mut a = Host::new(1, 2).unwrap();
    a.declare_counter(0, 0).unwrap();
    let mut b = Host::new(1, 2).unwrap();
    b.declare_counter(0, 100).unwrap();
    assert_eq!(vm.execute(&mut frame(&[0, 3]), &mut a), Ok(0));
    assert_eq!(vm.execute(&mut frame(&[0, 9]), &mut b), Ok(0));
    assert_eq!(vm.execute(&mut frame(&[0, 5]), &mut a), Ok(0));
    assert_eq!(a.counter(0), Some(8));
    assert_eq!(b.counter(0), Some(109));
    let mut full = frame(&[0, 1]);
    assert_eq!(
        vm.execute(&mut full, &mut a),
        Err(Error::Host(HostError::Container(
            containers::Error::Capacity { limit: 2 }
        )))
    );
    assert_eq!(u32::from_le_bytes(full[12..16].try_into().unwrap()), 6);
    assert_eq!(a.counter(0), Some(9));
    assert_eq!(a.pop_output().unwrap().value, 3);
    assert_eq!(vm.execute(&mut frame(&[0, 2]), &mut a), Ok(0));
    assert_eq!(a.counter(0), Some(11));
    assert_eq!(
        vm.execute(&mut frame(&[1, 1]), &mut b),
        Err(Error::Host(HostError::MissingCounter(1)))
    );
    assert_eq!(b.counter(0), Some(109));
    assert_eq!(vm.execute(&mut [0; 33], &mut b), Err(Error::FrameCapacity));
    assert_eq!(vm.execute(&mut [0; 16], &mut b), Ok(5));
    let mut limited = Vm::load(&entry, 1, 32).unwrap();
    assert!(matches!(
        limited.execute(&mut frame(&[0, 1]), &mut b),
        Err(Error::Runtime(_))
    ));
    assert_eq!(b.counter(0), Some(109));
    let mut overflow = Host::new(1, 1).unwrap();
    overflow.declare_counter(0, u64::MAX).unwrap();
    assert_eq!(
        vm.execute(&mut frame(&[0, 1]), &mut overflow),
        Err(Error::Host(HostError::Container(
            containers::Error::Overflow
        )))
    );
    assert_eq!(overflow.counter(0), Some(u64::MAX));
    assert!(overflow.pop_output().is_none());
    let mut bad_frame = entry.clone();
    bad_frame.frame_header.push('x');
    assert!(Vm::load(&bad_frame, 100, 32).is_err());
    let mut bad = entry.clone();
    bad.helpers.clear();
    assert!(Vm::load(&bad, 100, 32).is_err());
    bad = entry;
    bad.elf[0] = 0;
    assert!(Vm::load(&bad, 100, 32).is_err());
}
#[test]
fn actual_rust_host_map_histogram_partial_failure() {
    let entry = entry(
        "integration/aggregate-helpers/aggregate.fg",
        "program::aggregate",
    );
    let mut vm = Vm::load(&entry, 10000, 40).unwrap();
    let mut a = Host::with_capacities(0, 1, 1, 0).unwrap();
    a.declare_map(0, 2).unwrap();
    a.declare_histogram(0, vec![0, 10], 3).unwrap();
    for (key, sample, weight) in [(7, i64::MIN, 2), (7, 0, 3), (8, i64::MAX, 4)] {
        assert_eq!(
            vm.execute(&mut frame(&[key, sample as u64, weight]), &mut a),
            Ok(0)
        );
    }
    assert_eq!(a.map(0).unwrap().get(&7).unwrap().value(), 5);
    assert_eq!(a.histogram(0).unwrap().counts(), &[2, 3, 4]);
    assert!(matches!(
        vm.execute(&mut frame(&[9, 1, 1]), &mut a),
        Err(Error::Host(HostError::Container(
            containers::Error::Capacity { .. }
        )))
    ));
    assert_eq!(a.histogram(0).unwrap().total(), 9);
    let mut b = Host::with_capacities(0, 1, 0, 0).unwrap();
    b.declare_map(0, 1).unwrap();
    assert_eq!(
        vm.execute(&mut frame(&[7, 0, 10]), &mut b),
        Err(Error::Host(HostError::MissingHistogram(0)))
    );
    assert_eq!(b.map(0).unwrap().get(&7).unwrap().value(), 10);
    assert_eq!(a.map(0).unwrap().get(&7).unwrap().value(), 5);
}

#[test]
fn histogram_overflow_preserves_prior_map_update() {
    let entry = entry(
        "integration/aggregate-helpers/aggregate.fg",
        "program::aggregate",
    );
    let mut vm = Vm::load(&entry, 10000, 40).unwrap();
    let mut host = Host::with_capacities(0, 1, 1, 0).unwrap();
    host.declare_map(0, 2).unwrap();
    host.declare_histogram(0, vec![0, 10], 3).unwrap();
    assert_eq!(vm.execute(&mut frame(&[1, 0, u64::MAX]), &mut host), Ok(0));
    assert_eq!(
        vm.execute(&mut frame(&[2, 10, 1]), &mut host),
        Err(Error::Host(HostError::Container(
            containers::Error::Overflow
        )))
    );
    assert_eq!(host.map(0).unwrap().get(&2).unwrap().value(), 1);
    assert_eq!(host.histogram(0).unwrap().counts(), &[0, u64::MAX, 0]);
}

#[test]
fn typed_function_roundtrips_composite_values_and_rejects_bad_arguments() {
    use fingolfin::{
        pattern::Value,
        vm::{CallError, Function},
    };
    use std::collections::BTreeMap;
    let text = "enum Mode:i8 {low=-1,high=1} struct S {flag:bool,mode:Mode,values:[i16;2],ptr:*u8} fn copy(s:S)->S{return s;}";
    let model = source::load(Path::new("models/fixture")).unwrap();
    let checked = compiler::check_source("typed.fg", text, &model).unwrap();
    let bytes = build::compile(&checked, &["program::copy".into()], Path::new("clang")).unwrap();
    let entry = package::decode(&bytes)
        .unwrap()
        .entries
        .remove("program::copy")
        .unwrap();
    let mut function = Function::load(&checked, &entry, 10000, 4096).unwrap();
    let mut host = Host::new(0, 0).unwrap();
    let value = Value::Record(BTreeMap::from([
        ("flag".into(), Value::Bool(true)),
        ("mode".into(), Value::Variant("low".into())),
        (
            "values".into(),
            Value::Array(vec![
                Value::Integer(i16::MIN as i128),
                Value::Integer(i16::MAX as i128),
            ]),
        ),
        ("ptr".into(), Value::Pointer(u64::MAX)),
    ]));
    let returned = function
        .call(
            std::slice::from_ref(&value),
            &BTreeMap::new(),
            10000,
            64,
            &mut host,
        )
        .unwrap();
    assert_eq!(returned.value, Some(value.clone()));
    assert!(returned.remaining < 10000);
    assert_eq!(
        function.call(
            std::slice::from_ref(&value),
            &BTreeMap::new(),
            0,
            64,
            &mut host
        ),
        Err(CallError::Entry(1))
    );
    let mut bad = value.clone();
    if let Value::Record(fields) = &mut bad {
        fields.insert("mode".into(), Value::Variant("missing".into()));
    }
    assert!(matches!(
        function.call(&[bad], &BTreeMap::new(), 10000, 64, &mut host),
        Err(CallError::Arguments(_))
    ));
    assert_eq!(
        function
            .call(
                std::slice::from_ref(&value),
                &BTreeMap::new(),
                10000,
                64,
                &mut host
            )
            .unwrap()
            .value,
        Some(value)
    );
    let different =
        compiler::check_source("typed.fg", "fn copy(s:u8)->u8{return s;}", &model).unwrap();
    assert!(Function::load(&different, &entry, 10000, 4096).is_err());
}
