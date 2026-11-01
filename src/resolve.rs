//! Declaration collection followed by fixed-point import resolution.
use crate::{source::LoadedPackage, syntax::*};
use serde::Serialize;
use std::collections::{BTreeMap, BTreeSet};

#[derive(Debug, Clone, Serialize)]
pub struct Symbol {
    pub id: String,
    pub module: String,
    pub name: String,
    pub file: String,
    pub declaration: Node<Item>,
}
#[derive(Debug, Clone, Serialize)]
pub struct Binding {
    pub symbol: String,
    pub public: bool,
}
#[derive(Debug, Clone, Serialize)]
pub struct Index {
    pub package: String,
    pub packages: BTreeSet<String>,
    pub symbols: BTreeMap<String, Symbol>,
    pub scopes: BTreeMap<String, BTreeMap<String, Binding>>,
    pub hooks: BTreeMap<String, String>,
    pub objects: BTreeMap<String, String>,
    pub dependencies: BTreeMap<String, BTreeSet<String>>,
    #[serde(skip)]
    pending: BTreeSet<(String, String)>,
}
#[derive(Debug, Clone)]
struct Import {
    module: String,
    name: String,
    target: String,
    public: bool,
    file: String,
    span: Span,
}
fn qualify(module: &str, name: &str) -> String {
    if module.is_empty() {
        name.into()
    } else {
        format!("{module}.{name}")
    }
}
fn diag(file: &str, span: Span, message: impl Into<String>) -> Diagnostic {
    Diagnostic {
        file: file.into(),
        span,
        message: message.into(),
    }
}
fn declaration_name(item: &ItemKind) -> Option<&str> {
    match item {
        ItemKind::Import { .. } => None,
        ItemKind::Function(f) => Some(&f.name),
        ItemKind::Struct { name, .. } | ItemKind::Enum { name, .. } => Some(name),
        ItemKind::Hook { path, .. } | ItemKind::Object { path, .. } => path.rsplit('.').next(),
    }
}
impl Index {
    /// Resolve a lexical identifier, local module-relative path, or package path.
    /// A private binding is only accessible from its own module.
    pub fn resolve(&self, module: &str, path: &str) -> Result<&Symbol, String> {
        self.lookup(module, path).map(|(symbol, _)| symbol)
    }
    fn lookup(&self, module: &str, path: &str) -> Result<(&Symbol, String), String> {
        let (context_package, context_module) =
            module.split_once("::").unwrap_or((&self.package, module));
        let explicit = path
            .split_once('.')
            .filter(|(package, _)| self.packages.contains(*package));
        let (package, candidates) = if let Some((package, path)) = explicit {
            (package, vec![path.to_owned()])
        } else if context_module.is_empty() {
            (context_package, vec![path.to_owned()])
        } else {
            (
                context_package,
                vec![qualify(context_module, path), path.to_owned()],
            )
        };
        for candidate in candidates {
            let (owner, name) = candidate.rsplit_once('.').unwrap_or(("", &candidate));
            let owner = if package == self.package {
                owner.to_string()
            } else {
                format!("{package}::{owner}")
            };
            if let Some(binding) = self.scopes.get(&owner).and_then(|scope| scope.get(name)) {
                if owner != module && !binding.public {
                    return Err(format!(
                        "private symbol `{candidate}` is not accessible from `{module}`"
                    ));
                }
                return Ok((&self.symbols[&binding.symbol], owner.to_string()));
            }
            if path.contains('.') && self.pending.contains(&(owner.clone(), name.into())) {
                return Err(format!("pending re-export `{candidate}`"));
            }
        }
        Err(format!("unresolved symbol `{path}` in module `{module}`"))
    }
    /// Transitive reverse dependency closure, including each changed module.
    pub fn affected_modules(&self, changed: &BTreeSet<String>) -> BTreeSet<String> {
        let mut affected = changed.clone();
        loop {
            let before = affected.len();
            for (module, dependencies) in &self.dependencies {
                if dependencies.iter().any(|d| affected.contains(d)) {
                    affected.insert(module.clone());
                }
            }
            if before == affected.len() {
                return affected;
            }
        }
    }
}

pub fn collect(package: &LoadedPackage) -> Result<Index, Vec<Diagnostic>> {
    collect_with_dependencies(package, &[])
}

