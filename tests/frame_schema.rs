use fingolfin::{compiler, frame::Layout, frame_schema, source};
use std::{io::Write, path::Path, process::Command};
fn schema(text: &str, function: &str) -> Vec<u8> {
    let p = compiler::check_source(
        "schema.fg",
        text,
        &source::load(Path::new("models/fixture")).unwrap(),
    )
    .unwrap();
    Layout::for_function(&p, function)
        .unwrap()
        .schema_bytes()
        .unwrap()
}
#[test]
fn portable_schema_matches_independent_c_validator() {
    let samples = vec![
        schema("fn f(){}", "program::f"),
        schema(
            "fn f(a:u8,b:i64,c:bool,p:*u8)->i64{return b;}",
            "program::f",
        ),
        schema(
            "enum E:i8 {low=-128, high=127} struct Inner {z:u8,a:i64} struct S {z:bool,inner:Inner,e:E,values:[u32;3]} fn f(s:S)->S{return s;}",
            "program::f",
        ),
        schema(
            include_str!("fixtures/valid_observation.fg"),
            "program::push",
        ),
    ];
    let dir = std::env::temp_dir().join(format!("fg-schema-c-{}", std::process::id()));
    std::fs::create_dir_all(&dir).unwrap();
    let executable = dir.join("schema-test");
    let compiled = Command::new("clang")
        .args([
            "-std=c11",
            "-Wall",
            "-Wextra",
            "-Wpedantic",
            "-Werror",
            "-I",
            "runtime/include",
            "runtime/src/bytes.c",
            "runtime/src/schema.c",
            "runtime/tests/schema.c",
            "-o",
        ])
        .arg(&executable)
        .output()
        .unwrap();
    assert!(
        compiled.status.success(),
        "{}",
        String::from_utf8_lossy(&compiled.stderr)
    );
    let path = dir.join("corpus.bin");
    let mut corpus = std::fs::File::create(&path).unwrap();
    let mut emit = |bytes: &[u8]| {
        let expected = frame_schema::validate(bytes).is_ok();
        corpus.write_all(&[u8::from(expected)]).unwrap();
        corpus
            .write_all(&(bytes.len() as u32).to_le_bytes())
            .unwrap();
        corpus.write_all(bytes).unwrap();
    };
    let mut seed = 13u64;
    for sample in &samples {
        assert!(frame_schema::validate(sample).is_ok());
        emit(sample);
        for end in 0..sample.len() {
            assert!(frame_schema::validate(&sample[..end]).is_err());
            emit(&sample[..end]);
        }
        for _ in 0..5000 {
            seed = seed.wrapping_mul(6364136223846793005).wrapping_add(1);
            let mut altered = sample.clone();
            let at = (seed >> 32) as usize % altered.len();
            altered[at] ^= ((seed >> 16) | 1) as u8;
            emit(&altered);
        }
    }
    drop(corpus);
    let result = Command::new(&executable).arg(&path).output().unwrap();
    assert!(
        result.status.success(),
        "{}\n{}",
        String::from_utf8_lossy(&result.stdout),
        String::from_utf8_lossy(&result.stderr)
    );
    // Keep corpus in /tmp for instrumented C replay; path is explicit in test output.
    println!(
        "{}Corpus: {}",
        String::from_utf8_lossy(&result.stdout),
        path.display()
    );
}
#[test]
fn package_schema_corruption_and_old_version_are_rejected() {
    use fingolfin::{build, package, util::hash::sha256};
    let p = compiler::check_source(
        "schema.fg",
        "fn f(n:u64)->u64{return n;}",
        &source::load(Path::new("models/fixture")).unwrap(),
    )
    .unwrap();
    let bytes = build::compile(&p, &["program::f".into()], Path::new("clang")).unwrap();
    let m = package::decode(&bytes).unwrap();
    assert_eq!(m.version, 3);
    let mut bad = m.clone();
    bad.version = 2;
    assert!(package::encode(bad).is_err());
    let mut bad = m;
    let e = bad.entries.get_mut("program::f").unwrap();
    e.frame_schema[16] = 0;
    e.frame_schema_hash = sha256(&e.frame_schema);
    assert!(package::encode(bad).is_err());
}
