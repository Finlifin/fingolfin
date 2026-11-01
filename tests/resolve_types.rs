use fingolfin::{
    resolve,
    source::{LoadedPackage, Manifest, Module, Package},
    syntax, types,
};
use std::collections::{BTreeMap, BTreeSet};
fn package(files: &[(&str, &str)]) -> LoadedPackage {
    LoadedPackage {
        manifest: Manifest {
            availability: vec![],
            package: Package {
                name: "test".into(),
                version: "0.1.0".into(),
                source: "src".into(),
            },
        },
        modules: files
            .iter()
            .map(|(module, source)| {
                let file = format!("{module}.fg");
                (
                    module.to_string(),
                    Module {
                        name: module.to_string(),
                        ast: syntax::parse(&file, source).unwrap(),
                        file,
                        source_hash: String::new(),
                    },
                )
            })
            .collect(),
        source_hash: String::new(),
    }
}
#[test]
fn reexports_share_identity_and_imports_invalidate_transitively() {
    let p = package(&[
        ("", "pub use api.View;"),
        ("api", "pub use storage.View;"),
        ("storage", "pub struct View { id: u32 }"),
        ("client", "use test.View; fn read(v: View) {}"),
        ("unrelated", "struct Other { id: u8 }"),
    ]);
    let index = resolve::collect(&p).unwrap();
    assert_eq!(
        index.resolve("client", "View").unwrap().id,
        "test::storage.View"
    );
    let affected = index.affected_modules(&BTreeSet::from(["storage".into()]));
    assert!(affected.contains("api") && affected.contains("client") && affected.contains(""));
    assert!(!affected.contains("unrelated"));
    let checked = types::check(&index).unwrap();
    assert_eq!(checked.layouts["test::storage.View"].size, 4);
}
#[test]
fn import_errors_are_batched_and_cycles_terminate() {
    let p = package(&[
        ("", "use a.Secret; use missing.Nope;"),
        ("a", "struct Secret { x: u8 } pub use b.Cycle;"),
        ("b", "pub use a.Cycle;"),
    ]);
    let errors = resolve::collect(&p).unwrap_err();
    assert_eq!(errors.len(), 4);
    assert!(errors.iter().any(|e| e.message.contains("private")));
    assert!(errors.iter().any(|e| e.message.contains("Cycle")));
}
#[test]
fn collisions_include_aliases_and_logical_hook_owners() {
    for files in [
        vec![("", "struct A { x: u8 } struct A { x: u64 }")],
        vec![
            ("", "use a.A; use b.A;"),
            ("a", "pub struct A { x: u8 }"),
            ("b", "pub struct A { x: u8 }"),
        ],
        vec![("a", "pub hook root.hit();"), ("b", "pub hook root.hit();")],
    ] {
        assert!(resolve::collect(&package(&files)).is_err());
    }
}
#[test]
fn layout_padding_enum_width_and_pointer_recursion() {
    let index = resolve::collect(&package(&[("", "struct Node { flag: u8, next: *Node, data: [u16; 3] } enum Mode: u8 { a = 0, b = 255 } hook root.hit(node: *Node, mode: Mode);")])).unwrap();
    let checked = types::check(&index).unwrap();
    let layout = &checked.layouts["test::Node"];
    assert_eq!((layout.size, layout.align), (24, 8));
    assert_eq!(
        layout.fields,
        BTreeMap::from([("flag".into(), 0), ("next".into(), 8), ("data".into(), 16)])
    );
    assert_eq!(checked.layouts["test::Mode"].size, 1);
}
#[test]
fn invalid_types_layouts_and_discriminants_fail() {
    for (source, message) in [
        ("struct A { child: A }", "recursive"),
        ("struct A { b: B } struct B { a: A }", "recursive"),
        ("struct A { x: Unknown }", "unresolved"),
        ("struct A { x: [u64; 18446744073709551615] }", "size limit"),
        ("struct A { x: [u8; 0] }", "zero-length"),
        ("struct A {}", "empty struct"),
        (
            "object root { x: [u64; 18446744073709551615] }",
            "size limit",
        ),
        ("enum E: u8 { x = 256 }", "does not fit"),
        ("enum E: bool { x = 0 }", "fixed-width"),
        ("enum E: i8 { x = -129 }", "does not fit"),
        ("enum E: u8 { x = 0, y = 0 }", "duplicate"),
        ("hook root.hit(x: u8, x: u32);", "duplicate"),
        ("fn f() {} struct A { x: f }", "ABI type"),
    ] {
        let index = resolve::collect(&package(&[("", source)])).unwrap();
        let errors = types::check(&index).unwrap_err();
        assert!(
            errors.iter().any(|e| e.message.contains(message)),
            "{source}: {errors:?}"
        );
    }
}
#[test]
fn forward_references_across_modules_and_scan_order() {
    let files = [
        ("z", "use a.A; pub struct Z { a: *A }"),
        ("a", "use z.Z; pub struct A { z: *Z }"),
    ];
    let a = resolve::collect(&package(&files)).unwrap();
    let b = resolve::collect(&package(&[files[1], files[0]])).unwrap();
    assert_eq!(
        a.symbols.keys().collect::<Vec<_>>(),
        b.symbols.keys().collect::<Vec<_>>()
    );
    assert!(types::check(&a).is_ok());
}
#[test]
fn thirty_modules_match_single_file_layouts() {
    let single = (0..30)
        .map(|i| format!("pub struct T{i} {{ id: u32, count: u64 }}"))
        .collect::<Vec<_>>()
        .join("\n");
    let a = types::check(&resolve::collect(&package(&[("", &single)])).unwrap()).unwrap();
    let files: Vec<_> = (0..30)
        .map(|i| {
            (
                format!("m{i}"),
                format!("pub struct T{i} {{ id: u32, count: u64 }}"),
            )
        })
        .collect();
    let borrowed: Vec<_> = files
        .iter()
        .map(|(a, b)| (a.as_str(), b.as_str()))
        .collect();
    let b = types::check(&resolve::collect(&package(&borrowed)).unwrap()).unwrap();
    for i in 0..30 {
        assert_eq!(
            a.layouts[&format!("test::T{i}")],
            b.layouts[&format!("test::m{i}.T{i}")]
        );
    }
}

#[test]
fn pending_relative_alias_does_not_fall_back_to_root() {
    let p = package(&[
        ("api", "pub struct X { id: u8 }"),
        ("local", "use api.X; fn consume(x: X) {}"),
        ("local.api", "pub use test.z.X;"),
        ("z", "pub struct X { id: u64 }"),
    ]);
    let index = resolve::collect(&p).unwrap();
    assert_eq!(index.resolve("local", "X").unwrap().id, "test::z.X");
    let affected = index.affected_modules(&BTreeSet::from(["local.api".into()]));
    assert!(affected.contains("local"));
}

#[test]
fn entry_signatures_compare_canonical_types_and_passing_modes() {
    for (entry, valid) in [
        ("fn watch on root.hit(x: *Handle, n: u32) {}", true),
        ("fn watch on root.hit(x: Handle, n: u32) {}", false),
        ("fn watch on root.hit(x: *Handle, n: u64) {}", false),
        ("fn watch on root.hit(x: *Handle) {}", false),
        ("fn watch on root.missing(x: *Handle, n: u32) {}", false),
    ] {
        let source =
            format!("struct Handle {{ id: u64 }} hook root.hit(x: *Handle, n: u32); {entry}");
        let index = resolve::collect(&package(&[("", &source)])).unwrap();
        assert_eq!(types::check(&index).is_ok(), valid, "{entry}");
    }
}
