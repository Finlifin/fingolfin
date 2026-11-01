//! Run with `cargo run --example bounded_state`. State is allocated before
//! publication; the callback only updates fixed-layout values in that storage.
use fingolfin::{
    gate::{Facts, Plan, Predicate, Stage, Stamp},
    runtime::{Port, Subscriber},
    sdk::ObjectKey,
    state::{Key, Store},
    util::{
        budget::Budget,
        containers::{BoundedQueue, Counter, Histogram},
    },
};
use std::{
    collections::BTreeMap,
    sync::{Arc, Mutex},
};
struct Stats {
    bytes: Counter,
    sizes: Histogram,
    evidence: BoundedQueue<u64>,
    evicted: Counter,
}
fn main() {
    let object = ObjectKey {
        run: 1,
        launch: 1,
        slot: 0,
        generation: 1,
    };
    let key = Key {
        program: 7,
        configuration: 1,
        object,
    };
    let mut store = Store::new(1).unwrap();
    store
        .get_or_create(key, || Stats {
            bytes: Counter::default(),
            sizes: Histogram::new(vec![128, 256], 3).unwrap(),
            evidence: BoundedQueue::new(2).unwrap(),
            evicted: Counter::default(),
        })
        .unwrap();
    let state = Arc::new(Mutex::new(store));
    let callback_state = state.clone();
    let port = Port::<u64>::new(object, 1);
    port.publish(
        0,
        vec![Subscriber {
            id: 7,
            gate: Plan {
                version: 1,
                reads: vec![],
                predicate: Predicate::True,
            },
            facts: Facts::new(
                Stamp {
                    configuration: 1,
                    object,
                },
                BTreeMap::new(),
            ),
            callback: Arc::new(move |call| {
                let mut store = callback_state.lock().map_err(|e| e.to_string())?;
                let key = Key {
                    program: 7,
                    configuration: call.stamp.configuration,
                    object: call.stamp.object,
                };
                let stats = store
                    .get_mut(&key)
                    .ok_or("state not initialized for this generation")?;
                stats
                    .bytes
                    .add(call.frame)
                    .map_err(|e| format!("bytes: {e:?}"))?;
                stats
                    .sizes
                    .add(i64::try_from(call.frame).map_err(|e| e.to_string())?, 1)
                    .map_err(|e| format!("histogram: {e:?}"))?;
                if stats
                    .evidence
                    .push_overwrite(call.frame)
                    .map_err(|e| format!("evidence: {:?}", e.reason))?
                    .is_some()
                {
                    stats
                        .evicted
                        .add(1)
                        .map_err(|e| format!("evictions: {e:?}"))?;
                }
                Ok(())
            }),
        }],
    )
    .unwrap();
    for size in [64, 128, 256, 64] {
        let result = port
            .acquire()
            .observe(Stage::Invocation, &mut Budget::new(100), |_| size);
        result.subscribers[0]
            .execution
            .as_ref()
            .unwrap()
            .as_ref()
            .unwrap();
    }
    port.close(1).unwrap();
    assert!(port.is_closed_and_drained());
    let mut store = state.lock().unwrap();
    let stats = store.get(&key).unwrap();
    println!(
        "{}",
        serde_json::json!({"bytes":stats.bytes.value(),"size_buckets":stats.sizes.counts(),"evidence":stats.evidence.iter().copied().collect::<Vec<_>>(),"evicted":stats.evicted.value()})
    );
    assert_eq!(store.remove_program(7), 1);
}
