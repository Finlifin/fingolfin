//! Clang orchestration; output is only published after every entry succeeds.
use crate::{
    codegen,
    compiler::CheckedProgram,
    ir,
    package::{self, Entry, Manifest},
    util::hash::sha256,
};
use std::{
    collections::BTreeMap,
    fs,
    path::{Path, PathBuf},
    process::Command,
    sync::atomic::{AtomicU64, Ordering},
};
static NEXT: AtomicU64 = AtomicU64::new(0);
struct Scratch(PathBuf);
impl Scratch {
    fn new() -> Result<Self, String> {
        loop {
            let p = std::env::temp_dir().join(format!(
                "fg-build-{}-{}",
                std::process::id(),
                NEXT.fetch_add(1, Ordering::Relaxed)
            ));
            match fs::create_dir(&p) {
                Ok(()) => return Ok(Self(p)),
                Err(e) if e.kind() == std::io::ErrorKind::AlreadyExists => continue,
                Err(e) => return Err(e.to_string()),
            }
        }
    }
}
impl Drop for Scratch {
    fn drop(&mut self) {
        let _ = fs::remove_dir_all(&self.0);
    }
}
pub fn compile(
    program: &CheckedProgram,
    functions: &[String],
    clang: &Path,
) -> Result<Vec<u8>, String> {
    let module = ir::lower(program)?;
    let selected = if functions.is_empty() {
        &program.entries
    } else {
        functions
    };
    if selected.is_empty() {
        return Err("no entries; select an ordinary function with --function".into());
    }
    let version = Command::new(clang)
        .arg("--version")
        .output()
        .map_err(|e| e.to_string())?;
    if !version.status.success() {
        return Err("cannot read Clang version".into());
    }
    let flags = vec![
        "-target",
        "bpfel",
        "-mcpu=v3",
        "-ffreestanding",
        "-O2",
        "-c",
    ];
    let mut entries = BTreeMap::new();
    for function in selected {
        if entries.contains_key(function) {
            return Err(format!("duplicate selected function {function}"));
        }
        let f = module
            .functions
            .get(function)
            .ok_or_else(|| format!("unknown function {function}"))?;
        let generated = codegen::generate_bpf(program, function)?;
        let dir = Scratch::new()?;
        fs::write(dir.0.join("entry.c"), generated.source).map_err(|e| e.to_string())?;
        let output = Command::new(clang)
            .args(&flags)
            .args(["entry.c", "-o", "entry.o"])
            .current_dir(&dir.0)
            .output()
            .map_err(|e| e.to_string())?;
        if !output.status.success() {
            return Err(format!(
                "Clang failed for {function}: {}",
                String::from_utf8_lossy(&output.stderr)
            ));
        }
        let elf = fs::read(dir.0.join("entry.o")).map_err(|e| e.to_string())?;
        crate::util::elf::validate_bpf(&elf)?;
        let signature = serde_json::json!({"parameters":f.parameters.iter().map(|slot|&f.locals[*slot as usize].ty).collect::<Vec<_>>(),"bindings":f.bindings.iter().map(|(name,slot)|(name,&f.locals[*slot as usize].ty)).collect::<BTreeMap<_,_>>(),"returns":f.returns,"hook":f.hook});
        let frame_schema = crate::frame::Layout::for_function(program, function)?.schema_bytes()?;
        entries.insert(
            function.clone(),
            Entry {
                function: function.clone(),
                helpers: crate::util::elf::helper_ids(&elf)?,
                hook: f.hook.clone(),
                dependencies: program.bodies.calls[function].iter().cloned().collect(),
                signature_hash: sha256(&serde_json::to_vec(&signature).map_err(|e| e.to_string())?),
                signature,
                elf_hash: sha256(&elf),
                elf,
                frame_schema_hash: sha256(&frame_schema),
                frame_schema,
                frame_hash: sha256(generated.header.as_bytes()),
                frame_header: generated.header,
            },
        );
    }
    package::encode(Manifest {
        version: 3,
        profile: "development-function-frames".into(),
        target: "bpfel-v3".into(),
        program_source_hash: program.program_source_hash.clone(),
        model_source_hash: program.model_source_hash.clone(),
        compiler_version: env!("CARGO_PKG_VERSION").into(),
        clang_version: String::from_utf8_lossy(&version.stdout).trim().into(),
        clang_flags: flags.iter().map(|s| s.to_string()).collect(),
        entries,
        typed_ir: serde_json::to_value(&module).map_err(|e| e.to_string())?,
    })
}
