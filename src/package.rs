//! Development .fgo container. Integrity hashes detect corruption, not authorship.
use crate::util::{elf::validate_bpf, hash::sha256};
use serde::{Deserialize, Serialize};
use std::collections::BTreeMap;
pub const MAX_BYTES: usize = 32 * 1024 * 1024;
#[derive(Debug, Clone, Serialize, Deserialize)]
#[serde(deny_unknown_fields)]
pub struct Entry {
    pub function: String,
    pub hook: Option<String>,
    pub dependencies: Vec<String>,
    pub helpers: Vec<u32>,
    pub signature: serde_json::Value,
    pub signature_hash: String,
    pub elf: Vec<u8>,
    pub elf_hash: String,
    pub frame_schema: Vec<u8>,
    pub frame_schema_hash: String,
    pub frame_header: String,
    pub frame_hash: String,
}
#[derive(Debug, Clone, Serialize, Deserialize)]
#[serde(deny_unknown_fields)]
pub struct Manifest {
    pub version: u32,
    /// Explicitly prevents confusing this container with a deployable runtime package.
    pub profile: String,
    pub target: String,
    pub program_source_hash: String,
    pub model_source_hash: String,
    pub compiler_version: String,
    pub clang_version: String,
    pub clang_flags: Vec<String>,
    pub entries: BTreeMap<String, Entry>,
    pub typed_ir: serde_json::Value,
}
#[derive(Debug, Serialize, Deserialize)]
#[serde(deny_unknown_fields)]
struct Container {
    magic: String,
    manifest: Manifest,
    integrity: String,
}
fn digest(manifest: &Manifest) -> Result<String, String> {
    Ok(sha256(
        &serde_json::to_vec(manifest).map_err(|e| e.to_string())?,
    ))
}
pub fn encode(manifest: Manifest) -> Result<Vec<u8>, String> {
    validate(&manifest)?;
    let integrity = digest(&manifest)?;
    let bytes = serde_json::to_vec(&Container {
        magic: "FINGOLFIN-FGO".into(),
        manifest,
        integrity,
    })
    .map_err(|e| e.to_string())?;
    if bytes.len() > MAX_BYTES {
        return Err("package exceeds 32 MiB limit".into());
    }
    Ok(bytes)
}
pub fn decode(bytes: &[u8]) -> Result<Manifest, String> {
    if bytes.len() > MAX_BYTES {
        return Err("package exceeds 32 MiB limit".into());
    }
    let container: Container = serde_json::from_slice(bytes).map_err(|e| e.to_string())?;
    if container.magic != "FINGOLFIN-FGO" || digest(&container.manifest)? != container.integrity {
        return Err("invalid package magic or integrity".into());
    }
    validate(&container.manifest)?;
    Ok(container.manifest)
}
fn hash_valid(s: &str) -> bool {
    s.len() == 64
        && s.bytes()
            .all(|b| b.is_ascii_digit() || (b'a'..=b'f').contains(&b))
}
fn validate(m: &Manifest) -> Result<(), String> {
    if m.version != 3 || m.profile != "development-function-frames" || m.target != "bpfel-v3" {
        return Err("unsupported package version/profile/target".into());
    }
    if !hash_valid(&m.program_source_hash)
        || !hash_valid(&m.model_source_hash)
        || m.entries.is_empty()
    {
        return Err("missing package identity or entries".into());
    }
    for (id, e) in &m.entries {
        if id != &e.function
            || sha256(&e.elf) != e.elf_hash
            || sha256(e.frame_schema.as_slice()) != e.frame_schema_hash
            || sha256(e.frame_header.as_bytes()) != e.frame_hash
            || sha256(&serde_json::to_vec(&e.signature).map_err(|e| e.to_string())?)
                != e.signature_hash
        {
            return Err(format!("entry integrity mismatch: {id}"));
        }
        crate::frame_schema::validate(&e.frame_schema)?;
        validate_bpf(&e.elf)?;
        if e.helpers != crate::util::elf::helper_ids(&e.elf)?
            || e.helpers.iter().any(|id| ![1, 2, 3, 4].contains(id))
        {
            return Err(format!(
                "helper requirements mismatch or unsupported helper: {id}"
            ));
        }
    }
    Ok(())
}

/// Bound the file read before JSON decoding allocates the manifest.
pub fn read(path: &std::path::Path) -> Result<Manifest, String> {
    use std::io::Read;
    let file = std::fs::File::open(path).map_err(|e| format!("{}: {e}", path.display()))?;
    let mut bytes = Vec::new();
    file.take(MAX_BYTES as u64 + 1)
        .read_to_end(&mut bytes)
        .map_err(|e| e.to_string())?;
    decode(&bytes)
}
