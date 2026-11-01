//! Deterministic package discovery. Loading source never activates an entry.
use crate::syntax::{self, Diagnostic, File, Span};
use crate::util::hash::{ContentHash, sha256};
use serde::{Deserialize, Serialize};
use std::{
    collections::BTreeMap,
    fs,
    path::{Component, Path, PathBuf},
};

#[derive(Debug, Clone, Deserialize, Serialize)]
#[serde(deny_unknown_fields)]
pub struct Manifest {
    pub package: Package,
    #[serde(default)]
    pub availability: Vec<PropertyAvailability>,
}
/// Trusted model contract for an exact property path. No prefix inheritance.
#[derive(Debug, Clone, Deserialize, Serialize)]
#[serde(deny_unknown_fields)]
pub struct PropertyAvailability {
    pub object: String,
    #[serde(default)]
    pub fields: Vec<String>,
    pub stage: crate::gate::Stage,
}
#[derive(Debug, Clone, Deserialize, Serialize)]
#[serde(deny_unknown_fields)]
pub struct Package {
    pub name: String,
    pub version: String,
    #[serde(default = "default_source")]
    pub source: String,
}
fn default_source() -> String {
    "src".into()
}
#[derive(Debug, Clone, Serialize)]
pub struct Module {
    pub name: String,
    pub file: String,
    pub source_hash: String,
    pub ast: File,
}
#[derive(Debug, Clone, Serialize)]
pub struct LoadedPackage {
    pub manifest: Manifest,
    pub modules: BTreeMap<String, Module>,
    /// Content identity, not the later ABI/schema compatibility fingerprint.
    pub source_hash: String,
}
fn error(path: &Path, message: impl Into<String>) -> Diagnostic {
    Diagnostic {
        file: path.display().to_string(),
        span: Span { start: 0, end: 0 },
        message: message.into(),
    }
}
fn valid_ident(s: &str) -> bool {
    let mut chars = s.chars();
    chars
        .next()
        .is_some_and(|c| c.is_ascii_alphabetic() || c == '_')
        && chars.all(|c| c.is_ascii_alphanumeric() || c == '_')
}
fn scan(root: &Path, dir: &Path, paths: &mut Vec<PathBuf>, errors: &mut Vec<Diagnostic>) {
    let entries = match fs::read_dir(dir) {
        Ok(e) => e,
        Err(e) => {
            errors.push(error(dir, e.to_string()));
            return;
        }
    };
    let mut entries = entries.collect::<std::io::Result<Vec<_>>>();
    let entries = match &mut entries {
        Ok(e) => {
            e.sort_by_key(|e| e.file_name());
            e
        }
        Err(e) => {
            errors.push(error(dir, e.to_string()));
            return;
        }
    };
    for entry in entries {
        let path = entry.path();
        let kind = match entry.file_type() {
            Ok(k) => k,
            Err(e) => {
                errors.push(error(&path, e.to_string()));
                continue;
            }
        };
        if kind.is_symlink() {
            errors.push(error(&path, "source symlinks are not allowed"));
            continue;
        }
        if kind.is_dir() {
            let name = entry.file_name().to_string_lossy().into_owned();
            if !valid_ident(&name) {
                errors.push(error(&path, "directory name must be a language identifier"));
                continue;
            }
            scan(root, &path, paths, errors);
        } else if kind.is_file() && path.extension().is_some_and(|x| x == "fg") {
            paths.push(path.strip_prefix(root).unwrap().to_owned());
        }
    }
}
pub fn load(path: &Path) -> Result<LoadedPackage, Vec<Diagnostic>> {
    let manifest_path = path.join("Fingolfin.toml");
    let raw = fs::read_to_string(&manifest_path)
        .map_err(|e| vec![error(&manifest_path, e.to_string())])?;
    let manifest: Manifest =
        toml::from_str(&raw).map_err(|e| vec![error(&manifest_path, e.to_string())])?;
    if !valid_ident(&manifest.package.name) || manifest.package.version.is_empty() {
        return Err(vec![error(
            &manifest_path,
            "package requires an identifier name and nonempty version",
        )]);
    }
    let source = Path::new(&manifest.package.source);
    if source.as_os_str().is_empty()
        || source
            .components()
            .any(|c| !matches!(c, Component::Normal(_)))
    {
        return Err(vec![error(
            &manifest_path,
            "source must be a relative path without . or .. components",
        )]);
    }
    let mut ancestor = path.to_owned();
    for component in source.components() {
        ancestor.push(component);
        if fs::symlink_metadata(&ancestor).is_ok_and(|m| m.file_type().is_symlink()) {
            return Err(vec![error(&ancestor, "source symlinks are not allowed")]);
        }
    }
    let root = path.join(source);
    let mut errors = vec![];
    let mut paths = vec![];
    scan(&root, &root, &mut paths, &mut errors);
    if !paths.iter().any(|p| p == Path::new("lib.fg")) {
        errors.push(error(&root, "package source root requires lib.fg"));
    }
    let mut modules = BTreeMap::new();
    let mut hasher = ContentHash::new();
    hasher.field(manifest.package.name.as_bytes());
    hasher.field(manifest.package.version.as_bytes());
    if !manifest.availability.is_empty() {
        let mut entries = manifest.availability.clone();
        entries
            .sort_by(|a, b| (&a.object, &a.fields, a.stage).cmp(&(&b.object, &b.fields, b.stage)));
        hasher.field(b"property-availability-v1");
        hasher.field(&serde_json::to_vec(&entries).expect("serializable availability"));
    }
    for relative in paths {
        let absolute = root.join(&relative);
        let stem = relative.file_stem().unwrap().to_string_lossy();
        if !valid_ident(&stem) {
            errors.push(error(&absolute, "file name must be a language identifier"));
            continue;
        }
        if stem != "mod" && absolute.with_extension("").is_dir() {
            errors.push(error(
                &absolute,
                "file and directory define the same module",
            ));
            continue;
        }
        if relative == Path::new("mod.fg") {
            errors.push(error(&absolute, "root module must be lib.fg, not mod.fg"));
            continue;
        }
        let mut segments: Vec<String> = relative
            .parent()
            .unwrap()
            .components()
            .map(|c| c.as_os_str().to_string_lossy().into_owned())
            .collect();
        if relative != Path::new("lib.fg") && stem != "mod" {
            segments.push(stem.into_owned());
        }
        let module_name = segments.join(".");
        let source = match fs::read_to_string(&absolute) {
            Ok(s) => s,
            Err(e) => {
                errors.push(error(&absolute, e.to_string()));
                continue;
            }
        };
        let file = absolute.display().to_string();
        let ast = match syntax::parse(&file, &source) {
            Ok(ast) => ast,
            Err(e) => {
                errors.push(e);
                continue;
            }
        };
        hasher.field(module_name.as_bytes());
        hasher.field(source.as_bytes());
        let module = Module {
            name: module_name.clone(),
            file,
            source_hash: sha256(source.as_bytes()),
            ast,
        };
        if modules.insert(module_name, module).is_some() {
            errors.push(error(&absolute, "duplicate module"));
        }
    }
    if errors.is_empty() {
        Ok(LoadedPackage {
            manifest,
            modules,
            source_hash: hasher.finish(),
        })
    } else {
        Err(errors)
    }
}
