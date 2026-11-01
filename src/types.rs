//! Fixed-layout type checking for the Linux x86-64 little-endian ABI profile.
use crate::{resolve::Index, syntax::*};
use serde::Serialize;
use std::collections::{BTreeMap, BTreeSet};

#[derive(Debug, Clone, PartialEq, Eq, Serialize)]
pub enum Ty {
    Integer { signed: bool, bits: u8 },
    Bool,
    Struct(String),
    Enum(String),
    Pointer(Box<Ty>),
    Array(Box<Ty>, u64),
}
#[derive(Debug, Clone, PartialEq, Eq, Serialize)]
pub struct Layout {
    pub size: u64,
    pub align: u64,
    pub fields: BTreeMap<String, u64>,
}
#[derive(Debug, Clone, Serialize)]
pub struct Signature {
    pub parameters: Vec<Ty>,
    pub returns: Option<Ty>,
}
#[derive(Debug, Clone, Serialize)]
pub struct CheckedTypes {
    pub layouts: BTreeMap<String, Layout>,
    pub signatures: BTreeMap<String, Signature>,
    pub fields: BTreeMap<String, BTreeMap<String, Ty>>,
}
fn builtin(name: &str) -> Option<Ty> {
    if name == "bool" {
        return Some(Ty::Bool);
    }
    let signed = name.starts_with('i');
    if !signed && !name.starts_with('u') {
        return None;
    }
    let bits = name[1..].parse::<u8>().ok()?;
    if [8, 16, 32, 64].contains(&bits) {
        Some(Ty::Integer { signed, bits })
    } else {
        None
    }
}
pub fn resolve_type(index: &Index, module: &str, ty: &Node<Type>) -> Result<Ty, String> {
    Ok(match &ty.value {
        Type::Named(name) => {
            if let Some(ty) = builtin(name) {
                return Ok(ty);
            }
            let symbol = index.resolve(module, name)?;
            match symbol.declaration.value.kind {
                ItemKind::Struct { .. } => Ty::Struct(symbol.id.clone()),
                ItemKind::Enum { .. } => Ty::Enum(symbol.id.clone()),
                _ => return Err(format!("`{name}` does not name an ABI type")),
            }
        }
        Type::Pointer(inner) => Ty::Pointer(Box::new(resolve_type(index, module, inner)?)),
        Type::Array(inner, length) => {
            if *length == 0 {
                return Err("zero-length arrays are outside the C ABI subset".into());
            }
            Ty::Array(Box::new(resolve_type(index, module, inner)?), *length)
        }
    })
}
pub fn integer_fits(value: i128, signed: bool, bits: u8) -> bool {
    if ![8, 16, 32, 64].contains(&bits) {
        return false;
    }
    if signed {
        let limit = 1i128 << (bits - 1);
        value >= -limit && value < limit
    } else {
        value >= 0 && value < (1i128 << bits)
    }
}
fn diagnostic(file: &str, span: Span, message: impl Into<String>) -> Diagnostic {
    Diagnostic {
        file: file.into(),
        span,
        message: message.into(),
    }
}
struct Layouts<'a> {
    index: &'a Index,
    fields: &'a BTreeMap<String, Vec<(String, Ty)>>,
    enums: &'a BTreeMap<String, Ty>,
    done: BTreeMap<String, Layout>,
    active: BTreeSet<String>,
}
fn align_up(size: u64, align: u64) -> Result<u64, String> {
    size.checked_add(align - 1)
        .map(|n| n / align * align)
        .filter(|n| *n <= i64::MAX as u64)
        .ok_or_else(|| "ABI layout exceeds target object size limit".into())
}
impl Layouts<'_> {
    fn ty(&mut self, ty: &Ty) -> Result<Layout, String> {
        let scalar = |size| Layout {
            size,
            align: size,
            fields: BTreeMap::new(),
        };
        match ty {
            Ty::Bool => Ok(scalar(1)),
            Ty::Integer { bits, .. } => Ok(scalar(u64::from(*bits) / 8)),
            Ty::Pointer(_) => Ok(scalar(8)),
            Ty::Struct(id) | Ty::Enum(id) => self.named(id),
            Ty::Array(inner, length) => {
                let inner = self.ty(inner)?;
                let size = inner
                    .size
                    .checked_mul(*length)
                    .filter(|n| *n <= i64::MAX as u64)
                    .ok_or("array layout exceeds target object size limit")?;
                Ok(Layout {
                    size,
                    align: inner.align,
                    fields: BTreeMap::new(),
                })
            }
        }
    }
    fn named(&mut self, id: &str) -> Result<Layout, String> {
        if let Some(layout) = self.done.get(id) {
            return Ok(layout.clone());
        }
        if !self.active.insert(id.into()) {
            return Err(format!("by-value recursive layout involving `{id}`"));
        }
        let result = (|| {
            if let Some(repr) = self.enums.get(id) {
                return self.ty(&repr.clone());
            }
            let fields = self
                .fields
                .get(id)
                .ok_or_else(|| format!("layout unavailable for `{id}`"))?
                .clone();
            if fields.is_empty() {
                return Err(format!("empty struct `{id}` is outside the C ABI subset"));
            }
            let mut layout = Layout {
                size: 0,
                align: 1,
                fields: BTreeMap::new(),
            };
            for (name, ty) in fields {
                let field = self.ty(&ty)?;
                layout.align = layout.align.max(field.align);
                layout.size = align_up(layout.size, field.align)?;
                layout.fields.insert(name, layout.size);
                layout.size = layout
                    .size
                    .checked_add(field.size)
                    .ok_or("struct size overflow")?;
            }
            layout.size = align_up(layout.size, layout.align)?;
            Ok(layout)
        })();
        self.active.remove(id);
        if let Ok(layout) = &result {
            self.done.insert(id.into(), layout.clone());
        }
        result
    }
}
pub fn check(index: &Index) -> Result<CheckedTypes, Vec<Diagnostic>> {
    let mut errors = vec![];
    let mut fields = BTreeMap::new();
    let mut all_fields: BTreeMap<String, BTreeMap<String, Ty>> = BTreeMap::new();
    let mut enums = BTreeMap::new();
    let mut signatures = BTreeMap::new();
    for (id, symbol) in &index.symbols {
        let resolve = |ty: &Node<Type>| {
            resolve_type(index, &symbol.module, ty)
                .map_err(|message| diagnostic(&symbol.file, ty.span, message))
        };
        let parameters = match &symbol.declaration.value.kind {
            ItemKind::Struct { fields, .. } | ItemKind::Object { fields, .. } => Some(fields),
            ItemKind::Hook { parameters, .. } => Some(parameters),
            ItemKind::Function(f) => Some(&f.parameters),
            _ => None,
        };
        if let Some(parameters) = parameters {
            let mut names = BTreeSet::new();
            let mut resolved = vec![];
            for parameter in parameters {
                if !names.insert(&parameter.value.name) {
                    errors.push(diagnostic(
                        &symbol.file,
                        parameter.span,
                        format!("duplicate parameter/field `{}`", parameter.value.name),
                    ));
                }
                match resolve(&parameter.value.ty) {
                    Ok(ty) => resolved.push((parameter.value.name.clone(), ty)),
                    Err(e) => errors.push(e),
                }
            }
            match &symbol.declaration.value.kind {
                ItemKind::Struct { .. } => {
                    fields.insert(id.clone(), resolved.clone());
                    all_fields.insert(id.clone(), resolved.into_iter().collect());
                }
                ItemKind::Object { .. } => {
                    all_fields.insert(id.clone(), resolved.into_iter().collect());
                }
                _ => {
                    let returns = if let ItemKind::Function(f) = &symbol.declaration.value.kind {
                        match f.returns.as_ref().map(resolve).transpose() {
                            Ok(t) => t,
                            Err(e) => {
                                errors.push(e);
                                None
                            }
                        }
                    } else {
                        None
                    };
                    signatures.insert(
                        id.clone(),
                        Signature {
                            parameters: resolved.into_iter().map(|(_, t)| t).collect(),
                            returns,
                        },
                    );
                }
            }
        }
        if let ItemKind::Enum { repr, variants, .. } = &symbol.declaration.value.kind {
            match resolve(repr) {
                Ok(ty @ Ty::Integer { .. }) => {
                    let Ty::Integer { signed, bits } = ty else {
                        unreachable!()
                    };
                    let mut names = BTreeSet::new();
                    let mut values = BTreeSet::new();
                    for variant in variants {
                        if !names.insert(&variant.value.0) || !values.insert(variant.value.1) {
                            errors.push(diagnostic(
                                &symbol.file,
                                variant.span,
                                "duplicate enum name or discriminant",
                            ));
                        }
                        if !integer_fits(variant.value.1, signed, bits) {
                            errors.push(diagnostic(
                                &symbol.file,
                                variant.span,
                                "enum discriminant does not fit its representation",
                            ));
                        }
                    }
                    enums.insert(id.clone(), ty);
                }
                Ok(_) => errors.push(diagnostic(
                    &symbol.file,
                    repr.span,
                    "enum representation must be a fixed-width integer",
                )),
                Err(e) => errors.push(e),
            }
        }
    }
    for (id, symbol) in &index.symbols {
        if let ItemKind::Function(function) = &symbol.declaration.value.kind
            && let Some(hook) = &function.hook
        {
            match index.hooks.get(&hook.value) {
                None => errors.push(diagnostic(
                    &symbol.file,
                    hook.span,
                    format!("unknown hook `{}`", hook.value),
                )),
                Some(target) => {
                    let target_symbol = &index.symbols[target];
                    if target_symbol.module != symbol.module
                        && !target_symbol.declaration.value.public
                    {
                        errors.push(diagnostic(
                            &symbol.file,
                            hook.span,
                            format!("hook `{}` is private", hook.value),
                        ));
                    }
                    if signatures[id].parameters != signatures[target].parameters {
                        errors.push(diagnostic(
                            &symbol.file,
                            hook.span,
                            format!("entry signature does not match hook `{}`", hook.value),
                        ));
                    }
                }
            }
        }
    }
    // Avoid cascades from missing types; independent declaration diagnostics above
    // are still batched across all modules.
    if !errors.is_empty() {
        return Err(errors);
    }
    let mut layouts = Layouts {
        index,
        fields: &fields,
        enums: &enums,
        done: BTreeMap::new(),
        active: BTreeSet::new(),
    };
    for id in fields.keys().chain(enums.keys()) {
        if let Err(message) = layouts.named(id) {
            let s = &layouts.index.symbols[id];
            errors.push(diagnostic(&s.file, s.declaration.span, message));
        }
    }
    for (id, object_fields) in &all_fields {
        if matches!(
            index.symbols[id].declaration.value.kind,
            ItemKind::Object { .. }
        ) {
            for ty in object_fields.values() {
                if let Err(message) = layouts.ty(ty) {
                    let s = &index.symbols[id];
                    errors.push(diagnostic(&s.file, s.declaration.span, message));
                }
            }
        }
    }
    for (id, signature) in &signatures {
        for ty in signature.parameters.iter().chain(signature.returns.iter()) {
            if let Err(message) = layouts.ty(ty) {
                let s = &index.symbols[id];
                errors.push(diagnostic(&s.file, s.declaration.span, message));
            }
        }
    }
    if errors.is_empty() {
        Ok(CheckedTypes {
            layouts: layouts.done,
            signatures,
            fields: all_fields,
        })
    } else {
        Err(errors)
    }
}
