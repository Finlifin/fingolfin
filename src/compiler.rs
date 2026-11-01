//! Frontend orchestration for a standalone observation source and model package.
use crate::util::hash::sha256;
use crate::{
    resolve, sema,
    source::{self, LoadedPackage, Manifest, Module, Package},
    syntax::{self, Diagnostic, ItemKind, Span},
    types,
};
use serde::Serialize;
use std::{collections::BTreeMap, path::Path};

#[derive(Debug, Serialize)]
pub struct CheckedProgram {
    pub format_version: u32,
    pub target: String,
    pub program_source_hash: String,
    pub model_name: String,
    pub model_version: String,
    pub model_source_hash: String,
    pub entries: Vec<String>,
    pub property_availability: Vec<source::PropertyAvailability>,
    pub index: resolve::Index,
    pub types: types::CheckedTypes,
    pub bodies: sema::CheckedBodies,
}
fn error(file: &str, message: impl Into<String>) -> Vec<Diagnostic> {
    vec![Diagnostic {
        file: file.into(),
        span: Span { start: 0, end: 0 },
        message: message.into(),
    }]
}
/// Validate exact property contracts after model name and type resolution.
/// Shared by observation compilation and typed model inspection/ABI emission.
pub fn validate_availability(
    model: &LoadedPackage,
    index: &resolve::Index,
    types: &types::CheckedTypes,
) -> Result<(), Vec<Diagnostic>> {
    let mut seen = std::collections::BTreeSet::new();
    for entry in &model.manifest.availability {
        let bad = |message: &str| {
            error(
                "Fingolfin.toml",
                format!(
                    "availability {} {:?}: {message}",
                    entry.object, entry.fields
                ),
            )
        };
        if entry.fields.len() > 32
            || entry.object.len() > 4096
            || entry.fields.iter().any(|f| f.is_empty() || f.len() > 4096)
        {
            return Err(bad("invalid property path bounds"));
        }
        if !seen.insert((&entry.object, &entry.fields)) {
            return Err(bad("duplicate property contract"));
        }
        let mut owner = index
            .objects
            .get(&entry.object)
            .cloned()
            .ok_or_else(|| bad("unknown model object"))?;
        let mut object = Some(entry.object.clone());
        let mut scalar = false;
        for field in &entry.fields {
            if scalar {
                return Err(bad("cannot traverse a scalar property"));
            }
            let child_path = object.as_ref().map(|o| format!("{o}.{field}"));
            let child = child_path.as_ref().and_then(|p| index.objects.get(p));
            let ty = types
                .fields
                .get(&owner)
                .and_then(|fields| fields.get(field));
            match (child, ty) {
                (Some(_), Some(_)) => return Err(bad("ambiguous object and field")),
                (Some(id), None) => {
                    owner = id.clone();
                    object = child_path;
                }
                (None, Some(types::Ty::Struct(id))) => {
                    owner = id.clone();
                    object = None;
                }
                (None, Some(_)) => {
                    scalar = true;
                    object = None;
                }
                (None, None) => return Err(bad("unknown property field")),
            }
        }
    }
    Ok(())
}
pub fn check_source(
    file: &str,
    source: &str,
    model: &LoadedPackage,
) -> Result<CheckedProgram, Vec<Diagnostic>> {
    let ast = syntax::parse(file, source).map_err(|e| vec![e])?;
    let forbidden: Vec<_> = ast
        .items
        .iter()
        .filter(|i| {
            matches!(
                i.value.kind,
                ItemKind::Hook { .. } | ItemKind::Object { .. }
            )
        })
        .map(|i| Diagnostic {
            file: file.into(),
            span: i.span,
            message: "observation programs cannot declare model hooks or objects".into(),
        })
        .collect();
    if !forbidden.is_empty() {
        return Err(forbidden);
    }
    // Check the model independently first: it cannot resolve a missing model helper
    // by accidentally capturing a declaration from the observation program.
    let model_index = resolve::collect(model)?;
    let model_types = types::check(&model_index)?;
    sema::check(&model_index, &model_types)?;
    validate_availability(model, &model_index, &model_types)?;
    let program_name = if model.manifest.package.name == "program" {
        "observation"
    } else {
        "program"
    };
    let program_source_hash = sha256(source.as_bytes());
    let package = LoadedPackage {
        manifest: Manifest {
            availability: vec![],
            package: Package {
                name: program_name.into(),
                version: "0.1.0".into(),
                source: "src".into(),
            },
        },
        modules: BTreeMap::from([(
            "".into(),
            Module {
                name: "".into(),
                file: file.into(),
                source_hash: program_source_hash.clone(),
                ast,
            },
        )]),
        source_hash: program_source_hash.clone(),
    };
    let index = resolve::collect_with_dependencies(&package, &[model_index])?;
    let checked_types = types::check(&index)?;
    let bodies = sema::check(&index, &checked_types)?;
    let entries = index
        .symbols
        .values()
        .filter(|s| {
            s.module.is_empty()
                && matches!(&s.declaration.value.kind,ItemKind::Function(f) if f.hook.is_some())
        })
        .map(|s| s.id.clone())
        .collect();
    Ok(CheckedProgram {
        format_version: 1,
        target: "x86_64-linux-little-endian".into(),
        program_source_hash,
        model_name: model.manifest.package.name.clone(),
        model_version: model.manifest.package.version.clone(),
        model_source_hash: model.source_hash.clone(),
        entries,
        property_availability: model.manifest.availability.clone(),
        index,
        types: checked_types,
        bodies,
    })
}
pub fn check(
    source: &Path,
    model: &Path,
    expected_model_hash: Option<&str>,
) -> Result<CheckedProgram, Vec<Diagnostic>> {
    let model = source::load(model)?;
    if expected_model_hash.is_some_and(|expected| expected != model.source_hash) {
        return Err(error(
            "Fingolfin.toml",
            "model source hash differs from the required pin",
        ));
    }
    let file = source.display().to_string();
    let text = std::fs::read_to_string(source).map_err(|e| error(&file, e.to_string()))?;
    check_source(&file, &text, &model)
}
