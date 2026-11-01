use fingolfin::{
    abi, resolve,
    source::{LoadedPackage, Manifest, Module, Package},
    syntax, types,
};
use std::{collections::BTreeMap, process::Command};
fn model(source: &str) -> (resolve::Index, types::CheckedTypes) {
    let package = LoadedPackage {
        manifest: Manifest {
            availability: vec![],
            package: Package {
                name: "abi".into(),
                version: "0.1.0".into(),
                source: "src".into(),
            },
        },
        modules: BTreeMap::from([(
            "".into(),
            Module {
                name: "".into(),
                file: "abi.fg".into(),
                source_hash: String::new(),
                ast: syntax::parse("abi.fg", source).unwrap(),
            },
        )]),
        source_hash: String::new(),
    };
    let index = resolve::collect(&package).unwrap();
    let types = types::check(&index).unwrap();
    (index, types)
}
#[repr(C)]
struct Inner {
    id: u32,
    flag: u8,
}
#[repr(C)]
struct Packet {
    lead: u8,
    inner: Inner,
    data: [u16; 3],
    link: *mut Packet,
    tag: u8,
}
#[test]
fn generated_header_agrees_with_c_cpp_and_rust_and_preserves_field_order() {
    let (index, types) = model(
        "struct Packet { lead: u8, inner: Inner, data: [u16;3], link: *Packet, tag: Mode } struct Inner { id:u32, flag:bool } enum Mode:u8 { low=0, high=255 } enum Signed:i64 { low=-9223372036854775808, high=9223372036854775807 } fn roundtrip(p:Packet) -> Packet { return p; } fn rust_callback(p:Packet) -> Packet { return p; } hook root.hit(p: *Packet, matrix: *[u8;4]);",
    );
    let header = abi::generate(&index, &types).unwrap();
    let layout = &types.layouts["abi::Packet"];
    assert_eq!(layout.size, size_of::<Packet>() as u64);
    assert_eq!(layout.align, align_of::<Packet>() as u64);
    assert_eq!(
        layout.fields["inner"],
        std::mem::offset_of!(Packet, inner) as u64
    );
    assert_eq!(
        layout.fields["link"],
        std::mem::offset_of!(Packet, link) as u64
    );
    let temp = std::env::temp_dir().join(format!("fg-generated-abi-{}", std::process::id()));
    std::fs::create_dir_all(&temp).unwrap();
    let packet = abi::identifier("abi::Packet");
    let function = abi::identifier("abi::roundtrip");
    let data = abi::identifier("data");
    let mode = abi::identifier("abi::Mode");
    let source = format!(
        "{}\n{packet} {function}({packet} p) {{ p.{data}[1] += 5; return p; }}\nint main(void) {{ {packet} p = {{0}}; p.{data}[1]=7; p={function}(p); return p.{data}[1]!=12 || !{mode}_valid(255) || {mode}_valid(1); }}\n",
        header.source
    );
    std::fs::write(temp.join("probe.c"), source).unwrap();
    for (compiler, language, standard) in [("clang", "c", "c11"), ("clang++", "c++", "c++20")] {
        let binary = temp.join(format!("probe-{language}"));
        let output = Command::new(compiler)
            .args(["-x", language, "-Werror", "-Wno-pragma-once-outside-header"])
            .arg(format!("-std={standard}"))
            .arg(temp.join("probe.c"))
            .arg("-o")
            .arg(&binary)
            .output()
            .unwrap();
        assert!(
            output.status.success(),
            "{}",
            String::from_utf8_lossy(&output.stderr)
        );
        assert!(Command::new(binary).status().unwrap().success());
    }
    let rust = abi::generate_rust(&index, &types).unwrap();
    std::fs::write(temp.join("generated.rs"), rust).unwrap();
    let output = Command::new("rustc")
        .args([
            "--edition=2024",
            "--crate-type=lib",
            "--emit=metadata",
            "-Dwarnings",
        ])
        .arg(temp.join("generated.rs"))
        .arg("-o")
        .arg(temp.join("generated.rmeta"))
        .output()
        .unwrap();
    assert!(
        output.status.success(),
        "{}",
        String::from_utf8_lossy(&output.stderr)
    );
    let callback = abi::identifier("abi::rust_callback");
    let inner = abi::identifier("inner");
    let id = abi::identifier("id");
    let native = format!(
        "{}\n{packet} {function}({packet} p) {{ p.{data}[1]+=5; return {callback}(p); }}\n",
        header.source
    );
    std::fs::write(temp.join("native.c"), native).unwrap();
    let linked_rust = format!(
        "{}\nmod implementation {{ #[unsafe(no_mangle)] pub extern \"C\" fn {callback}(mut p: super::{packet}) -> super::{packet} {{ p.{inner}.{id}+=2; p }} }}\nfn main() {{ let mut p: {packet}=unsafe {{ core::mem::zeroed() }}; p.{data}[1]=7; p.{inner}.{id}=19; let p=unsafe {{ {function}(p) }}; assert_eq!(p.{data}[1],12); assert_eq!(p.{inner}.{id},21); }}\n",
        abi::generate_rust(&index, &types).unwrap()
    );
    std::fs::write(temp.join("linked.rs"), linked_rust).unwrap();
    for (compiler, language, standard) in [("clang", "c", "c11"), ("clang++", "c++", "c++20")] {
        let object = temp.join(format!("native-{language}.o"));
        let output = Command::new(compiler)
            .args([
                "-x",
                language,
                "-Werror",
                "-Wno-pragma-once-outside-header",
                "-c",
            ])
            .arg(format!("-std={standard}"))
            .arg(temp.join("native.c"))
            .arg("-o")
            .arg(&object)
            .output()
            .unwrap();
        assert!(
            output.status.success(),
            "{}",
            String::from_utf8_lossy(&output.stderr)
        );
        let binary = temp.join(format!("linked-{language}"));
        let output = Command::new("rustc")
            .args(["--edition=2024", "-Dwarnings"])
            .arg(temp.join("linked.rs"))
            .arg("-C")
            .arg(format!("link-arg={}", object.display()))
            .arg("-o")
            .arg(&binary)
            .output()
            .unwrap();
        assert!(
            output.status.success(),
            "{}",
            String::from_utf8_lossy(&output.stderr)
        );
        assert!(Command::new(binary).status().unwrap().success());
    }
    std::fs::remove_dir_all(temp).unwrap();
}
#[test]
fn direct_array_arguments_are_not_silently_decayed_to_pointers() {
    for source in ["hook root.hit(a:[u8;4]);", "fn f() -> [u8;4] {}"] {
        let (index, types) = model(source);
        assert!(
            abi::generate(&index, &types)
                .unwrap_err()
                .contains("by-value")
        );
    }
}
#[test]
fn encoding_is_collision_free_and_emission_is_deterministic() {
    assert_ne!(abi::identifier("a.b"), abi::identifier("a_b"));
    let (index, types) = model("struct switch { auto:u8 } hook root.hit(value:*switch);");
    assert_eq!(
        abi::generate(&index, &types).unwrap().source,
        abi::generate(&index, &types).unwrap().source
    );
}
