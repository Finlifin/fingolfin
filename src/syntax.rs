use pest::{
    Parser,
    iterators::Pair,
    pratt_parser::{Assoc, Op, PrattParser},
};
use pest_derive::Parser;
use serde::{Deserialize, Serialize};

#[derive(Parser)]
#[grammar = "grammar.pest"]
struct Grammar;

#[derive(Debug, Clone, Copy, PartialEq, Eq, Serialize, Deserialize)]
pub struct Span {
    pub start: usize,
    pub end: usize,
}
#[derive(Debug, Clone, PartialEq, Eq, Serialize, Deserialize)]
pub struct Diagnostic {
    pub file: String,
    pub span: Span,
    pub message: String,
}
impl std::fmt::Display for Diagnostic {
    fn fmt(&self, f: &mut std::fmt::Formatter<'_>) -> std::fmt::Result {
        write!(
            f,
            "{}:{}..{}: {}",
            self.file, self.span.start, self.span.end, self.message
        )
    }
}
impl std::error::Error for Diagnostic {}
#[derive(Debug, Clone, PartialEq, Eq, Serialize, Deserialize)]
pub struct Node<T> {
    pub span: Span,
    pub value: T,
}
#[derive(Debug, Clone, PartialEq, Eq, Serialize, Deserialize)]
pub enum Type {
    Named(String),
    Pointer(Box<Node<Type>>),
    Array(Box<Node<Type>>, u64),
}
#[derive(Debug, Clone, PartialEq, Eq, Serialize, Deserialize)]
pub struct Parameter {
    pub name: String,
    pub ty: Node<Type>,
}
#[derive(Debug, Clone, PartialEq, Eq, Serialize, Deserialize)]
pub enum Literal {
    Integer(i128),
    Bool(bool),
    String(String),
    Variant(String),
}
#[derive(Debug, Clone, PartialEq, Eq, Serialize, Deserialize)]
pub enum Expr {
    Literal(Literal),
    Path(String),
    Call(String, Vec<Node<Expr>>),
    Unary(String, Box<Node<Expr>>),
    Binary(String, Box<Node<Expr>>, Box<Node<Expr>>),
}
#[derive(Debug, Clone, PartialEq, Eq, Serialize, Deserialize)]
pub enum Stmt {
    Variable {
        mutable: bool,
        name: String,
        ty: Option<Node<Type>>,
        value: Node<Expr>,
    },
    Assign(String, Node<Expr>),
    Return(Option<Node<Expr>>),
    Expr(Node<Expr>),
    If {
        condition: Node<Expr>,
        then_body: Vec<Node<Stmt>>,
        else_body: Vec<Node<Stmt>>,
    },
}
#[derive(Debug, Clone, PartialEq, Eq, Serialize, Deserialize)]
pub enum Match {
    Fields(Vec<Node<PatternField>>),
    Literal(Literal),
    Range { lo: i128, hi: i128, inclusive: bool },
    Any,
}
#[derive(Debug, Clone, PartialEq, Eq, Serialize, Deserialize)]
pub struct PatternField {
    pub name: String,
    pub matcher: Node<Match>,
    pub binding: Option<Node<String>>,
}
#[derive(Debug, Clone, PartialEq, Eq, Serialize, Deserialize)]
pub enum Pattern {
    Object(String, Vec<Node<PatternField>>),
    Or(Vec<Node<Pattern>>),
    Not(Box<Node<Pattern>>),
}
#[derive(Debug, Clone, PartialEq, Eq, Serialize, Deserialize)]
pub struct Function {
    pub name: String,
    pub hook: Option<Node<String>>,
    pub parameters: Vec<Node<Parameter>>,
    pub returns: Option<Node<Type>>,
    pub pattern: Option<Node<Pattern>>,
    pub body: Vec<Node<Stmt>>,
}
#[derive(Debug, Clone, PartialEq, Eq, Serialize, Deserialize)]
pub enum ItemKind {
    Import {
        path: String,
        members: Vec<String>,
    },
    Function(Function),
    Struct {
        name: String,
        fields: Vec<Node<Parameter>>,
    },
    Enum {
        name: String,
        repr: Node<Type>,
        variants: Vec<Node<(String, i128)>>,
    },
    Hook {
        path: String,
        parameters: Vec<Node<Parameter>>,
    },
    Object {
        path: String,
        fields: Vec<Node<Parameter>>,
    },
}
#[derive(Debug, Clone, PartialEq, Eq, Serialize, Deserialize)]
pub struct Item {
    pub public: bool,
    pub kind: ItemKind,
}
#[derive(Debug, Clone, PartialEq, Eq, Serialize, Deserialize)]
pub struct File {
    pub items: Vec<Node<Item>>,
}

