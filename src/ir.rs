//! Structured typed IR for backend lowering. Local IDs are function-local and
//! never reused across lexical scopes. Logical nodes retain short-circuit order.
use crate::{
    compiler::CheckedProgram,
    syntax::{self, ItemKind, Literal, Node, Span},
    types::Ty,
};
use serde::Serialize;
use std::collections::BTreeMap;
#[derive(Debug, Clone, Serialize)]
pub struct Module {
    pub version: u32,
    pub target: String,
    pub program_source_hash: String,
    pub model_source_hash: String,
    pub functions: BTreeMap<String, Function>,
}
#[derive(Debug, Clone, Serialize)]
pub struct Function {
    pub gate: crate::gate::Plan,
    pub file: String,
    pub hook: Option<String>,
    pub parameters: Vec<u32>,
    pub bindings: BTreeMap<String, u32>,
    pub locals: Vec<Local>,
    pub returns: Option<Ty>,
    /// Unoptimized pattern retained for the reference planner.
    pub pattern: Option<Node<syntax::Pattern>>,
    pub body: Vec<Statement>,
}
#[derive(Debug, Clone, Serialize)]
pub struct Local {
    pub name: String,
    pub ty: Ty,
    pub mutable: bool,
}
#[derive(Debug, Clone, Serialize)]
pub struct Expression {
    pub span: Span,
    pub ty: Option<Ty>,
    pub kind: ExpressionKind,
}
#[derive(Debug, Clone, Serialize)]
pub enum ExpressionKind {
    Helper {
        intrinsic: crate::intrinsic::Intrinsic,
        arguments: Vec<Expression>,
    },
    Constant(Literal),
    Local(u32),
    Field {
        base: Box<Expression>,
        name: String,
        offset: u64,
    },
    Call {
        target: String,
        arguments: Vec<Expression>,
    },
    Unary {
        op: Unary,
        operand: Box<Expression>,
    },
    Binary {
        op: Binary,
        left: Box<Expression>,
        right: Box<Expression>,
    },
    /// Right side is evaluated only when left is true.
    And(Box<Expression>, Box<Expression>),
    /// Right side is evaluated only when left is false.
    Or(Box<Expression>, Box<Expression>),
}
#[derive(Debug, Clone, Copy, PartialEq, Eq, Serialize)]
pub enum Unary {
    Negate,
    Not,
}
#[derive(Debug, Clone, Copy, PartialEq, Eq, Serialize)]
pub enum Binary {
    Add,
    Subtract,
    Multiply,
    Divide,
    Remainder,
    Equal,
    NotEqual,
    Less,
    LessEqual,
    Greater,
    GreaterEqual,
}
#[derive(Debug, Clone, Serialize)]
pub struct Statement {
    pub span: Span,
    pub kind: StatementKind,
}
#[derive(Debug, Clone, Serialize)]
pub enum StatementKind {
    Initialize {
        local: u32,
        value: Expression,
    },
    Assign {
        local: u32,
        value: Expression,
    },
    Return(Option<Expression>),
    Evaluate(Expression),
    If {
        condition: Expression,
        then_body: Vec<Statement>,
        else_body: Vec<Statement>,
    },
}
struct Lower<'a> {
    program: &'a CheckedProgram,
    id: &'a str,
    locals: Vec<Local>,
    scopes: Vec<BTreeMap<String, u32>>,
}
impl Lower<'_> {
    fn declare(&mut self, name: &str, ty: Ty, mutable: bool) -> Result<u32, String> {
        let id = u32::try_from(self.locals.len()).map_err(|_| "too many locals")?;
        if self
            .scopes
            .last_mut()
            .unwrap()
            .insert(name.into(), id)
            .is_some()
        {
            return Err(format!("duplicate local {name}"));
        }
        self.locals.push(Local {
            name: name.into(),
            ty,
            mutable,
        });
        Ok(id)
    }
    fn local(&self, name: &str) -> Result<u32, String> {
        self.scopes
            .iter()
            .rev()
            .find_map(|s| s.get(name))
            .copied()
            .ok_or_else(|| format!("unresolved local {name}"))
    }
    fn expression(&mut self, e: &Node<syntax::Expr>) -> Result<Expression, String> {
        let ty = self
            .program
            .bodies
            .expressions
            .get(self.id)
            .and_then(|m| m.get(&format!("{}:{}", e.span.start, e.span.end)))
            .ok_or("missing checked expression type")?
            .clone();
        use syntax::Expr;
        let kind = match &e.value {
            Expr::Literal(l) => ExpressionKind::Constant(l.clone()),
            Expr::Path(path) => {
                let mut parts = path.split('.');
                let id = self.local(parts.next().unwrap())?;
                let mut base = Expression {
                    span: e.span,
                    ty: Some(self.locals[id as usize].ty.clone()),
                    kind: ExpressionKind::Local(id),
                };
                for name in parts {
                    let Some(Ty::Struct(owner)) = &base.ty else {
                        return Err("field base is not a struct".into());
                    };
                    let field_ty = self.program.types.fields[owner]
                        .get(name)
                        .ok_or("missing field type")?
                        .clone();
                    let offset = *self.program.types.layouts[owner]
                        .fields
                        .get(name)
                        .ok_or("missing field offset")?;
                    base = Expression {
                        span: e.span,
                        ty: Some(field_ty),
                        kind: ExpressionKind::Field {
                            base: Box::new(base),
                            name: name.into(),
                            offset,
                        },
                    };
                }
                if base.ty != ty {
                    return Err("path type differs from checked type".into());
                }
                return Ok(base);
            }
            Expr::Call(path, args) => {
                if let Some(intrinsic) = crate::intrinsic::Intrinsic::lookup(path) {
                    ExpressionKind::Helper {
                        intrinsic,
                        arguments: args
                            .iter()
                            .map(|e| self.expression(e))
                            .collect::<Result<_, _>>()?,
                    }
                } else {
                    let symbol = &self.program.index.symbols[self.id];
                    let target = self.program.index.resolve(&symbol.module, path)?.id.clone();
                    ExpressionKind::Call {
                        target,
                        arguments: args
                            .iter()
                            .map(|e| self.expression(e))
                            .collect::<Result<_, _>>()?,
                    }
                }
            }
            Expr::Unary(op, rhs) => {
                // The checker folds this case to admit the signed minimum literal.
                if op == "-"
                    && let Expr::Literal(Literal::Integer(n)) = rhs.value
                {
                    ExpressionKind::Constant(Literal::Integer(
                        n.checked_neg().ok_or("negation overflow")?,
                    ))
                } else {
                    let op = match op.as_str() {
                        "-" => Unary::Negate,
                        "!" => Unary::Not,
                        _ => return Err(format!("unsupported unary operator {op}")),
                    };
                    ExpressionKind::Unary {
                        op,
                        operand: Box::new(self.expression(rhs)?),
                    }
                }
            }
            Expr::Binary(op, lhs, rhs) => {
                let left = Box::new(self.expression(lhs)?);
                let right = Box::new(self.expression(rhs)?);
                match op.as_str() {
                    "&&" => ExpressionKind::And(left, right),
                    "||" => ExpressionKind::Or(left, right),
                    _ => {
                        let op = match op.as_str() {
                            "+" => Binary::Add,
                            "-" => Binary::Subtract,
                            "*" => Binary::Multiply,
                            "/" => Binary::Divide,
                            "%" => Binary::Remainder,
                            "==" => Binary::Equal,
                            "!=" => Binary::NotEqual,
                            "<" => Binary::Less,
                            "<=" => Binary::LessEqual,
                            ">" => Binary::Greater,
                            ">=" => Binary::GreaterEqual,
                            _ => return Err(format!("unsupported operator {op}")),
                        };
                        ExpressionKind::Binary { op, left, right }
                    }
                }
            }
        };
        Ok(Expression {
            span: e.span,
            ty,
            kind,
        })
    }
    fn block(&mut self, body: &[Node<syntax::Stmt>]) -> Result<Vec<Statement>, String> {
        let mut result = vec![];
        for stmt in body {
            use syntax::Stmt;
            let kind = match &stmt.value {
                Stmt::Variable {
                    name,
                    mutable,
                    value,
                    ..
                } => {
                    // Resolve initializer before introducing the shadowing local.
                    let value = self.expression(value)?;
                    let local =
                        self.declare(name, value.ty.clone().ok_or("void local")?, *mutable)?;
                    StatementKind::Initialize { local, value }
                }
                Stmt::Assign(name, value) => {
                    let local = self.local(name)?;
                    if !self.locals[local as usize].mutable {
                        return Err("assignment to immutable local".into());
                    }
                    StatementKind::Assign {
                        local,
                        value: self.expression(value)?,
                    }
                }
                Stmt::Return(e) => {
                    StatementKind::Return(e.as_ref().map(|e| self.expression(e)).transpose()?)
                }
                Stmt::Expr(e) => StatementKind::Evaluate(self.expression(e)?),
                Stmt::If {
                    condition,
                    then_body,
                    else_body,
                } => {
                    let condition = self.expression(condition)?;
                    self.scopes.push(BTreeMap::new());
                    let then_body = self.block(then_body)?;
                    self.scopes.pop();
                    self.scopes.push(BTreeMap::new());
                    let else_body = self.block(else_body)?;
                    self.scopes.pop();
                    StatementKind::If {
                        condition,
                        then_body,
                        else_body,
                    }
                }
            };
            result.push(Statement {
                span: stmt.span,
                kind,
            });
        }
        Ok(result)
    }
}
pub fn lower(program: &CheckedProgram) -> Result<Module, String> {
    let mut functions = BTreeMap::new();
    for (id, symbol) in &program.index.symbols {
        let ItemKind::Function(f) = &symbol.declaration.value.kind else {
            continue;
        };
        let signature = &program.types.signatures[id];
        let mut lower = Lower {
            program,
            id,
            locals: vec![],
            scopes: vec![BTreeMap::new()],
        };
        let mut parameters = vec![];
        for (parameter, ty) in f.parameters.iter().zip(&signature.parameters) {
            parameters.push(lower.declare(&parameter.value.name, ty.clone(), false)?);
        }
        let mut bindings = BTreeMap::new();
        if let Some(types) = program.bodies.bindings.get(id) {
            for (name, ty) in types {
                bindings.insert(name.clone(), lower.declare(name, ty.clone(), false)?);
            }
        }
        let body = lower.block(&f.body)?;
        functions.insert(
            id.clone(),
            Function {
                gate: crate::gate::compile(
                    program,
                    id,
                    &program
                        .property_availability
                        .iter()
                        .map(|a| {
                            (
                                crate::gate::Property {
                                    object: a.object.clone(),
                                    fields: a.fields.clone(),
                                },
                                a.stage,
                            )
                        })
                        .collect(),
                )?,
                file: symbol.file.clone(),
                hook: f.hook.as_ref().map(|h| h.value.clone()),
                parameters,
                bindings,
                locals: lower.locals,
                returns: signature.returns.clone(),
                pattern: f.pattern.clone(),
                body,
            },
        );
    }
    Ok(Module {
        version: 1,
        target: program.target.clone(),
        program_source_hash: program.program_source_hash.clone(),
        model_source_hash: program.model_source_hash.clone(),
        functions,
    })
}
