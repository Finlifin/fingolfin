use fingolfin::source::load;
use std::{
    fs,
    path::{Path, PathBuf},
    sync::atomic::{AtomicUsize, Ordering},
};
static NEXT: AtomicUsize = AtomicUsize::new(0);
struct Fixture(PathBuf);
impl Fixture {
    fn new() -> Self {
        let path = std::env::temp_dir().join(format!(
            "fg-source-{}-{}",
            std::process::id(),
            NEXT.fetch_add(1, Ordering::Relaxed)
        ));
        fs::create_dir_all(path.join("src")).unwrap();
        let f = Self(path);
        f.write(
            "Fingolfin.toml",
            "[package]\nname = 'fixture'\nversion = '0.1.0'\n",
        );
        f.write("src/lib.fg", "pub use types.Handle;");
        f
    }
    fn write(&self, path: &str, value: &str) {
        let path = self.0.join(path);
        fs::create_dir_all(path.parent().unwrap()).unwrap();
        fs::write(path, value).unwrap();
    }
    fn path(&self) -> &Path {
        &self.0
    }
}
impl Drop for Fixture {
    fn drop(&mut self) {
        fs::remove_dir_all(&self.0).unwrap();
    }
}
#[test]
fn deterministic_content_identity_and_module_names() {
    let a = Fixture::new();
    let b = Fixture::new();
    for f in [&a, &b] {
        f.write("src/types/mod.fg", "pub use handles.Handle;");
        f.write("src/types/handles.fg", "pub struct Handle { id: u64 }");
        f.write("src/hooks.fg", "pub hook root.hit(id: u64);");
    }
    let first = load(a.path()).unwrap();
    let second = load(b.path()).unwrap();
    assert_eq!(first.source_hash, second.source_hash);
    assert_eq!(
        first.modules.keys().map(String::as_str).collect::<Vec<_>>(),
        vec!["", "hooks", "types", "types.handles"]
    );
    a.write("src/hooks.fg", "pub hook root.hit(id: u32);");
    let changed = load(a.path()).unwrap();
    assert_ne!(first.source_hash, changed.source_hash);
    assert_eq!(
        first.modules["types.handles"].source_hash,
        changed.modules["types.handles"].source_hash
    );
}
#[test]
fn reports_conflicts_and_multiple_syntax_errors() {
    let f = Fixture::new();
    f.write("src/types.fg", "struct A {}");
    f.write("src/types/mod.fg", "struct B {}");
    f.write("src/broken.fg", "fn missing(");
    f.write("src/another.fg", "fn also(");
    let errors = load(f.path()).unwrap_err();
    assert_eq!(errors.len(), 3, "{errors:?}");
    assert!(errors.iter().any(|e| e.message.contains("same module")));
}
#[test]
fn rejects_source_escape_unknown_manifest_and_missing_root() {
    let f = Fixture::new();
    for extra in [
        "source = '../outside'",
        "source = '/tmp'",
        "surprise = true",
    ] {
        f.write(
            "Fingolfin.toml",
            &format!("[package]\nname = 'fixture'\nversion = '0.1.0'\n{extra}\n"),
        );
        assert!(load(f.path()).is_err());
    }
    f.write(
        "Fingolfin.toml",
        "[package]\nname = 'fixture'\nversion = '0.1.0'\n",
    );
    fs::remove_file(f.0.join("src/lib.fg")).unwrap();
    assert!(
        load(f.path())
            .unwrap_err()
            .iter()
            .any(|e| e.message.contains("requires lib.fg"))
    );
}
#[cfg(unix)]
#[test]
fn rejects_symlink_sources() {
    let f = Fixture::new();
    std::os::unix::fs::symlink("/tmp", f.0.join("src/external")).unwrap();
    assert!(
        load(f.path())
            .unwrap_err()
            .iter()
            .any(|e| e.message.contains("symlinks"))
    );
}

#[test]
fn model_availability_is_validated_lowered_and_hashed() {
    use fingolfin::{compiler, gate::Stage, ir};
    let f = Fixture::new();
    f.write("src/lib.fg","pub object fixture { id:u32 }\npub object fixture.device { id:u64 }\npub hook fixture.device.tick(v:u32);");
    let base = "[package]\nname='fixture'\nversion='0.1.0'\n";
    let text = "fn watch on fixture.device.tick(v:u32) where fixture:{device:{id:0}} {}";
    let plain = load(f.path()).unwrap();
    let checked = compiler::check_source("watch.fg", text, &plain).unwrap();
    assert!(
        ir::lower(&checked).unwrap().functions["program::watch"]
            .gate
            .reads
            .iter()
            .all(|r| r.stage == Stage::Invocation)
    );
    let contracts = [
        "[[availability]]\nobject='fixture'\nstage='Object'\n",
        "[[availability]]\nobject='fixture'\nfields=['device']\nstage='Object'\n",
        "[[availability]]\nobject='fixture'\nfields=['device','id']\nstage='Object'\n",
    ];
    f.write("Fingolfin.toml", &format!("{base}{}", contracts.concat()));
    let model = load(f.path()).unwrap();
    assert_ne!(model.source_hash, plain.source_hash);
    let checked = compiler::check_source("watch.fg", text, &model).unwrap();
    let lowered = ir::lower(&checked).unwrap();
    assert_eq!(lowered.functions["program::watch"].gate.reads.len(), 3);
    assert!(
        lowered.functions["program::watch"]
            .gate
            .reads
            .iter()
            .all(|r| r.stage == Stage::Object)
    );
    assert!(serde_json::to_string(&checked).is_ok());
    f.write(
        "Fingolfin.toml",
        &format!(
            "{base}{}",
            contracts.iter().rev().copied().collect::<String>()
        ),
    );
    assert_eq!(load(f.path()).unwrap().source_hash, model.source_hash);
    // Exact path only: a stable record does not promise stable descendants.
    f.write("Fingolfin.toml", &format!("{base}{}", contracts[0]));
    let partial = compiler::check_source("watch.fg", text, &load(f.path()).unwrap()).unwrap();
    let lowered = ir::lower(&partial).unwrap();
    let reads = &lowered.functions["program::watch"].gate.reads;
    assert_eq!(reads[0].stage, Stage::Object);
    assert!(reads[1..].iter().all(|r| r.stage == Stage::Invocation));
    for invalid in [
        format!("{}{}", contracts[0], contracts[0]),
        "[[availability]]\nobject='missing'\nstage='Object'\n".into(),
        "[[availability]]\nobject='fixture'\nfields=['device','missing']\nstage='Object'\n".into(),
        "[[availability]]\nobject='fixture'\nfields=['id','x']\nstage='Object'\n".into(),
    ] {
        f.write("Fingolfin.toml", &format!("{base}{invalid}"));
        let errors =
            compiler::check_source("watch.fg", text, &load(f.path()).unwrap()).unwrap_err();
        assert!(
            errors.iter().any(|e| e.message.contains("availability")),
            "{errors:?}"
        );
    }
    f.write(
        "Fingolfin.toml",
        &format!("{base}[[availability]]\nobject='fixture'\nstage='Guess'\n"),
    );
    assert!(load(f.path()).is_err());
}
