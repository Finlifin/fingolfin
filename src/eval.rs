//! Budgeted reference execution of checked ordinary functions. No native pointer
//! dereferences or host side effects. Hook dispatch accepts typed snapshots.
use crate::{
    compiler::CheckedProgram,
    pattern::Value,
    syntax::{Expr, ItemKind, Literal, Node, Stmt},
    types::{self, Ty},
    util::budget::Budget,
};
use std::collections::BTreeMap;
#[derive(Debug, Clone, PartialEq, Eq)]
pub enum Error {
    BudgetExceeded,
    DepthExceeded,
    InvalidValue(String),
    Arithmetic,
    Unsupported(String),
    Host(crate::intrinsic::HostError),
}
type Result<T> = std::result::Result<T, Error>;
#[derive(Debug, Clone, PartialEq, Eq, serde::Serialize)]
pub struct Invocation {
    pub entry: String,
    pub bindings: BTreeMap<String, Value>,
    pub result: Option<Value>,
}
#[derive(Debug, Clone, PartialEq, Eq, serde::Serialize)]
pub struct Dispatch {
    pub invocations: Vec<Invocation>,
    pub incomplete: bool,
}
type Scopes = Vec<BTreeMap<String, Value>>;
pub struct Evaluator<'a> {
    program: &'a CheckedProgram,
    budget: Budget,
    max_depth: usize,
    host: Option<crate::intrinsic::Host>,
}
impl<'a> Evaluator<'a> {
    pub fn new(program: &'a CheckedProgram, steps: u64, max_depth: usize) -> Self {
        Self {
            program,
            budget: Budget::new(steps),
            max_depth,
            host: None,
        }
    }
    pub fn set_host(&mut self, host: crate::intrinsic::Host) {
        self.host = Some(host);
    }
    pub fn host_mut(&mut self) -> Option<&mut crate::intrinsic::Host> {
        self.host.as_mut()
    }
    pub fn consumed(&self) -> u64 {
        self.budget.consumed()
    }
    fn step(&mut self) -> Result<()> {
        self.budget.charge(1).map_err(|_| Error::BudgetExceeded)
    }
    fn validate(&mut self, value: &Value, ty: &Ty) -> Result<()> {
        self.step()?;
        let valid = match (value, ty) {
            (Value::Integer(n), Ty::Integer { signed, bits }) => {
                types::integer_fits(*n, *signed, *bits)
            }
            (Value::Bool(_), Ty::Bool) => true,
            (Value::Pointer(_), Ty::Pointer(_)) => true,
            (Value::Array(values), Ty::Array(inner, count)) => {
                if values.len() as u64 != *count {
                    false
                } else {
                    for value in values {
                        self.validate(value, inner)?;
                    }
                    true
                }
            }
            (Value::Variant(name), Ty::Enum(id)) => {
                matches!(&self.program.index.symbols[id].declaration.value.kind, ItemKind::Enum { variants, .. } if variants.iter().any(|v| v.value.0 == *name))
            }
            (Value::Record(record), Ty::Struct(id)) => {
                let fields = &self.program.types.fields[id];
                if fields.len() != record.len() {
                    false
                } else {
                    for (name, ty) in fields {
                        self.validate(
                            record
                                .get(name)
                                .ok_or_else(|| Error::InvalidValue(name.clone()))?,
                            ty,
                        )?;
                    }
                    true
                }
            }
            _ => false,
        };
        if valid {
            Ok(())
        } else {
            Err(Error::InvalidValue(format!("expected {ty:?}")))
        }
    }
    pub fn call(&mut self, id: &str, args: Vec<Value>) -> Result<Option<Value>> {
        self.invoke(id, args, 0)
    }
    fn invoke(&mut self, id: &str, args: Vec<Value>, depth: usize) -> Result<Option<Value>> {
        self.execute(id, args, depth, None)
    }
    fn execute(
        &mut self,
        id: &str,
        args: Vec<Value>,
        depth: usize,
        bindings: Option<BTreeMap<String, Value>>,
    ) -> Result<Option<Value>> {
        self.step()?;
        if depth >= self.max_depth {
            return Err(Error::DepthExceeded);
        }
        let symbol = self
            .program
            .index
            .symbols
            .get(id)
            .ok_or_else(|| Error::Unsupported(id.into()))?;
        let ItemKind::Function(f) = &symbol.declaration.value.kind else {
            return Err(Error::Unsupported(id.into()));
        };
        if bindings.is_none() && (f.hook.is_some() || f.pattern.is_some()) {
            return Err(Error::Unsupported(
                "hook dispatch requires a snapshot".into(),
            ));
        }
        let sig = &self.program.types.signatures[id];
        if args.len() != sig.parameters.len() {
            return Err(Error::InvalidValue("argument count".into()));
        }
        for (value, ty) in args.iter().zip(&sig.parameters) {
            self.validate(value, ty)?;
        }
        let mut scopes: Scopes = vec![
            f.parameters
                .iter()
                .zip(args)
                .map(|(p, v)| (p.value.name.clone(), v))
                .collect(),
        ];
        if let Some(bindings) = bindings {
            for (name, value) in bindings {
                if scopes[0].insert(name.clone(), value).is_some() {
                    return Err(Error::InvalidValue(format!("conflicting binding {name}")));
                }
            }
        }
        let result = self.block(id, &f.body, &mut scopes, depth)?.flatten();
        match (&result, &sig.returns) {
            (Some(v), Some(t)) => self.validate(v, t)?,
            (None, None) => {}
            _ => return Err(Error::InvalidValue("return type".into())),
        }
        Ok(result)
    }
    /// Snapshot keys are canonical object paths. Missing fields and Unknown are
    /// incomplete evidence; malformed known fields are errors, never a nonmatch.
    pub fn dispatch(
        &mut self,
        hook: &str,
        args: Vec<Value>,
        objects: &BTreeMap<String, Value>,
    ) -> Result<Dispatch> {
        self.step()?;
        let hook_id = self
            .program
            .index
            .hooks
            .get(hook)
            .ok_or_else(|| Error::Unsupported(format!("unknown hook {hook}")))?;
        let signature = &self.program.types.signatures[hook_id];
        if args.len() != signature.parameters.len() {
            return Err(Error::InvalidValue("argument count".into()));
        }
        for (value, ty) in args.iter().zip(&signature.parameters) {
            self.validate(value, ty)?;
        }
        for (path, value) in objects {
            let id = self
                .program
                .index
                .objects
                .get(path)
                .ok_or_else(|| Error::InvalidValue(format!("unknown object {path}")))?;
            self.snapshot_record(value, id, Some(path))?;
        }

        let mut result = Dispatch {
            invocations: vec![],
            incomplete: false,
        };
        for id in &self.program.entries {
            self.step()?;
            let ItemKind::Function(f) = &self.program.index.symbols[id].declaration.value.kind
            else {
                continue;
            };
            if f.hook.as_ref().map(|h| h.value.as_str()) != Some(hook) {
                continue;
            }
            let outcome = if let Some(pattern) = &f.pattern {
                crate::pattern::evaluate_with_budget(pattern, objects, &mut self.budget).map_err(
                    |error| match error {
                        crate::pattern::Error::BudgetExceeded => Error::BudgetExceeded,
                        other => Error::InvalidValue(format!("pattern: {other:?}")),
                    },
                )?
            } else {
                crate::pattern::Outcome {
                    matches: vec![BTreeMap::new()],
                    incomplete: false,
                }
            };
            result.incomplete |= outcome.incomplete;
            for bindings in outcome.matches {
                let value = self.execute(id, args.clone(), 0, Some(bindings.clone()))?;
                result.invocations.push(Invocation {
                    entry: id.clone(),
                    bindings,
                    result: value,
                });
            }
        }
        Ok(result)
    }
    fn snapshot_record(&mut self, value: &Value, id: &str, path: Option<&str>) -> Result<()> {
        self.step()?;
        if *value == Value::Unknown {
            return Ok(());
        }
        let Value::Record(record) = value else {
            return Err(Error::InvalidValue("snapshot record expected".into()));
        };
        for (name, value) in record {
            self.step()?;
            let child_path = path.map(|p| format!("{p}.{name}"));
            let child = child_path
                .as_ref()
                .and_then(|p| self.program.index.objects.get(p));
            let ty = self
                .program
                .types
                .fields
                .get(id)
                .and_then(|fields| fields.get(name));
            match (child, ty) {
                (Some(child), None) => self.snapshot_record(value, child, child_path.as_deref())?,
                (None, Some(Ty::Struct(id))) => self.snapshot_record(value, id, None)?,
                (None, Some(ty)) => {
                    if *value != Value::Unknown {
                        self.validate(value, ty)?;
                    }
                }
                _ => {
                    return Err(Error::InvalidValue(format!(
                        "unknown or ambiguous snapshot field {name}"
                    )));
                }
            }
        }
        Ok(())
    }
    fn block(
        &mut self,
        id: &str,
        body: &[Node<Stmt>],
        scopes: &mut Scopes,
        depth: usize,
    ) -> Result<Option<Option<Value>>> {
        for stmt in body {
            self.step()?;
            match &stmt.value {
                Stmt::Variable { name, value, .. } => {
                    let v = self.value(id, value, scopes, depth)?;
                    scopes.last_mut().unwrap().insert(name.clone(), v);
                }
                Stmt::Assign(name, expr) => {
                    let v = self.value(id, expr, scopes, depth)?;
                    let slot = scopes
                        .iter_mut()
                        .rev()
                        .find_map(|s| s.get_mut(name))
                        .ok_or_else(|| Error::InvalidValue(name.clone()))?;
                    *slot = v;
                }
                Stmt::Return(expr) => {
                    return Ok(Some(
                        expr.as_ref()
                            .map(|e| self.value(id, e, scopes, depth))
                            .transpose()?,
                    ));
                }
                Stmt::Expr(expr) => {
                    self.expression(id, expr, scopes, depth)?;
                }
                Stmt::If {
                    condition,
                    then_body,
                    else_body,
                } => {
                    let Value::Bool(yes) = self.value(id, condition, scopes, depth)? else {
                        return Err(Error::InvalidValue("condition".into()));
                    };
                    scopes.push(BTreeMap::new());
                    let result =
                        self.block(id, if yes { then_body } else { else_body }, scopes, depth);
                    scopes.pop();
                    if let Some(value) = result? {
                        return Ok(Some(value));
                    }
                }
            }
        }
        Ok(None)
    }
    fn value(
        &mut self,
        id: &str,
        e: &Node<Expr>,
        scopes: &mut Scopes,
        depth: usize,
    ) -> Result<Value> {
        self.expression(id, e, scopes, depth)?
            .ok_or_else(|| Error::InvalidValue("void value".into()))
    }
    fn expression(
        &mut self,
        id: &str,
        e: &Node<Expr>,
        scopes: &mut Scopes,
        depth: usize,
    ) -> Result<Option<Value>> {
        self.step()?;
        let value = match &e.value {
            Expr::Literal(l) => match l {
                Literal::Integer(n) => Value::Integer(*n),
                Literal::Bool(b) => Value::Bool(*b),
                Literal::Variant(v) => Value::Variant(v.clone()),
                Literal::String(_) => return Err(Error::Unsupported("string".into())),
            },
            Expr::Path(path) => {
                let mut parts = path.split('.');
                let root = parts.next().unwrap();
                let mut v = scopes
                    .iter()
                    .rev()
                    .find_map(|s| s.get(root))
                    .ok_or_else(|| Error::InvalidValue(path.clone()))?;
                for field in parts {
                    let Value::Record(fields) = v else {
                        return Err(Error::InvalidValue(path.clone()));
                    };
                    v = fields
                        .get(field)
                        .ok_or_else(|| Error::InvalidValue(path.clone()))?;
                }
                v.clone()
            }
            Expr::Call(path, args) => {
                let mut values = vec![];
                for arg in args {
                    values.push(self.value(id, arg, scopes, depth)?);
                }
                if let Some(intrinsic) = crate::intrinsic::Intrinsic::lookup(path) {
                    self.host
                        .as_mut()
                        .ok_or_else(|| Error::Unsupported("intrinsic host is not attached".into()))?
                        .invoke_values(intrinsic, &values)
                        .map_err(Error::Host)?;
                    return Ok(None);
                }
                let target = self
                    .program
                    .index
                    .resolve(&self.program.index.symbols[id].module, path)
                    .map_err(Error::Unsupported)?;

                return self.invoke(&target.id, values, depth + 1);
            }
            Expr::Unary(op, rhs) => {
                if op == "-"
                    && let Expr::Literal(Literal::Integer(n)) = rhs.value
                {
                    Value::Integer(n.checked_neg().ok_or(Error::Arithmetic)?)
                } else {
                    match (op.as_str(), self.value(id, rhs, scopes, depth)?) {
                        ("-", Value::Integer(n)) => {
                            Value::Integer(n.checked_neg().ok_or(Error::Arithmetic)?)
                        }
                        ("!", Value::Bool(b)) => Value::Bool(!b),
                        _ => return Err(Error::Unsupported(op.clone())),
                    }
                }
            }
            Expr::Binary(op, lhs, rhs) => {
                let left = self.value(id, lhs, scopes, depth)?;
                if (op == "&&" && left == Value::Bool(false))
                    || (op == "||" && left == Value::Bool(true))
                {
                    left
                } else {
                    let right = self.value(id, rhs, scopes, depth)?;
                    binary(op, left, right)?
                }
            }
        };
        let ty = self
            .program
            .bodies
            .expressions
            .get(id)
            .and_then(|m| m.get(&format!("{}:{}", e.span.start, e.span.end)))
            .and_then(Option::as_ref)
            .ok_or_else(|| Error::InvalidValue("missing checked expression type".into()))?;
        self.validate(&value, ty)?;
        Ok(Some(value))
    }
}
fn binary(op: &str, a: Value, b: Value) -> Result<Value> {
    if op == "==" {
        return Ok(Value::Bool(a == b));
    }
    if op == "!=" {
        return Ok(Value::Bool(a != b));
    }
    match (a, b) {
        (Value::Bool(a), Value::Bool(b)) => match op {
            "&&" => Ok(Value::Bool(a && b)),
            "||" => Ok(Value::Bool(a || b)),
            _ => Err(Error::Unsupported(op.into())),
        },
        (Value::Integer(a), Value::Integer(b)) => {
            let n = match op {
                "+" => a.checked_add(b),
                "-" => a.checked_sub(b),
                "*" => a.checked_mul(b),
                "/" => a.checked_div(b),
                "%" => a.checked_rem(b),
                "<" => return Ok(Value::Bool(a < b)),
                "<=" => return Ok(Value::Bool(a <= b)),
                ">" => return Ok(Value::Bool(a > b)),
                ">=" => return Ok(Value::Bool(a >= b)),
                _ => return Err(Error::Unsupported(op.into())),
            };
            Ok(Value::Integer(n.ok_or(Error::Arithmetic)?))
        }
        _ => Err(Error::InvalidValue("binary operands".into())),
    }
}
