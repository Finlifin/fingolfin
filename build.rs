use std::{env, path::PathBuf, process::Command};
fn main() {
    println!("cargo:rerun-if-env-changed=FINGOLFIN_UBPF_ROOT");
    println!("cargo:rerun-if-env-changed=FINGOLFIN_UBPF_BUILD");
    println!("cargo:rerun-if-changed=sdk/vm/bridge.c");
    if env::var_os("CARGO_FEATURE_UBPF").is_none() {
        return;
    }
    let root = PathBuf::from(
        env::var_os("FINGOLFIN_UBPF_ROOT").expect("ubpf feature requires FINGOLFIN_UBPF_ROOT"),
    );
    let build = PathBuf::from(
        env::var_os("FINGOLFIN_UBPF_BUILD").expect("ubpf feature requires FINGOLFIN_UBPF_BUILD"),
    );
    let revision = Command::new("git")
        .arg("-C")
        .arg(&root)
        .args(["rev-parse", "HEAD"])
        .output()
        .expect("read uBPF revision");
    assert!(revision.status.success());
    assert_eq!(
        String::from_utf8_lossy(&revision.stdout).trim(),
        "8d2dd3580ab80f9b4a79a9d2fa2420aa38fc7521",
        "unsupported uBPF revision"
    );
    for path in [root.join("vm/inc/ubpf.h"), build.join("lib/libubpf.a")] {
        println!("cargo:rerun-if-changed={}", path.display());
    }
    let out = PathBuf::from(env::var_os("OUT_DIR").unwrap());
    assert!(
        Command::new("clang")
            .args([
                "-std=c11",
                "-O2",
                "-Wall",
                "-Wextra",
                "-Werror",
                "-fPIC",
                "-c",
                "sdk/vm/bridge.c",
                "-o"
            ])
            .arg(out.join("bridge.o"))
            .arg("-I")
            .arg(root.join("vm/inc"))
            .arg("-I")
            .arg(build.join("vm"))
            .status()
            .unwrap()
            .success()
    );
    assert!(
        Command::new("ar")
            .arg("crs")
            .arg(out.join("libfgvmshim.a"))
            .arg(out.join("bridge.o"))
            .status()
            .unwrap()
            .success()
    );
    println!("cargo:rustc-link-search=native={}", out.display());
    println!(
        "cargo:rustc-link-search=native={}",
        build.join("lib").display()
    );
    println!("cargo:rustc-link-lib=static=fgvmshim");
    println!("cargo:rustc-link-lib=static=ubpf");
    println!("cargo:rustc-link-lib=m");
}