type P<'a> = Pair<'a, Rule>;
fn is_keyword(r: Rule) -> bool {
    matches!(
        r,
        Rule::kw_fn
            | Rule::kw_on
            | Rule::kw_where
            | Rule::kw_use
            | Rule::kw_struct
            | Rule::kw_enum
            | Rule::kw_hook
            | Rule::kw_object
            | Rule::kw_return
            | Rule::kw_if
            | Rule::kw_else
            | Rule::kw_as
            | Rule::kw_not
    )
}
type Result<T> = std::result::Result<T, Diagnostic>;
fn spelling(p: &P<'_>) -> String {
    if p.as_rule() == Rule::path {
        p.clone()
            .into_inner()
            .map(|p| p.as_str())
            .collect::<Vec<_>>()
            .join(".")
    } else {
        p.as_str().to_string()
    }
}
fn span(p: &P<'_>) -> Span {
    Span {
        start: p.as_span().start(),
        end: p.as_span().end(),
    }
}
fn node<T>(p: &P<'_>, value: T) -> Node<T> {
    Node {
        span: span(p),
        value,
    }
}
struct Builder<'a> {
    file: &'a str,
}
impl Builder<'_> {
    fn error(&self, p: &P<'_>, message: &str) -> Diagnostic {
        Diagnostic {
            file: self.file.into(),
            span: span(p),
            message: message.into(),
        }
    }
    fn integer(&self, p: P<'_>) -> Result<i128> {
        let s = p.as_str();
        let (negative, digits) = s.strip_prefix('-').map_or((false, s), |s| (true, s));
        let value = if let Some(hex) = digits.strip_prefix("0x") {
            u128::from_str_radix(hex, 16)
        } else {
            digits.parse::<u128>()
        };
        let value = value.ok().and_then(|v| {
            if negative {
                if v == (1u128 << 127) {
                    Some(i128::MIN)
                } else {
                    i128::try_from(v).ok()?.checked_neg()
                }
            } else {
                i128::try_from(v).ok()
            }
        });
        value.ok_or_else(|| {
            self.error(
                &p,
                "integer literal is outside signed 128-bit parsing range",
            )
        })
    }
    fn ty(&self, p: P<'_>) -> Result<Node<Type>> {
        let inner = p
            .clone()
            .into_inner()
            .find(|p| !is_keyword(p.as_rule()))
            .unwrap();
        let value = match inner.as_rule() {
            Rule::path => Type::Named(spelling(&inner)),
            Rule::pointer_type => Type::Pointer(Box::new(
                self.ty(inner
                    .into_inner()
                    .find(|p| !is_keyword(p.as_rule()))
                    .unwrap())?,
            )),
            Rule::array_type => {
                let mut parts = inner.into_inner().filter(|p| !is_keyword(p.as_rule()));
                let ty = self.ty(parts.next().unwrap())?;
                let length = parts.next().unwrap();
                let n = u64::try_from(self.integer(length.clone())?)
                    .map_err(|_| self.error(&length, "array length must fit u64"))?;
                Type::Array(Box::new(ty), n)
            }
            _ => unreachable!(),
        };
        Ok(node(&p, value))
    }
    fn parameters(&self, p: P<'_>) -> Result<Vec<Node<Parameter>>> {
        p.into_inner()
            .filter(|p| !is_keyword(p.as_rule()))
            .map(|p| {
                let mut parts = p.clone().into_inner().filter(|p| !is_keyword(p.as_rule()));
                let name = spelling(&parts.next().unwrap());
                let ty = self.ty(parts.next().unwrap())?;
                Ok(node(&p, Parameter { name, ty }))
            })
            .collect()
    }
    fn literal(&self, p: P<'_>) -> Result<Literal> {
        let inner = p.into_inner().find(|p| !is_keyword(p.as_rule())).unwrap();
        Ok(match inner.as_rule() {
            Rule::integer => Literal::Integer(self.integer(inner)?),
            Rule::boolean => Literal::Bool(inner.as_str() == "true"),
            Rule::string => {
                Literal::String(serde_json::from_str(inner.as_str()).map_err(|_| {
                    self.error(&inner, "invalid string escape or control character")
                })?)
            }
            Rule::variant_ref => Literal::Variant(
                inner
                    .into_inner()
                    .find(|p| !is_keyword(p.as_rule()))
                    .unwrap()
                    .as_str()
                    .into(),
            ),
            _ => unreachable!(),
        })
    }
    fn expr(&self, p: P<'_>) -> Result<Node<Expr>> {
        use Rule::*;
        let pratt = PrattParser::new()
            .op(Op::infix(logical_or, Assoc::Left))
            .op(Op::infix(logical_and, Assoc::Left))
            .op(Op::infix(equal, Assoc::Left) | Op::infix(not_equal, Assoc::Left))
            .op(Op::infix(less, Assoc::Left)
                | Op::infix(less_equal, Assoc::Left)
                | Op::infix(greater, Assoc::Left)
                | Op::infix(greater_equal, Assoc::Left))
            .op(Op::infix(add, Assoc::Left) | Op::infix(subtract, Assoc::Left))
            .op(Op::infix(multiply, Assoc::Left)
                | Op::infix(divide, Assoc::Left)
                | Op::infix(modulo, Assoc::Left))
            .op(Op::prefix(negate) | Op::prefix(logical_not));
        pratt
            .map_primary(|p| {
                let value = match p.as_rule() {
                    scalar => Expr::Literal(self.literal(p.clone())?),
                    path => Expr::Path(spelling(&p)),
                    expression => return self.expr(p),
                    call => {
                        let mut parts = p.clone().into_inner().filter(|p| !is_keyword(p.as_rule()));
                        let name = spelling(&parts.next().unwrap());
                        let args = parts
                            .next()
                            .map(|a| {
                                a.into_inner()
                                    .filter(|p| !is_keyword(p.as_rule()))
                                    .map(|e| self.expr(e))
                                    .collect::<Result<Vec<_>>>()
                            })
                            .transpose()?
                            .unwrap_or_default();
                        Expr::Call(name, args)
                    }
                    _ => unreachable!(),
                };
                Ok(node(&p, value))
            })
            .map_prefix(|op, rhs: Result<Node<Expr>>| {
                let rhs = rhs?;
                Ok(Node {
                    span: Span {
                        start: span(&op).start,
                        end: rhs.span.end,
                    },
                    value: Expr::Unary(op.as_str().into(), Box::new(rhs)),
                })
            })
            .map_infix(|lhs: Result<Node<Expr>>, op, rhs: Result<Node<Expr>>| {
                let (lhs, rhs) = (lhs?, rhs?);
                Ok(Node {
                    span: Span {
                        start: lhs.span.start,
                        end: rhs.span.end,
                    },
                    value: Expr::Binary(op.as_str().into(), Box::new(lhs), Box::new(rhs)),
                })
            })
            .parse(p.into_inner().filter(|p| !is_keyword(p.as_rule())))
    }
    fn fields(&self, p: P<'_>) -> Result<Vec<Node<PatternField>>> {
        p.into_inner()
            .filter(|p| !is_keyword(p.as_rule()))
            .map(|p| {
                let mut parts = p.clone().into_inner().filter(|p| !is_keyword(p.as_rule()));
                let name = spelling(&parts.next().unwrap());
                let matcher = parts.next().unwrap();
                let mut parts = matcher
                    .clone()
                    .into_inner()
                    .filter(|p| !is_keyword(p.as_rule()));
                let value = parts.next().unwrap();
                let value = match value.as_rule() {
                    Rule::pattern_fields => Match::Fields(self.fields(value)?),
                    Rule::wildcard => Match::Any,
                    Rule::scalar => Match::Literal(self.literal(value)?),
                    Rule::range => {
                        let mut parts = value
                            .clone()
                            .into_inner()
                            .filter(|p| !is_keyword(p.as_rule()));
                        let lo = self.integer(parts.next().unwrap())?;
                        let inclusive = parts.next().unwrap().as_str() == "..=";
                        let hi = self.integer(parts.next().unwrap())?;
                        if lo > hi || (!inclusive && lo == hi) {
                            return Err(self.error(&value, "empty or reversed pattern range"));
                        }
                        Match::Range { lo, hi, inclusive }
                    }
                    _ => unreachable!(),
                };
                let binding = parts.next().map(|b| {
                    let b = b.into_inner().find(|p| !is_keyword(p.as_rule())).unwrap();
                    node(&b, b.as_str().to_string())
                });
                Ok(node(
                    &p,
                    PatternField {
                        name,
                        matcher: node(&matcher, value),
                        binding,
                    },
                ))
            })
            .collect()
    }
    fn pattern(&self, p: P<'_>) -> Result<Node<Pattern>> {
        let value = match p.as_rule() {
            Rule::pattern => {
                let mut patterns = p
                    .clone()
                    .into_inner()
                    .filter(|p| !is_keyword(p.as_rule()))
                    .filter(|p| p.as_rule() != Rule::or_op)
                    .map(|p| self.pattern(p))
                    .collect::<Result<Vec<_>>>()?;
                if patterns.len() == 1 {
                    return Ok(patterns.remove(0));
                }
                Pattern::Or(patterns)
            }
            Rule::pattern_atom => {
                return self.pattern(p.into_inner().find(|p| !is_keyword(p.as_rule())).unwrap());
            }
            Rule::not_pattern => Pattern::Not(Box::new(
                self.pattern(
                    p.clone()
                        .into_inner()
                        .find(|p| !is_keyword(p.as_rule()))
                        .unwrap(),
                )?,
            )),
            Rule::object_pattern => {
                let mut parts = p.clone().into_inner().filter(|p| !is_keyword(p.as_rule()));
                Pattern::Object(
                    spelling(&parts.next().unwrap()),
                    self.fields(parts.next().unwrap())?,
                )
            }
            _ => unreachable!(),
        };
        Ok(node(&p, value))
    }
    fn block(&self, p: P<'_>) -> Result<Vec<Node<Stmt>>> {
        p.into_inner()
            .filter(|p| !is_keyword(p.as_rule()))
            .map(|p| self.stmt(p))
            .collect()
    }
    fn stmt(&self, p: P<'_>) -> Result<Node<Stmt>> {
        let mut parts = p.clone().into_inner().filter(|p| !is_keyword(p.as_rule()));
        let value = match p.as_rule() {
            Rule::variable_decl => {
                let mutable = parts.next().unwrap().as_str() == "var";
                let name = spelling(&parts.next().unwrap());
                let next = parts.next().unwrap();
                let (ty, expr) = if next.as_rule() == Rule::ty {
                    (Some(self.ty(next)?), parts.next().unwrap())
                } else {
                    (None, next)
                };
                Stmt::Variable {
                    mutable,
                    name,
                    ty,
                    value: self.expr(expr)?,
                }
            }
            Rule::assignment => Stmt::Assign(
                spelling(&parts.next().unwrap()),
                self.expr(parts.next().unwrap())?,
            ),
            Rule::return_stmt => Stmt::Return(parts.next().map(|p| self.expr(p)).transpose()?),
            Rule::expr_stmt => Stmt::Expr(self.expr(parts.next().unwrap())?),
            Rule::if_stmt => {
                let condition = self.expr(parts.next().unwrap())?;
                let then_body = self.block(parts.next().unwrap())?;
                let else_body = match parts.next() {
                    None => vec![],
                    Some(p) if p.as_rule() == Rule::block => self.block(p)?,
                    Some(p) => vec![self.stmt(p)?],
                };
                Stmt::If {
                    condition,
                    then_body,
                    else_body,
                }
            }
            _ => unreachable!(),
        };
        Ok(node(&p, value))
    }
    fn item(&self, p: P<'_>) -> Result<Node<Item>> {
        let mut parts = p
            .clone()
            .into_inner()
            .filter(|p| !is_keyword(p.as_rule()))
            .peekable();
        let public = parts
            .peek()
            .is_some_and(|p| p.as_rule() == Rule::visibility);
        if public {
            parts.next();
        }
        let name = spelling(&parts.next().unwrap());
        let kind = match p.as_rule() {
            Rule::import => ItemKind::Import {
                path: name,
                members: parts.map(|p| p.as_str().into()).collect(),
            },
            Rule::struct_decl => ItemKind::Struct {
                name,
                fields: parts
                    .next()
                    .map(|p| self.parameters(p))
                    .transpose()?
                    .unwrap_or_default(),
            },
            Rule::object_decl => ItemKind::Object {
                path: name,
                fields: parts
                    .next()
                    .map(|p| self.parameters(p))
                    .transpose()?
                    .unwrap_or_default(),
            },
            Rule::hook_decl => ItemKind::Hook {
                path: name,
                parameters: parts
                    .next()
                    .map(|p| self.parameters(p))
                    .transpose()?
                    .unwrap_or_default(),
            },
            Rule::enum_decl => {
                let repr = self.ty(parts.next().unwrap())?;
                let variants = parts
                    .next()
                    .unwrap()
                    .into_inner()
                    .filter(|p| !is_keyword(p.as_rule()))
                    .map(|p| {
                        let mut parts = p.clone().into_inner().filter(|p| !is_keyword(p.as_rule()));
                        let name = spelling(&parts.next().unwrap());
                        Ok(node(&p, (name, self.integer(parts.next().unwrap())?)))
                    })
                    .collect::<Result<Vec<_>>>()?;
                ItemKind::Enum {
                    name,
                    repr,
                    variants,
                }
            }
            Rule::function => {
                let mut f = Function {
                    name,
                    hook: None,
                    parameters: vec![],
                    returns: None,
                    pattern: None,
                    body: vec![],
                };
                for part in parts {
                    match part.as_rule() {
                        Rule::on_clause => {
                            let path = part
                                .into_inner()
                                .find(|p| !is_keyword(p.as_rule()))
                                .unwrap();
                            f.hook = Some(node(&path, spelling(&path)));
                        }
                        Rule::parameters => f.parameters = self.parameters(part)?,
                        Rule::return_type => {
                            f.returns = Some(
                                self.ty(part
                                    .into_inner()
                                    .find(|p| !is_keyword(p.as_rule()))
                                    .unwrap())?,
                            )
                        }
                        Rule::where_clause => {
                            f.pattern = Some(
                                self.pattern(
                                    part.into_inner()
                                        .find(|p| !is_keyword(p.as_rule()))
                                        .unwrap(),
                                )?,
                            )
                        }
                        Rule::block => f.body = self.block(part)?,
                        _ => unreachable!(),
                    }
                }
                if f.hook.is_none() && f.pattern.is_some() {
                    return Err(self.error(&p, "where is only allowed on hook entries"));
                }
                if f.hook.is_some() && f.returns.is_some() {
                    return Err(self.error(&p, "hook entries cannot declare a return type"));
                }
                ItemKind::Function(f)
            }
            _ => unreachable!(),
        };
        Ok(node(&p, Item { public, kind }))
    }
}

pub fn parse(file: &str, source: &str) -> Result<File> {
    let mut pairs = Grammar::parse(Rule::file, source).map_err(|error| {
        let span = match error.location {
            pest::error::InputLocation::Pos(p) => Span { start: p, end: p },
            pest::error::InputLocation::Span((start, end)) => Span { start, end },
        };
        Diagnostic {
            file: file.into(),
            span,
            message: error.to_string(),
        }
    })?;
    let builder = Builder { file };
    let items = pairs
        .next()
        .unwrap()
        .into_inner()
        .filter(|p| !is_keyword(p.as_rule()))
        .filter(|p| p.as_rule() != Rule::EOI)
        .map(|p| builder.item(p))
        .collect::<Result<Vec<_>>>()?;
    Ok(File { items })
}
