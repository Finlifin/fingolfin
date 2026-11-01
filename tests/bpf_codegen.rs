use fingolfin::{codegen, compiler, source};
use std::{fs, path::Path, process::Command};
#[test]
fn bpf_functions_compile_without_external_runtime_calls_and_host_frame_compiles() {
    let model = source::load(Path::new("models/fixture")).unwrap();
    let program = compiler::check_source(
        "arithmetic.fg",
        include_str!("../integration/generated-bpf/arithmetic.fg"),
        &model,
    )
    .unwrap();
    let dir = std::env::temp_dir().join(format!("fg-bpf-test-{}", std::process::id()));
    fs::create_dir_all(&dir).unwrap();
    for function in [
        "add", "mul", "smul", "sadd", "sub", "div", "rem", "branch", "short",
    ] {
        let generated = codegen::generate_bpf(&program, &format!("program::{function}")).unwrap();
        fs::write(dir.join("program.c"), generated.source).unwrap();
        fs::write(dir.join("frame.h"), generated.header).unwrap();
        let output = Command::new("clang")
            .args([
                "-target",
                "bpfel",
                "-mcpu=v3",
                "-ffreestanding",
                "-O2",
                "-c",
            ])
            .arg(dir.join("program.c"))
            .arg("-o")
            .arg(dir.join("program.o"))
            .output()
            .unwrap();
        assert!(
            output.status.success(),
            "{}: {}",
            function,
            String::from_utf8_lossy(&output.stderr)
        );
        let symbols = Command::new("llvm-nm")
            .arg("-u")
            .arg(dir.join("program.o"))
            .output()
            .unwrap();
        assert!(symbols.status.success());
        assert!(
            symbols.stdout.is_empty(),
            "unresolved BPF runtime calls: {}",
            String::from_utf8_lossy(&symbols.stdout)
        );
        let host = Command::new("clang")
            .args(["-std=c11", "-x", "c", "-fsyntax-only"])
            .arg(dir.join("frame.h"))
            .output()
            .unwrap();
        assert!(
            host.status.success(),
            "{}",
            String::from_utf8_lossy(&host.stderr)
        );
    }
    assert!(codegen::generate_bpf(&program, "program::missing").is_err());
    fs::remove_dir_all(dir).unwrap();
}
