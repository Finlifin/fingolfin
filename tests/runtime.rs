use fingolfin::{
    gate::{Facts, Plan, Predicate, Stage, Stamp},
    runtime::{Callback, Port, PublishError, Subscriber},
    sdk::ObjectKey,
    util::budget::Budget,
};
use std::{
    collections::BTreeMap,
    sync::{
        Arc, Barrier,
        atomic::{AtomicUsize, Ordering},
    },
};
fn object() -> ObjectKey {
    ObjectKey {
        run: 1,
        launch: 2,
        slot: 3,
        generation: 4,
    }
}
fn subscriber(
    id: u64,
    generation: u64,
    allow: bool,
    callback: Arc<Callback<u64>>,
) -> Subscriber<u64> {
    Subscriber {
        id,
        gate: Plan {
            version: 1,
            reads: vec![],
            predicate: if allow {
                Predicate::True
            } else {
                Predicate::Not(Box::new(Predicate::True))
            },
        },
        facts: Facts::new(
            Stamp {
                configuration: generation,
                object: object(),
            },
            BTreeMap::new(),
        ),
        callback,
    }
}
#[test]
fn disabled_route_never_builds_frame_and_mixed_route_builds_once() {
    let port = Port::new(object(), 4);
    assert!(
        !port
            .acquire()
            .observe(Stage::Invocation, &mut Budget::new(100), |_| panic!(
                "empty route built frame"
            ))
            .frame_built
    );
    port.publish(
        0,
        vec![subscriber(
            1,
            1,
            false,
            Arc::new(|_| panic!("rejected subscriber ran")),
        )],
    )
    .unwrap();
    assert!(
        !port
            .acquire()
            .observe(Stage::Invocation, &mut Budget::new(100), |_| panic!(
                "false gate built frame"
            ))
            .frame_built
    );
    let calls = Arc::new(AtomicUsize::new(0));
    let counter = calls.clone();
    port.publish(
        1,
        vec![
            subscriber(1, 2, false, Arc::new(|_| panic!())),
            subscriber(
                2,
                2,
                true,
                Arc::new(move |i| {
                    assert_eq!(i.frame, 7);
                    counter.fetch_add(1, Ordering::Relaxed);
                    Ok(())
                }),
            ),
            subscriber(3, 2, true, Arc::new(|_| Err("visible failure".into()))),
        ],
    )
    .unwrap();
    let mut built = 0;
    let result = port
        .acquire()
        .observe(Stage::Invocation, &mut Budget::new(100), |_| {
            built += 1;
            7
        });
    assert_eq!(built, 1);
    assert_eq!(calls.load(Ordering::Relaxed), 1);
    assert!(result.subscribers[0].execution.is_none());
    assert_eq!(
        result.subscribers[2].execution,
        Some(Err("visible failure".into()))
    );
}
struct Track(Arc<AtomicUsize>);
impl Drop for Track {
    fn drop(&mut self) {
        self.0.fetch_add(1, Ordering::SeqCst);
    }
}
#[test]
fn retirement_waits_for_old_code_and_close_waits_for_all_generations() {
    let port = Port::new(object(), 1);
    let drops = Arc::new(AtomicUsize::new(0));
    let tracked = Track(drops.clone());
    port.publish(
        0,
        vec![subscriber(
            1,
            1,
            true,
            Arc::new(move |_| {
                let _ = &tracked;
                Ok(())
            }),
        )],
    )
    .unwrap();
    let old = port.acquire();
    let update = port.publish(1, vec![]).unwrap();
    assert!(!update.retired.is_drained());
    assert_eq!(drops.load(Ordering::SeqCst), 0);
    let close = port.close(2).unwrap();
    assert!(close.retired.is_drained()); // Last route drained, older generation still lives.
    assert!(!port.is_closed_and_drained());
    assert!(
        old.observe(Stage::Invocation, &mut Budget::new(100), |s| s
            .configuration)
            .frame_built
    );
    drop(old);
    assert!(update.retired.is_drained());
    assert!(port.is_closed_and_drained());
    assert_eq!(drops.load(Ordering::SeqCst), 1);
    assert!(matches!(port.publish(3, vec![]), Err(PublishError::Closed)));
    assert!(
        !port
            .acquire()
            .observe(Stage::Invocation, &mut Budget::new(100), |_| panic!())
            .frame_built
    );
}
#[test]
fn callback_can_reenter_and_publish_without_deadlock_or_generation_mixing() {
    let port = Arc::new(Port::new(object(), 1));
    let weak = Arc::downgrade(&port);
    port.publish(
        0,
        vec![subscriber(
            1,
            1,
            true,
            Arc::new(move |i| {
                assert_eq!(i.stamp.configuration, 1);
                let port = weak.upgrade().unwrap();
                port.publish(1, vec![]).unwrap();
                assert_eq!(i.frame, 1);
                Ok(())
            }),
        )],
    )
    .unwrap();
    let old = port.acquire();
    let result = old.observe(Stage::Invocation, &mut Budget::new(100), |s| {
        s.configuration
    });
    assert_eq!(result.stamp.configuration, 1);
    assert_eq!(port.acquire().stamp().configuration, 2);
}
#[test]
fn duplicate_capacity_and_concurrent_update_conflicts_are_reported() {
    let port = Arc::new(Port::new(object(), 2));
    let sub = || subscriber(1, 1, true, Arc::new(|_| Ok(())));
    assert!(matches!(
        port.publish(0, vec![sub(), sub()]),
        Err(PublishError::DuplicateSubscriber(1))
    ));
    assert!(matches!(
        port.publish(0, vec![sub(), sub(), sub()]),
        Err(PublishError::TooManySubscribers)
    ));
    let barrier = Arc::new(Barrier::new(3));
    let threads: Vec<_> = (0..2)
        .map(|_| {
            let p = port.clone();
            let b = barrier.clone();
            std::thread::spawn(move || {
                b.wait();
                p.publish(0, vec![]).map(|p| p.generation)
            })
        })
        .collect();
    barrier.wait();
    let results: Vec<_> = threads.into_iter().map(|t| t.join().unwrap()).collect();
    assert_eq!(results.iter().filter(|r| r.is_ok()).count(), 1);
    assert_eq!(
        results
            .iter()
            .filter(|r| matches!(
                r,
                Err(PublishError::Conflict {
                    expected: 0,
                    actual: 1
                })
            ))
            .count(),
        1
    );
}
#[test]
fn ten_thousand_publications_with_four_concurrent_readers_preserve_snapshot_identity() {
    let port = Arc::new(Port::new(object(), 1));
    let barrier = Arc::new(Barrier::new(5));
    let calls = Arc::new(AtomicUsize::new(0));
    // Start subscribed so every reader invocation exercises a callback.
    let make = |generation| {
        let calls = calls.clone();
        subscriber(
            1,
            generation,
            true,
            Arc::new(move |i| {
                assert_eq!(i.stamp.configuration, generation);
                assert_eq!(i.frame, generation);
                assert_eq!(i.stamp.object, object());
                calls.fetch_add(1, Ordering::Relaxed);
                Ok(())
            }),
        )
    };
    port.publish(0, vec![make(1)]).unwrap();
    let readers: Vec<_> = (0..4)
        .map(|_| {
            let p = port.clone();
            let b = barrier.clone();
            std::thread::spawn(move || {
                b.wait();
                for _ in 0..10000 {
                    let lease = p.acquire();
                    let r = lease.observe(Stage::Invocation, &mut Budget::new(100), |s| {
                        s.configuration
                    });
                    assert!(r.frame_built);
                    assert!(r.subscribers[0].execution.as_ref().unwrap().is_ok());
                }
            })
        })
        .collect();
    barrier.wait();
    for expected in 1..=10000 {
        port.publish(expected, vec![make(expected + 1)]).unwrap();
    }
    for reader in readers {
        reader.join().unwrap();
    }
    assert_eq!(calls.load(Ordering::Relaxed), 40000);
    port.close(10001).unwrap();
    assert!(port.is_closed_and_drained());
}
