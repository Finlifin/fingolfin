//! Conservative staged predicates: Reject is permitted only on known false.
//! Availability is a model contract, not inferred from field spelling.
use crate::{
    compiler::CheckedProgram,
    pattern::Value,
    sdk::ObjectKey,
    syntax::{ItemKind, Literal, Match, Node, Pattern, PatternField},
    util::budget::Budget,
};
use serde::{Deserialize, Serialize};
use std::collections::{BTreeMap, BTreeSet};
#[derive(Debug, Clone, Copy, PartialEq, Eq, PartialOrd, Ord, Serialize, Deserialize)]
pub enum Stage {
    Attachment,
    Object,
    Update,
    Invocation,
}
#[derive(Debug, Clone, PartialEq, Eq, PartialOrd, Ord, Serialize, Deserialize)]
pub struct Property {
    pub object: String,
    pub fields: Vec<String>,
}
#[derive(Debug, Clone, Serialize, Deserialize)]
pub struct Read {
    pub property: Property,
    pub stage: Stage,
}
#[derive(Debug, Clone, Serialize, Deserialize)]
pub enum Test {
    Record,
    Known,
    Literal(Literal),
    Range { lo: i128, hi: i128, inclusive: bool },
}
#[derive(Debug, Clone, Serialize, Deserialize)]
pub enum Predicate {
    True,
    Test { read: usize, test: Test },
    And(Vec<Predicate>),
    Or(Vec<Predicate>),
    Not(Box<Predicate>),
}
#[derive(Debug, Clone, Serialize, Deserialize)]
pub struct Plan {
    pub version: u32,
    pub reads: Vec<Read>,
    pub predicate: Predicate,
}
#[derive(Debug, Clone, Copy, PartialEq, Eq)]
pub struct Stamp {
    pub configuration: u64,
    pub object: ObjectKey,
}
/// Facts are valid only under their exact configuration and object identity.
/// Invalidation must be delivered before publishing a property change.
pub struct Facts {
    stamp: Stamp,
    objects: BTreeMap<String, Value>,
    invalid: BTreeSet<Property>,
}
impl Facts {
    pub fn new(stamp: Stamp, objects: BTreeMap<String, Value>) -> Self {
        Self {
            stamp,
            objects,
            invalid: BTreeSet::new(),
        }
    }
    pub fn invalidate(&mut self, property: Property) {
        self.invalid.insert(property);
    }
    fn get(&self, property: &Property, budget: &mut Budget) -> Result<Option<&Value>, Reason> {
        if self
            .invalid
            .iter()
            .any(|p| p.object == property.object && property.fields.starts_with(&p.fields))
        {
            return Ok(None);
        }
        let Some(mut value) = self.objects.get(&property.object) else {
            return Ok(None);
        };
        for field in &property.fields {
            budget.charge(1).map_err(|_| Reason::BudgetExceeded)?;
            if *value == Value::Unknown {
                return Ok(None);
            }
            let Value::Record(fields) = value else {
                return Err(Reason::InvalidFacts);
            };
            let Some(next) = fields.get(field) else {
                return Ok(None);
            };
            value = next;
        }
        Ok((value != &Value::Unknown).then_some(value))
    }
}
#[derive(Debug, Clone, Copy, PartialEq, Eq, Serialize)]
pub enum Reason {
    KnownFalse,
    KnownTrue,
    Unknown,
    StaleIdentity,
    BudgetExceeded,
    InvalidFacts,
    InvalidPlan,
}
#[derive(Debug, Clone, Copy, PartialEq, Eq, Serialize)]
pub struct Decision {
    pub allow: bool,
    pub reason: Reason,
}
#[derive(Clone, Copy, PartialEq, Eq)]
enum Truth {
    True,
    False,
    Unknown,
}
struct Builder<'a> {
    availability: &'a BTreeMap<Property, Stage>,
    reads: Vec<Read>,
    ids: BTreeMap<Property, usize>,
}
impl Builder<'_> {
    fn test(&mut self, property: Property, test: Test) -> Predicate {
        let read = if let Some(id) = self.ids.get(&property) {
            *id
        } else {
            let id = self.reads.len();
            self.reads.push(Read {
                stage: self
                    .availability
                    .get(&property)
                    .copied()
                    .unwrap_or(Stage::Invocation),
                property: property.clone(),
            });
            self.ids.insert(property, id);
            id
        };
        Predicate::Test { read, test }
    }
    fn fields(&mut self, path: Property, fields: &[Node<PatternField>]) -> Predicate {
        let mut nodes = vec![self.test(path.clone(), Test::Record)];
        for field in fields {
            let mut child = path.clone();
            child.fields.push(field.value.name.clone());
            nodes.push(match &field.value.matcher.value {
                Match::Fields(fields) => self.fields(child, fields),
                Match::Any => self.test(child, Test::Known),
                Match::Literal(l) => self.test(child, Test::Literal(l.clone())),
                Match::Range { lo, hi, inclusive } => self.test(
                    child,
                    Test::Range {
                        lo: *lo,
                        hi: *hi,
                        inclusive: *inclusive,
                    },
                ),
            });
        }
        Predicate::And(nodes)
    }
    fn pattern(&mut self, p: &Pattern) -> Predicate {
        match p {
            Pattern::Object(object, fields) => self.fields(
                Property {
                    object: object.clone(),
                    fields: vec![],
                },
                fields,
            ),
            Pattern::Or(branches) => {
                Predicate::Or(branches.iter().map(|p| self.pattern(&p.value)).collect())
            }
            Pattern::Not(inner) => Predicate::Not(Box::new(self.pattern(&inner.value))),
        }
    }
}
pub fn compile(
    program: &CheckedProgram,
    id: &str,
    availability: &BTreeMap<Property, Stage>,
) -> Result<Plan, String> {
    let symbol = program
        .index
        .symbols
        .get(id)
        .ok_or("unknown gate function")?;
    let ItemKind::Function(f) = &symbol.declaration.value.kind else {
        return Err("gate requires a function".into());
    };
    let mut builder = Builder {
        availability,
        reads: vec![],
        ids: BTreeMap::new(),
    };
    let predicate = f
        .pattern
        .as_ref()
        .map(|p| builder.pattern(&p.value))
        .unwrap_or(Predicate::True);
    Ok(Plan {
        version: 1,
        reads: builder.reads,
        predicate,
    })
}
impl Plan {
    pub fn evaluate(
        &self,
        facts: &Facts,
        current: Stamp,
        stage: Stage,
        budget: &mut Budget,
    ) -> Decision {
        if self.version != 1 {
            return Decision {
                allow: true,
                reason: Reason::InvalidPlan,
            };
        }
        if facts.stamp != current {
            return Decision {
                allow: true,
                reason: Reason::StaleIdentity,
            };
        }
        match self.eval(&self.predicate, facts, stage, budget) {
            Ok(Truth::False) => Decision {
                allow: false,
                reason: Reason::KnownFalse,
            },
            Ok(Truth::True) => Decision {
                allow: true,
                reason: Reason::KnownTrue,
            },
            Ok(Truth::Unknown) => Decision {
                allow: true,
                reason: Reason::Unknown,
            },
            Err(reason) => Decision {
                allow: true,
                reason,
            },
        }
    }
    fn eval(
        &self,
        p: &Predicate,
        facts: &Facts,
        stage: Stage,
        budget: &mut Budget,
    ) -> Result<Truth, Reason> {
        budget.charge(1).map_err(|_| Reason::BudgetExceeded)?;
        match p {
            Predicate::True => Ok(Truth::True),
            Predicate::Test { read, test } => {
                let read = self.reads.get(*read).ok_or(Reason::InvalidPlan)?;
                if read.stage > stage {
                    return Ok(Truth::Unknown);
                }
                let Some(value) = facts.get(&read.property, budget)? else {
                    return Ok(Truth::Unknown);
                };
                let yes = match (test, value) {
                    (Test::Record, Value::Record(_)) => true,
                    (Test::Known, _) => true,
                    (Test::Literal(Literal::Integer(a)), Value::Integer(b)) => a == b,
                    (Test::Literal(Literal::Bool(a)), Value::Bool(b)) => a == b,
                    (Test::Literal(Literal::Variant(a)), Value::Variant(b)) => a == b,
                    (Test::Literal(Literal::String(a)), Value::String(b)) => a == b,
                    (Test::Range { lo, hi, inclusive }, Value::Integer(n)) => {
                        n >= lo && if *inclusive { n <= hi } else { n < hi }
                    }
                    _ => return Err(Reason::InvalidFacts),
                };
                Ok(if yes { Truth::True } else { Truth::False })
            }
            Predicate::Not(inner) => Ok(match self.eval(inner, facts, stage, budget)? {
                Truth::True => Truth::False,
                Truth::False => Truth::True,
                Truth::Unknown => Truth::Unknown,
            }),
            Predicate::And(nodes) | Predicate::Or(nodes) => {
                let and = matches!(p, Predicate::And(_));
                let mut unknown = false;
                for node in nodes {
                    let value = self.eval(node, facts, stage, budget)?;
                    if (and && value == Truth::False) || (!and && value == Truth::True) {
                        return Ok(value);
                    }
                    unknown |= value == Truth::Unknown;
                }
                Ok(if unknown {
                    Truth::Unknown
                } else if and {
                    Truth::True
                } else {
                    Truth::False
                })
            }
        }
    }
}

