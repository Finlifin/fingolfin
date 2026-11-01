//! Function-body checking, independent of execution and ABI code generation.
use crate::{
    resolve::{Index, Symbol},
    syntax::*,
    types::{self, CheckedTypes, Ty},
};
use serde::Serialize;
use std::collections::{BTreeMap, BTreeSet};

#[derive(Debug, Clone, Serialize)]
pub struct CheckedBodies {
    pub calls: BTreeMap<String, BTreeSet<String>>,
    pub bindings: BTreeMap<String, BTreeMap<String, Ty>>,
    pub expressions: BTreeMap<String, BTreeMap<String, Option<Ty>>>,
}
#[derive(Clone)]
struct Local {
    ty: Ty,
    mutable: bool,
}
struct Checker<'a> {
    index: &'a Index,
    types: &'a CheckedTypes,
    symbol: &'a Symbol,
    scopes: Vec<BTreeMap<String, Local>>,
    calls: BTreeSet<String>,
    expressions: BTreeMap<String, Option<Ty>>,
    errors: Vec<Diagnostic>,
}
impl Checker<'_> {
    fn error(&self, span: Span, message: impl Into<String>) -> Diagnostic {
        Diagnostic {
            file: self.symbol.file.clone(),
            span,
            message: message.into(),
        }
    }
    fn local(&self, name: &str) -> Option<&Local> {
        self.scopes.iter().rev().find_map(|scope| scope.get(name))
    }
    fn expression(
        &mut self,
        e: &Node<Expr>,
        expected: Option<&Ty>,
    ) -> Result<Option<Ty>, Diagnostic> {
        let ty = self.expression_inner(e, expected)?;
        self.expressions
            .insert(format!("{}:{}", e.span.start, e.span.end), ty.clone());
        Ok(ty)
    }
    fn expression_inner(
        &mut self,
        e: &Node<Expr>,
        expected: Option<&Ty>,
    ) -> Result<Option<Ty>, Diagnostic> {
        let ty = match &e.value {
            Expr::Literal(Literal::Integer(value)) => {
                let ty = expected.cloned().unwrap_or(Ty::Integer {
                    signed: true,
                    bits: 64,
                });
                let Ty::Integer { signed, bits } = ty else {
                    return Err(self.error(e.span, "integer requires an integer context"));
                };
                if !types::integer_fits(*value, signed, bits) {
                    return Err(self.error(e.span, "integer literal does not fit expected type"));
                }
                Some(ty)
            }
            Expr::Literal(Literal::Bool(_)) => Some(Ty::Bool),
            Expr::Literal(Literal::String(_)) => {
                return Err(self.error(e.span, "strings require a supported output interface"));
            }
            Expr::Literal(Literal::Variant(name)) => {
                let Some(Ty::Enum(id)) = expected else {
                    return Err(self.error(e.span, "enum variant requires an enum context"));
                };
                let ItemKind::Enum { variants, .. } =
                    &self.index.symbols[id].declaration.value.kind
                else {
                    unreachable!()
                };
                if !variants.iter().any(|v| v.value.0 == *name) {
                    return Err(self.error(e.span, format!("unknown enum variant `{name}`")));
                }
                Some(Ty::Enum(id.clone()))
            }
            Expr::Path(path) => {
                let mut names = path.split('.');
                let root = names.next().unwrap();
                let mut ty = self
                    .local(root)
                    .ok_or_else(|| self.error(e.span, format!("unknown variable `{root}`")))?
                    .ty
                    .clone();
                for field in names {
                    let Ty::Struct(id) = &ty else {
                        return Err(self.error(e.span, "field access requires a struct value; implicit pointer dereference is forbidden"));
                    };
                    ty = self.types.fields[id]
                        .get(field)
                        .cloned()
                        .ok_or_else(|| self.error(e.span, format!("unknown field `{field}`")))?;
                }
                Some(ty)
            }
            Expr::Call(path, args) => {
                if self.local(path.split('.').next().unwrap()).is_some() {
                    return Err(self.error(e.span, format!("local `{path}` is not callable")));
                }
                if let Some(intrinsic) = crate::intrinsic::Intrinsic::lookup(path) {
                    let parameters = intrinsic.parameters();
                    if args.len() != parameters.len() {
                        return Err(self.error(e.span, "intrinsic argument count mismatch"));
                    }
                    for (arg, ty) in args.iter().zip(parameters) {
                        self.expression(arg, Some(ty))?;
                    }
                    self.calls.insert(path.clone());
                    None
                } else {
                    let target = self
                        .index
                        .resolve(&self.symbol.module, path)
                        .map_err(|m| self.error(e.span, m))?;
                    let ItemKind::Function(function) = &target.declaration.value.kind else {
                        return Err(self.error(e.span, "call target is not a function"));
                    };
                    if function.hook.is_some() {
                        return Err(self.error(e.span, "hook entries cannot be called as helpers"));
                    }
                    let signature = self.types.signatures[&target.id].clone();
                    self.calls.insert(target.id.clone());
                    if args.len() != signature.parameters.len() {
                        return Err(self.error(e.span, "function argument count mismatch"));
                    }
                    for (arg, expected) in args.iter().zip(&signature.parameters) {
                        self.expression(arg, Some(expected))?;
                    }
                    signature.returns
                }
            }
            Expr::Unary(op, rhs) => {
                if op == "-"
                    && let Expr::Literal(Literal::Integer(value)) = rhs.value
                {
                    let value = value
                        .checked_neg()
                        .ok_or_else(|| self.error(e.span, "integer negation overflow"))?;
                    return self.expression(
                        &Node {
                            span: e.span,
                            value: Expr::Literal(Literal::Integer(value)),
                        },
                        expected,
                    );
                }

                let context = if op == "!" { Some(&Ty::Bool) } else { expected };
                let ty = self
                    .expression(rhs, context)?
                    .ok_or_else(|| self.error(rhs.span, "void expression used as a value"))?;
                if op == "-" && !matches!(ty, Ty::Integer { signed: true, .. }) {
                    return Err(self.error(e.span, "negation requires a signed integer"));
                }
                Some(ty)
            }
            Expr::Binary(op, lhs, rhs) => {
                let logical = op == "&&" || op == "||";
                let comparison = ["==", "!=", "<", "<=", ">", ">="].contains(&op.as_str());
                let context = if logical {
                    Some(Ty::Bool)
                } else if comparison {
                    None
                } else {
                    expected.cloned()
                };
                // Infer from a typed operand when the other side is a literal.
                let left_literal = matches!(lhs.value, Expr::Literal(_));
                let (left, right) = if left_literal
                    && !matches!(rhs.value, Expr::Literal(_))
                    && context.is_none()
                {
                    let right = self
                        .expression(rhs, None)?
                        .ok_or_else(|| self.error(rhs.span, "void operand"))?;
                    let left = self
                        .expression(lhs, Some(&right))?
                        .ok_or_else(|| self.error(lhs.span, "void operand"))?;
                    (left, right)
                } else {
                    let left = self
                        .expression(lhs, context.as_ref())?
                        .ok_or_else(|| self.error(lhs.span, "void operand"))?;
                    let right = self
                        .expression(rhs, Some(&left))?
                        .ok_or_else(|| self.error(rhs.span, "void operand"))?;
                    (left, right)
                };
                if left != right {
                    return Err(self.error(e.span, "binary operand types differ"));
                }
                if logical {
                    Some(Ty::Bool)
                } else if comparison {
                    let equality = op == "==" || op == "!=";
                    if !matches!(left, Ty::Integer { .. })
                        && !(equality && matches!(left, Ty::Bool | Ty::Enum(_)))
                    {
                        return Err(self.error(e.span, "unsupported comparison operands"));
                    }
                    Some(Ty::Bool)
                } else {
                    if !matches!(left, Ty::Integer { .. }) {
                        return Err(self.error(e.span, "arithmetic requires integers"));
                    }
                    if ["/", "%"].contains(&op.as_str())
                        && matches!(rhs.value, Expr::Literal(Literal::Integer(0)))
                    {
                        return Err(self.error(rhs.span, "division by zero"));
                    }
                    Some(left)
                }
            }
        };
        if let Some(expected) = expected
            && ty.as_ref() != Some(expected)
        {
            return Err(self.error(
                e.span,
                format!("type mismatch: expected {expected:?}, got {ty:?}"),
            ));
        }
        Ok(ty)
    }
    fn pattern_fields(
        &mut self,
        fields: &[Node<PatternField>],
        owner: &str,
        object_path: Option<&str>,
    ) -> Result<BTreeMap<String, Ty>, Diagnostic> {
        let mut bindings = BTreeMap::new();
        let mut seen = BTreeSet::new();
        for field in fields {
            let f = &field.value;
            if !seen.insert(&f.name) {
                return Err(self.error(field.span, format!("duplicate pattern field `{}`", f.name)));
            }
            let child_path = object_path.map(|p| format!("{p}.{}", f.name));
            let child = child_path
                .as_ref()
                .and_then(|p| self.index.objects.get(p))
                .cloned();
            let ty = self
                .types
                .fields
                .get(owner)
                .and_then(|m| m.get(&f.name))
                .cloned();
            if child.is_some() && ty.is_some() {
                return Err(self.error(field.span, "ambiguous object relation and field name"));
            }
            let nested = match &f.matcher.value {
                Match::Fields(fields) => {
                    if let Some(child) = child {
                        let declaration = &self.index.symbols[&child];
                        if declaration.module != self.symbol.module
                            && !declaration.declaration.value.public
                        {
                            return Err(self.error(field.span, "pattern object is private"));
                        }
                        if f.binding.is_some() {
                            return Err(self.error(
                                field.span,
                                "object bindings require explicit handle metadata",
                            ));
                        }
                        self.pattern_fields(fields, &child, child_path.as_deref())?
                    } else if let Some(Ty::Struct(id)) = &ty {
                        self.pattern_fields(fields, id, None)?
                    } else {
                        return Err(self.error(
                            field.span,
                            format!("unknown object/struct field `{}`", f.name),
                        ));
                    }
                }
                matcher => {
                    let ty = ty.as_ref().ok_or_else(|| {
                        self.error(field.span, format!("unknown scalar field `{}`", f.name))
                    })?;
                    match matcher {
                        Match::Literal(literal) => {
                            self.expression(
                                &Node {
                                    span: f.matcher.span,
                                    value: Expr::Literal(literal.clone()),
                                },
                                Some(ty),
                            )?;
                        }
                        Match::Range { lo, hi, .. } => {
                            let Ty::Integer { signed, bits } = ty else {
                                return Err(
                                    self.error(field.span, "range requires an integer field")
                                );
                            };
                            if !types::integer_fits(*lo, *signed, *bits)
                                || !types::integer_fits(*hi, *signed, *bits)
                            {
                                return Err(
                                    self.error(field.span, "range bound does not fit field type")
                                );
                            }
                        }
                        Match::Any => {}
                        Match::Fields(_) => unreachable!(),
                    }
                    BTreeMap::new()
                }
            };
            for (name, ty) in nested {
                if bindings.insert(name.clone(), ty).is_some() {
                    return Err(
                        self.error(field.span, format!("duplicate pattern binding `{name}`"))
                    );
                }
            }
            if let Some(binding) = &f.binding {
                let ty =
                    ty.ok_or_else(|| self.error(binding.span, "binding has no fixed-layout type"))?;
                if bindings.insert(binding.value.clone(), ty).is_some() {
                    return Err(self.error(
                        binding.span,
                        format!("duplicate pattern binding `{}`", binding.value),
                    ));
                }
            }
        }
        Ok(bindings)
    }
    fn pattern(
        &mut self,
        pattern: &Node<Pattern>,
        hook_owner: &str,
    ) -> Result<BTreeMap<String, Ty>, Diagnostic> {
        match &pattern.value {
            Pattern::Object(path, fields) => {
                let id = self.index.objects.get(path).cloned().ok_or_else(|| {
                    self.error(pattern.span, format!("unknown model object `{path}`"))
                })?;
                let symbol = &self.index.symbols[&id];
                if symbol.module != self.symbol.module && !symbol.declaration.value.public {
                    return Err(self.error(pattern.span, "pattern object is private"));
                }
                if hook_owner != path && !hook_owner.starts_with(&format!("{path}.")) {
                    return Err(
                        self.error(pattern.span, "pattern anchor is unrelated to hook owner")
                    );
                }
                self.pattern_fields(fields, &id, Some(path))
            }
            Pattern::Not(inner) => {
                self.pattern(inner, hook_owner)?;
                Ok(BTreeMap::new())
            }
            Pattern::Or(branches) => {
                let mut expected = None;
                for branch in branches {
                    let bindings = self.pattern(branch, hook_owner)?;
                    if expected.as_ref().is_some_and(|e| e != &bindings) {
                        return Err(self.error(
                            branch.span,
                            "or branches must export identical binding names and types",
                        ));
                    }
                    expected = Some(bindings);
                }
                Ok(expected.unwrap_or_default())
            }
        }
    }
    fn statement(
        &mut self,
        statement: &Node<Stmt>,
        returns: Option<&Ty>,
    ) -> Result<bool, Diagnostic> {
        match &statement.value {
            Stmt::Variable {
                mutable,
                name,
                ty,
                value,
            } => {
                if self.scopes.last().unwrap().contains_key(name) {
                    return Err(self.error(statement.span, format!("duplicate local `{name}`")));
                }
                let expected = ty
                    .as_ref()
                    .map(|ty| types::resolve_type(self.index, &self.symbol.module, ty))
                    .transpose()
                    .map_err(|m| self.error(statement.span, m))?;
                let value = self
                    .expression(value, expected.as_ref())?
                    .ok_or_else(|| self.error(statement.span, "cannot bind a void value"))?;
                self.scopes.last_mut().unwrap().insert(
                    name.clone(),
                    Local {
                        ty: value,
                        mutable: *mutable,
                    },
                );
            }
            Stmt::Assign(name, value) => {
                if name.contains('.') {
                    return Err(self.error(
                        statement.span,
                        "field and model-memory writes are forbidden",
                    ));
                }
                let local = self.local(name).cloned().ok_or_else(|| {
                    self.error(statement.span, format!("unknown variable `{name}`"))
                })?;
                if !local.mutable {
                    return Err(
                        self.error(statement.span, format!("cannot assign immutable `{name}`"))
                    );
                }
                self.expression(value, Some(&local.ty))?;
            }
            Stmt::Return(value) => {
                match (value, returns) {
                    (Some(value), Some(ty)) => {
                        self.expression(value, Some(ty))?;
                    }
                    (None, None) => {}
                    _ => {
                        return Err(self.error(
                            statement.span,
                            "return value does not match function signature",
                        ));
                    }
                }
                return Ok(true);
            }
            Stmt::Expr(e) => {
                self.expression(e, None)?;
            }
            Stmt::If {
                condition,
                then_body,
                else_body,
            } => {
                self.expression(condition, Some(&Ty::Bool))?;
                let a = self.block(then_body, returns, true);
                let b = self.block(else_body, returns, true);
                return Ok(a && b);
            }
        }
        Ok(false)
    }
    fn block(&mut self, body: &[Node<Stmt>], returns: Option<&Ty>, nested: bool) -> bool {
        if nested {
            self.scopes.push(BTreeMap::new());
        }
        let mut terminated = false;
        for stmt in body {
            if terminated {
                self.errors
                    .push(self.error(stmt.span, "unreachable statement"));
            }
            match self.statement(stmt, returns) {
                Ok(done) => terminated |= done,
                Err(e) => self.errors.push(e),
            }
        }
        if nested {
            self.scopes.pop();
        }
        terminated
    }
}
/// Checks bodies and fixed-layout pattern bindings. Object identity bindings still
/// require future explicit handle metadata and are rejected.
pub fn check(index: &Index, types: &CheckedTypes) -> Result<CheckedBodies, Vec<Diagnostic>> {
    let mut calls = BTreeMap::new();
    let mut pattern_bindings = BTreeMap::new();
    let mut expressions = BTreeMap::new();
    let mut errors = vec![];
    for (id, symbol) in &index.symbols {
        let ItemKind::Function(function) = &symbol.declaration.value.kind else {
            continue;
        };
        let signature = &types.signatures[id];
        let mut checker = Checker {
            index,
            types,
            symbol,
            scopes: vec![BTreeMap::new()],
            calls: BTreeSet::new(),
            expressions: BTreeMap::new(),
            errors: vec![],
        };
        for (p, ty) in function.parameters.iter().zip(&signature.parameters) {
            checker.scopes[0].insert(
                p.value.name.clone(),
                Local {
                    ty: ty.clone(),
                    mutable: false,
                },
            );
        }
        if let Some(pattern) = &function.pattern {
            let owner = function
                .hook
                .as_ref()
                .and_then(|h| h.value.rsplit_once('.'))
                .map(|(owner, _)| owner)
                .unwrap_or("");
            match checker.pattern(pattern, owner) {
                Ok(bindings) => {
                    pattern_bindings.insert(id.clone(), bindings.clone());
                    for (name, ty) in bindings {
                        let conflict = match checker.scopes[0].entry(name.clone()) {
                            std::collections::btree_map::Entry::Vacant(entry) => {
                                entry.insert(Local { ty, mutable: false });
                                false
                            }
                            std::collections::btree_map::Entry::Occupied(_) => true,
                        };
                        if conflict {
                            checker.errors.push(checker.error(
                                pattern.span,
                                format!("pattern binding conflicts with parameter `{name}`"),
                            ));
                        }
                    }
                }
                Err(e) => checker.errors.push(e),
            }
        }
        let terminated = checker.block(&function.body, signature.returns.as_ref(), false);
        if signature.returns.is_some() && !terminated {
            checker
                .errors
                .push(checker.error(symbol.declaration.span, "not all paths return a value"));
        }
        errors.extend(checker.errors);
        calls.insert(id.clone(), checker.calls);
        expressions.insert(id.clone(), checker.expressions);
    }
    // Reachability from each node diagnoses both direct and mutual recursion.
    for (id, edges) in &calls {
        let mut pending: Vec<_> = edges.iter().collect();
        let mut visited = BTreeSet::new();
        while let Some(next) = pending.pop() {
            if next == id {
                let s = &index.symbols[id];
                errors.push(Diagnostic {
                    file: s.file.clone(),
                    span: s.declaration.span,
                    message: format!("recursive function cycle involving `{id}`"),
                });
                break;
            }
            if visited.insert(next)
                && let Some(edges) = calls.get(next)
            {
                pending.extend(edges);
            }
        }
    }
    if errors.is_empty() {
        Ok(CheckedBodies {
            calls,
            expressions,
            bindings: pattern_bindings,
        })
    } else {
        Err(errors)
    }
}
