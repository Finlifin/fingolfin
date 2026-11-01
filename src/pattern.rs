//! Side-effect-free reference pattern evaluator. Snapshots must already satisfy
//! the checked model's field types; absence is unknown, not a negative match.
use crate::syntax::{Literal, Match, Node, Pattern, PatternField};
use serde::{Deserialize, Serialize};
use std::collections::{BTreeMap, BTreeSet};

#[derive(Debug, Clone, PartialEq, Eq, PartialOrd, Ord, Serialize, Deserialize)]
pub enum Value {
    Integer(i128),
    Bool(bool),
    String(String),
    Variant(String),
    Record(BTreeMap<String, Value>),
    /// Opaque address; the reference evaluator never dereferences it.
    Pointer(u64),
    Array(Vec<Value>),
    Unknown,
}
pub type Bindings = BTreeMap<String, Value>;
#[derive(Debug, Clone, PartialEq, Eq)]
pub struct Outcome {
    pub matches: Vec<Bindings>,
    /// Additional matching environments may exist but required fields are unknown.
    pub incomplete: bool,
}
#[derive(Debug, Clone, PartialEq, Eq)]
pub enum Error {
    BudgetExceeded,
    InvalidSnapshot(String),
    ConflictingBinding(String),
}
struct Evaluator<'a> {
    budget: &'a mut crate::util::budget::Budget,
}
impl Evaluator<'_> {
    fn step(&mut self) -> Result<(), Error> {
        self.budget.charge(1).map_err(|_| Error::BudgetExceeded)?;
        Ok(())
    }
    fn pattern(
        &mut self,
        p: &Pattern,
        objects: &BTreeMap<String, Value>,
    ) -> Result<Outcome, Error> {
        self.step()?;
        match p {
            Pattern::Object(path, fields) => match objects.get(path).unwrap_or(&Value::Unknown) {
                Value::Unknown => Ok(unknown()),
                Value::Record(record) => self.fields(fields, record),
                _ => Err(Error::InvalidSnapshot(format!(
                    "object `{path}` is not a record"
                ))),
            },
            Pattern::Not(inner) => {
                let result = self.pattern(&inner.value, objects)?;
                if !result.matches.is_empty() {
                    Ok(no_match())
                } else if result.incomplete {
                    Ok(unknown())
                } else {
                    Ok(matched())
                }
            }
            Pattern::Or(branches) => {
                let mut matches = BTreeSet::new();
                let mut incomplete = false;
                for branch in branches {
                    let result = self.pattern(&branch.value, objects)?;
                    matches.extend(result.matches);
                    incomplete |= result.incomplete;
                }
                Ok(Outcome {
                    matches: matches.into_iter().collect(),
                    incomplete,
                })
            }
        }
    }
    fn fields(
        &mut self,
        fields: &[Node<PatternField>],
        record: &BTreeMap<String, Value>,
    ) -> Result<Outcome, Error> {
        let mut result = matched();
        for field in fields {
            self.step()?;
            let field = &field.value;
            let value = record.get(&field.name).unwrap_or(&Value::Unknown);
            let mut next = self.matcher(&field.matcher.value, value)?;
            if let Some(binding) = &field.binding {
                for environment in &mut next.matches {
                    if environment
                        .insert(binding.value.clone(), value.clone())
                        .is_some()
                    {
                        return Err(Error::ConflictingBinding(binding.value.clone()));
                    }
                }
            }
            // In three-valued conjunction, a known false term dominates unknown.
            if next.matches.is_empty() && !next.incomplete {
                return Ok(no_match());
            }
            let incomplete = result.incomplete || next.incomplete;
            let mut environments = Vec::new();
            for left in &result.matches {
                for right in &next.matches {
                    self.step()?;
                    let mut merged = left.clone();
                    for (name, value) in right {
                        if merged.insert(name.clone(), value.clone()).is_some() {
                            return Err(Error::ConflictingBinding(name.clone()));
                        }
                    }
                    environments.push(merged);
                }
            }
            result = Outcome {
                matches: environments,
                incomplete,
            };
        }
        Ok(result)
    }
    fn matcher(&mut self, matcher: &Match, value: &Value) -> Result<Outcome, Error> {
        self.step()?;
        if *value == Value::Unknown {
            return Ok(unknown());
        }
        let yes = match matcher {
            Match::Any => true,
            Match::Fields(fields) => {
                if let Value::Record(record) = value {
                    return self.fields(fields, record);
                }
                return Err(Error::InvalidSnapshot(
                    "nested fields require a record".into(),
                ));
            }
            Match::Range { lo, hi, inclusive } => {
                let Value::Integer(n) = value else {
                    return Err(Error::InvalidSnapshot("range requires an integer".into()));
                };
                n >= lo && if *inclusive { n <= hi } else { n < hi }
            }
            Match::Literal(literal) => match (literal, value) {
                (Literal::Integer(a), Value::Integer(b)) => a == b,
                (Literal::Bool(a), Value::Bool(b)) => a == b,
                (Literal::String(a), Value::String(b)) => a == b,
                (Literal::Variant(a), Value::Variant(b)) => a == b,
                _ => {
                    return Err(Error::InvalidSnapshot(
                        "literal and snapshot field types differ".into(),
                    ));
                }
            },
        };
        Ok(if yes { matched() } else { no_match() })
    }
}
fn matched() -> Outcome {
    Outcome {
        matches: vec![Bindings::new()],
        incomplete: false,
    }
}
fn no_match() -> Outcome {
    Outcome {
        matches: vec![],
        incomplete: false,
    }
}
fn unknown() -> Outcome {
    Outcome {
        matches: vec![],
        incomplete: true,
    }
}
/// OR is a union of binding environments: equal rows execute once, different
/// rows remain distinct. Negation never exports bindings. Budget is per call.
pub fn evaluate(
    pattern: &Node<Pattern>,
    objects: &BTreeMap<String, Value>,
    max_steps: u64,
) -> Result<Outcome, Error> {
    evaluate_with_budget(
        pattern,
        objects,
        &mut crate::util::budget::Budget::new(max_steps),
    )
}
/// Consume the caller's budget, including work performed before an error.
pub fn evaluate_with_budget(
    pattern: &Node<Pattern>,
    objects: &BTreeMap<String, Value>,
    budget: &mut crate::util::budget::Budget,
) -> Result<Outcome, Error> {
    Evaluator { budget }.pattern(&pattern.value, objects)
}