/// Dependencies must be independently resolved. Their identities remain intact;
/// only module scope keys are qualified to prevent accidental lexical capture.
pub fn collect_with_dependencies(
    package: &LoadedPackage,
    dependencies: &[Index],
) -> Result<Index, Vec<Diagnostic>> {
    let mut index = Index {
        package: package.manifest.package.name.clone(),
        packages: BTreeSet::from([package.manifest.package.name.clone()]),
        symbols: BTreeMap::new(),
        scopes: BTreeMap::new(),
        hooks: BTreeMap::new(),
        objects: BTreeMap::new(),
        dependencies: BTreeMap::new(),
        pending: BTreeSet::new(),
    };
    let mut errors = vec![];
    let mut imports = vec![];
    for dependency in dependencies {
        let dependency_error = |message| diag("Fingolfin.toml", Span { start: 0, end: 0 }, message);
        if dependency.package == index.package || !index.packages.insert(dependency.package.clone())
        {
            errors.push(dependency_error(format!(
                "duplicate dependency package `{}`",
                dependency.package
            )));
            continue;
        }
        if dependency.packages.len() != 1 {
            errors.push(dependency_error(
                "transitive dependency loading requires a package lock graph".into(),
            ));
            continue;
        }
        let module_key = |name: &str| format!("{}::{name}", dependency.package);
        for (id, symbol) in &dependency.symbols {
            let mut symbol = symbol.clone();
            symbol.module = module_key(&symbol.module);
            index.symbols.insert(id.clone(), symbol);
        }
        for (module, scope) in &dependency.scopes {
            index.scopes.insert(module_key(module), scope.clone());
        }
        for (module, edges) in &dependency.dependencies {
            index.dependencies.insert(
                module_key(module),
                edges.iter().map(|name| module_key(name)).collect(),
            );
        }
        for (target, source) in [
            (&mut index.hooks, &dependency.hooks),
            (&mut index.objects, &dependency.objects),
        ] {
            for (path, id) in source {
                if target.insert(path.clone(), id.clone()).is_some() {
                    errors.push(dependency_error(format!(
                        "duplicate dependency model path `{path}`"
                    )));
                }
            }
        }
    }

    for (module_name, module) in &package.modules {
        let scope = index.scopes.entry(module_name.clone()).or_default();
        index.dependencies.entry(module_name.clone()).or_default();
        for item in &module.ast.items {
            if let ItemKind::Import { path, members } = &item.value.kind {
                let targets = if members.is_empty() {
                    vec![path.clone()]
                } else {
                    members.iter().map(|m| qualify(path, m)).collect()
                };
                for target in targets {
                    imports.push(Import {
                        module: module_name.clone(),
                        name: target.rsplit('.').next().unwrap().into(),
                        target,
                        public: item.value.public,
                        file: module.file.clone(),
                        span: item.span,
                    });
                }
                continue;
            }
            let name = declaration_name(&item.value.kind).unwrap().to_string();
            let id = format!("{}::{}", index.package, qualify(module_name, &name));
            if scope.contains_key(&name) {
                errors.push(diag(
                    &module.file,
                    item.span,
                    format!("duplicate declaration `{name}`"),
                ));
                continue;
            }
            scope.insert(
                name.clone(),
                Binding {
                    symbol: id.clone(),
                    public: item.value.public,
                },
            );
            let logical = match &item.value.kind {
                ItemKind::Hook { path, .. } => Some((&mut index.hooks, path)),
                ItemKind::Object { path, .. } => Some((&mut index.objects, path)),
                _ => None,
            };
            if let Some((map, path)) = logical
                && map.insert(path.clone(), id.clone()).is_some()
            {
                errors.push(diag(
                    &module.file,
                    item.span,
                    format!("duplicate logical model path `{path}`"),
                ));
            }
            index.symbols.insert(
                id.clone(),
                Symbol {
                    id,
                    module: module_name.clone(),
                    name,
                    file: module.file.clone(),
                    declaration: item.clone(),
                },
            );
        }
    }
    // Reserve aliases before solving them, so duplicate unresolved imports cannot
    // disappear merely because a later target happens to resolve first.
    let mut reserved: BTreeSet<(String, String)> = BTreeSet::new();
    imports.retain(|i| {
        let duplicate = index.scopes[&i.module].contains_key(&i.name)
            || !reserved.insert((i.module.clone(), i.name.clone()));
        if duplicate {
            errors.push(diag(
                &i.file,
                i.span,
                format!("duplicate import binding `{}`", i.name),
            ));
        }
        !duplicate
    });
    index.pending = reserved;
    loop {
        let mut pending = vec![];
        let mut progress = false;
        for import in imports {
            match index.lookup(&import.module, &import.target) {
                Ok((symbol, exporter)) => {
                    let id = symbol.id.clone();
                    let owner = symbol.module.clone();
                    index
                        .pending
                        .remove(&(import.module.clone(), import.name.clone()));
                    // Import aliases do not create new type identities.
                    index.scopes.get_mut(&import.module).unwrap().insert(
                        import.name,
                        Binding {
                            symbol: id,
                            public: import.public,
                        },
                    );
                    index
                        .dependencies
                        .get_mut(&import.module)
                        .unwrap()
                        .insert(owner);
                    // Track the actual alias owner, including root re-exports.
                    index
                        .dependencies
                        .get_mut(&import.module)
                        .unwrap()
                        .insert(exporter);
                    progress = true;
                }
                Err(_) => pending.push(import),
            }
        }
        if pending.is_empty() {
            break;
        }
        if !progress {
            for import in pending {
                let reason = index.resolve(&import.module, &import.target).unwrap_err();
                errors.push(diag(&import.file, import.span, format!("{reason}; import may be missing, private, or part of an ungrounded re-export cycle")));
            }
            break;
        }
        imports = pending;
    }
    if errors.is_empty() {
        Ok(index)
    } else {
        Err(errors)
    }
}
