use fingolfin::{build, compiler, package, source, util::elf};
use std::{path::Path, process::Command};
fn bytes() -> Vec<u8> {
    let p = compiler::check_source(
        "package.fg",
        include_str!("fixtures/valid_observation.fg"),
        &source::load(Path::new("models/fixture")).unwrap(),
    )
    .unwrap();
    build::compile(&p, &[], Path::new("clang")).unwrap()
}
#[test]
fn multi_entry_package_roundtrips_and_rebuilds_identically() {
    let first = bytes();
    let second = bytes();
    assert_eq!(first, second);
    let manifest = package::decode(&first).unwrap();
    assert_eq!(manifest.entries.len(), 2);
    assert_eq!(
        manifest.entries["program::push"].dependencies,
        vec!["program::increment"]
    );
    assert_eq!(package::encode(manifest).unwrap(), first);
}
#[test]
fn package_integrity_and_entry_consistency_are_checked() {
    let bytes = bytes();
    let mut value: serde_json::Value = serde_json::from_slice(&bytes).unwrap();
    value["manifest"]["model_source_hash"] = serde_json::json!("0".repeat(64));
    assert!(package::decode(&serde_json::to_vec(&value).unwrap()).is_err());
    let base = package::decode(&bytes).unwrap();
    let mut damaged = base.clone();
    damaged.entries.get_mut("program::push").unwrap().elf[0] = 0;
    assert!(package::encode(damaged).is_err());
    let mut damaged = base.clone();
    damaged.entries.get_mut("program::push").unwrap().signature = serde_json::Value::Null;
    assert!(package::encode(damaged).is_err());
    let mut damaged = base.clone();
    damaged.version = 99;
    assert!(package::encode(damaged).is_err());
    let mut damaged = base;
    damaged
        .entries
        .get_mut("program::push")
        .unwrap()
        .frame_header
        .push('x');
    assert!(package::encode(damaged).is_err());
    assert!(package::decode(&vec![0; package::MAX_BYTES + 1]).is_err());
}
#[test]
fn elf_validation_rejects_truncation_wrong_machine_and_bad_section_ranges() {
    let manifest = package::decode(&bytes()).unwrap();
    let bytes = &manifest.entries["program::push"].elf;
    for end in 0..bytes.len() {
        assert!(
            elf::validate_bpf(&bytes[..end]).is_err(),
            "accepted truncation at {end}"
        );
    }
    let mut wrong = bytes.clone();
    wrong[18] = 62;
    assert!(elf::validate_bpf(&wrong).is_err());
    let mut wrong = bytes.clone();
    wrong[40..48].copy_from_slice(&u64::MAX.to_le_bytes());
    assert!(elf::validate_bpf(&wrong).is_err());
    for n in 0..256 {
        let mut header = vec![n as u8; 64];
        header[..7].copy_from_slice(b"\x7fELF\x02\x01\x01");
        assert!(elf::validate_bpf(&header).is_err());
    }
}
#[test]
fn build_cli_failure_preserves_existing_artifact_and_explain_selects_entry() {
    let path = std::env::temp_dir().join(format!("fg-package-cli-{}.fgo", std::process::id()));
    let original = bytes();
    std::fs::write(&path, &original).unwrap();
    let failed = Command::new(env!("CARGO_BIN_EXE_fingolfin"))
        .args([
            "build",
            "tests/fixtures/valid_observation.fg",
            "--model",
            "models/fixture",
            "--clang",
            "/bin/false",
            "--output",
        ])
        .arg(&path)
        .output()
        .unwrap();
    assert!(!failed.status.success());
    assert_eq!(std::fs::read(&path).unwrap(), original);
    let output = Command::new(env!("CARGO_BIN_EXE_fingolfin"))
        .arg("explain")
        .arg(&path)
        .args(["--entry", "program::push"])
        .output()
        .unwrap();
    assert!(
        output.status.success(),
        "{}",
        String::from_utf8_lossy(&output.stderr)
    );
    let value: serde_json::Value = serde_json::from_slice(&output.stdout).unwrap();
    assert_eq!(value["entries"].as_array().unwrap().len(), 1);
    std::fs::remove_file(path).unwrap();
}
