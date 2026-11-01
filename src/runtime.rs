//! Per-instance immutable route publication with Arc-pinned invocation leases.
//! The short RwLock is local to a port; no user code runs while it is held.
//! This is a correctness baseline, not a lock-free/epoch implementation.
use crate::{
    gate::{Decision, Facts, Plan, Stage, Stamp},
    sdk::ObjectKey,
    util::budget::Budget,
};
use std::{
    collections::BTreeSet,
    sync::{
        Arc, RwLock, Weak,
        atomic::{AtomicUsize, Ordering},
    },
};
pub struct Invocation<F> {
    pub stamp: Stamp,
    pub frame: F,
}
pub type Callback<F> = dyn Fn(&Invocation<F>) -> Result<(), String> + Send + Sync + 'static;
pub struct Subscriber<F> {
    pub id: u64,
    pub gate: Plan,
    pub facts: Facts,
    pub callback: Arc<Callback<F>>,
}
struct Route<F> {
    stamp: Stamp,
    closed: bool,
    subscribers: Vec<Subscriber<F>>,
}
pub struct Port<F> {
    object: ObjectKey,
    active: Arc<AtomicUsize>,
    max_subscribers: usize,
    current: RwLock<Arc<Route<F>>>,
}
pub struct Lease<F> {
    route: Option<Arc<Route<F>>>,
    active: Option<Arc<AtomicUsize>>,
}
pub struct Retired<F> {
    pub generation: u64,
    route: Weak<Route<F>>,
}
impl<F> Retired<F> {
    /// True after all old leases are gone. Shared callback owners can outlive
    /// this route; code resources belong in the callback's Arc-owned closure.
    pub fn is_drained(&self) -> bool {
        self.route.strong_count() == 0
    }
}
pub struct Publication<F> {
    pub generation: u64,
    pub retired: Retired<F>,
}
#[derive(Debug, Clone, PartialEq, Eq)]
pub enum PublishError {
    Conflict { expected: u64, actual: u64 },
    Closed,
    GenerationOverflow,
    TooManySubscribers,
    DuplicateSubscriber(u64),
}
#[derive(Debug)]
pub struct SubscriberResult {
    pub id: u64,
    pub gate: Decision,
    pub execution: Option<Result<(), String>>,
}
#[derive(Debug)]
pub struct Observation {
    pub stamp: Stamp,
    pub frame_built: bool,
    pub subscribers: Vec<SubscriberResult>,
}
impl<F> Port<F> {
    pub fn new(object: ObjectKey, max_subscribers: usize) -> Self {
        Self {
            object,
            active: Arc::new(AtomicUsize::new(0)),
            max_subscribers,
            current: RwLock::new(Arc::new(Route {
                stamp: Stamp {
                    configuration: 0,
                    object,
                },
                closed: false,
                subscribers: vec![],
            })),
        }
    }
    pub fn acquire(&self) -> Lease<F> {
        // Poison recovery is valid: the critical section only swaps fully built
        // immutable routes. All callbacks, allocations and drops occur outside.
        let route = self.current.read().unwrap_or_else(|e| e.into_inner());
        let active = if route.closed {
            None
        } else {
            self.active.fetch_add(1, Ordering::AcqRel);
            Some(Arc::clone(&self.active))
        };
        Lease {
            route: Some(Arc::clone(&route)),
            active,
        }
    }
    /// Covers every generation, including leases older than the last publication.
    pub fn is_closed_and_drained(&self) -> bool {
        let route = self.current.read().unwrap_or_else(|e| e.into_inner());
        route.closed && self.active.load(Ordering::Acquire) == 0
    }

    pub fn publish(
        &self,
        expected: u64,
        subscribers: Vec<Subscriber<F>>,
    ) -> Result<Publication<F>, PublishError> {
        self.replace(expected, subscribers, false)
    }
    /// Prevent future publications. The host must wait for retired leases before
    /// freeing any model memory that old callbacks may access. Use
    /// is_closed_and_drained(), not only the last retired generation.
    pub fn close(&self, expected: u64) -> Result<Publication<F>, PublishError> {
        self.replace(expected, vec![], true)
    }
    fn replace(
        &self,
        expected: u64,
        subscribers: Vec<Subscriber<F>>,
        closed: bool,
    ) -> Result<Publication<F>, PublishError> {
        if subscribers.len() > self.max_subscribers {
            return Err(PublishError::TooManySubscribers);
        }
        let mut ids = BTreeSet::new();
        for s in &subscribers {
            if !ids.insert(s.id) {
                return Err(PublishError::DuplicateSubscriber(s.id));
            }
        }
        let generation = expected
            .checked_add(1)
            .ok_or(PublishError::GenerationOverflow)?;
        let next = Arc::new(Route {
            stamp: Stamp {
                configuration: generation,
                object: self.object,
            },
            closed,
            subscribers,
        });
        let mut guard = self.current.write().unwrap_or_else(|e| e.into_inner());
        if guard.closed {
            return Err(PublishError::Closed);
        }
        if guard.stamp.configuration != expected {
            return Err(PublishError::Conflict {
                expected,
                actual: guard.stamp.configuration,
            });
        }
        let old = std::mem::replace(&mut *guard, next);
        drop(guard);
        let retired = Retired {
            generation: old.stamp.configuration,
            route: Arc::downgrade(&old),
        };
        drop(old);
        Ok(Publication {
            generation,
            retired,
        })
    }
}
impl<F> Drop for Lease<F> {
    fn drop(&mut self) {
        drop(self.route.take());
        if let Some(active) = &self.active {
            active.fetch_sub(1, Ordering::AcqRel);
        }
    }
}
impl<F> Lease<F> {
    fn route(&self) -> &Route<F> {
        self.route.as_ref().unwrap()
    }
    pub fn stamp(&self) -> Stamp {
        self.route().stamp
    }
    pub fn is_closed(&self) -> bool {
        self.route().closed
    }
    /// Build one shared frame only if at least one subscriber may match.
    /// This lease pins configuration, facts and callbacks through all execution.
    pub fn observe(
        &self,
        stage: Stage,
        budget: &mut Budget,
        build: impl FnOnce(Stamp) -> F,
    ) -> Observation {
        let stamp = self.route().stamp;
        let mut results: Vec<_> = self
            .route()
            .subscribers
            .iter()
            .map(|s| SubscriberResult {
                id: s.id,
                gate: s.gate.evaluate(&s.facts, stamp, stage, budget),
                execution: None,
            })
            .collect();
        let frame_built = results.iter().any(|r| r.gate.allow);
        if frame_built {
            let invocation = Invocation {
                stamp,
                frame: build(stamp),
            };
            for (subscriber, result) in self.route().subscribers.iter().zip(&mut results) {
                if result.gate.allow {
                    result.execution = Some((subscriber.callback)(&invocation));
                }
            }
        }
        Observation {
            stamp,
            frame_built,
            subscribers: results,
        }
    }
}
