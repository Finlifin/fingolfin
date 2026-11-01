use fingolfin::{compiler, frame::Layout, pattern::Value, source};
use std::{collections::BTreeMap, path::Path};
fn program(text: &str) -> compiler::CheckedProgram {
    compiler::check_source(
        "frame.fg",
        text,
        &source::load(Path::new("models/fixture")).unwrap(),
    )
    .unwrap()
}
#[test]
fn padding_ranges_and_typed_values_are_checked() {
    let p = program("fn test(a:u8,b:i16,c:bool,p:*u8)->i16{return b;}");
    let layout = Layout::for_function(&p, "program::test").unwrap();
    assert_eq!(layout.size(), 40);
    assert_eq!(layout.alignment(), 8);
    let mut f = layout.clone().allocate(40).unwrap();
    assert!(layout.allocate(39).is_err());
    let mut args = vec![
        Value::Integer(255),
        Value::Integer(-32768),
        Value::Bool(true),
        Value::Pointer(u64::MAX),
    ];
    f.prepare(&args, &BTreeMap::new(), 100, 64).unwrap();
    assert_eq!(&f.bytes()[16..24], &[255, 0, 0, 128, 1, 0, 0, 0]);
    assert_eq!(&f.bytes()[24..32], &[255; 8]);
    f.bytes_mut()[32..34].copy_from_slice(&(-123i16).to_le_bytes());
    assert_eq!(f.result().unwrap(), Some(Value::Integer(-123)));
    args[0] = Value::Integer(256);
    assert!(f.prepare(&args, &BTreeMap::new(), 100, 64).is_err());
    args[0] = Value::Unknown;
    assert!(f.prepare(&args, &BTreeMap::new(), 100, 64).is_err());
    assert!(f.prepare(&[], &BTreeMap::new(), 100, 64).is_err());
}
#[test]
fn hook_bindings_are_named_and_exact() {
    let p = program(include_str!("fixtures/valid_observation.fg"));
    let mut f = Layout::for_function(&p, "program::push")
        .unwrap()
        .allocate(4096)
        .unwrap();
    let args = [
        Value::Pointer(0),
        Value::Record(BTreeMap::from([
            ("slot".into(), Value::Integer(1)),
            ("generation".into(), Value::Integer(2)),
        ])),
        Value::Integer(3),
    ];
    assert!(f.prepare(&args, &BTreeMap::new(), 100, 64).is_err());
    f.prepare(
        &args,
        &BTreeMap::from([("cb_id".into(), Value::Integer(16))]),
        100,
        64,
    )
    .unwrap();
    assert!(
        f.prepare(
            &args,
            &BTreeMap::from([("wrong".into(), Value::Integer(16))]),
            100,
            64
        )
        .is_err()
    );
}