/// Subscriber decisions remain independent. A shared observation point closes
/// only when every subscriber is known ineligible; an empty route closes.
#[derive(Debug, Clone, Serialize)]
pub struct RouteDecision {
    pub allow: bool,
    pub subscribers: Vec<Decision>,
}
pub fn route(
    subscribers: &[(&Plan, &Facts, Stamp)],
    stage: Stage,
    budget: &mut Budget,
) -> RouteDecision {
    let subscribers: Vec<_> = subscribers
        .iter()
        .map(|(plan, facts, stamp)| plan.evaluate(facts, *stamp, stage, budget))
        .collect();
    RouteDecision {
        allow: subscribers.iter().any(|s| s.allow),
        subscribers,
    }
}

/// Stable C gate topology v1. Predicate descriptors retain their exact property,
/// stage and test; native model code supplies the read-only predicate evaluator.
#[derive(Debug, Clone, Serialize)]
pub struct CTest {
    pub read_index: usize,
    pub read: Read,
    pub test: Test,
}
#[derive(Debug, Clone, Copy, Serialize)]
pub struct CNode {
    pub op: u32,
    pub left: u32,
    pub right: u32,
    pub test: u32,
    pub stage: u32,
}
#[derive(Debug, Clone, Serialize)]
pub struct CPlan {
    pub version: u32,
    pub nodes: Vec<CNode>,
    pub tests: Vec<CTest>,
}
/// Lower n-ary predicates into bounded binary postorder nodes understood by
/// runtime/include/fingolfin/gate.h. No predicate is discarded or guessed.
pub fn lower_c(plan: &Plan) -> Result<CPlan, String> {
    fn push(out: &mut CPlan, node: CNode) -> Result<u32, String> {
        if out.nodes.len() >= 1024 {
            return Err("C gate exceeds 1024 nodes".into());
        }
        let index = out.nodes.len() as u32;
        out.nodes.push(node);
        Ok(index)
    }
    fn lower(p: &Predicate, plan: &Plan, out: &mut CPlan, depth: usize) -> Result<u32, String> {
        if depth > 128 {
            return Err("C gate source nesting exceeds 128".into());
        }
        let node = CNode {
            op: 0,
            left: 0,
            right: 0,
            test: 0,
            stage: 0,
        };
        match p {
            Predicate::True => push(out, node),
            Predicate::Test { read, test } => {
                let read_index = *read;
                let read = plan
                    .reads
                    .get(read_index)
                    .ok_or("C gate read index invalid")?;
                let index = out.tests.len() as u32;
                let stage = match read.stage {
                    Stage::Attachment => 0,
                    Stage::Object => 1,
                    Stage::Update => 2,
                    Stage::Invocation => 3,
                };
                let result = push(
                    out,
                    CNode {
                        op: 1,
                        test: index,
                        stage,
                        ..node
                    },
                )?;
                out.tests.push(CTest {
                    read_index,
                    read: read.clone(),
                    test: test.clone(),
                });
                Ok(result)
            }
            Predicate::Not(child) => {
                let left = lower(child, plan, out, depth + 1)?;
                push(
                    out,
                    CNode {
                        op: 4,
                        left,
                        ..node
                    },
                )
            }
            Predicate::And(children) | Predicate::Or(children) => {
                let is_and = matches!(p, Predicate::And(_));
                let Some(first) = children.first() else {
                    let value = push(out, node)?;
                    return if is_and {
                        Ok(value)
                    } else {
                        push(
                            out,
                            CNode {
                                op: 4,
                                left: value,
                                ..node
                            },
                        )
                    };
                };
                let mut left = lower(first, plan, out, depth + 1)?;
                for child in &children[1..] {
                    let right = lower(child, plan, out, depth + 1)?;
                    left = push(
                        out,
                        CNode {
                            op: if is_and { 2 } else { 3 },
                            left,
                            right,
                            ..node
                        },
                    )?;
                }
                Ok(left)
            }
        }
    }
    if plan.version != 1 {
        return Err("unsupported gate Plan version".into());
    }
    let mut out = CPlan {
        version: 1,
        nodes: Vec::new(),
        tests: Vec::new(),
    };
    lower(&plan.predicate, plan, &mut out, 0)?;
    Ok(out)
}
