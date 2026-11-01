use fingolfin::{
    sdk::ObjectKey,
    state::{Key, Store},
    util::containers::{BoundedMap, BoundedQueue, BoundedVec, Counter, Error, Histogram},
};
use std::collections::BTreeMap;
#[test]
fn bounded_map_matches_ordered_reference_under_updates_and_capacity_pressure() {
    let mut map = BoundedMap::new(16).unwrap();
    let mut reference = BTreeMap::new();
    let mut random = 7u64;
    for n in 0..5000 {
        random = random.wrapping_mul(6364136223846793005).wrapping_add(1);
        let key = (random >> 32) % 40;
        if n % 3 == 0 {
            assert_eq!(map.remove(&key), reference.remove(&key));
        } else if reference.contains_key(&key) || reference.len() < 16 {
            assert_eq!(map.insert(key, n).unwrap(), reference.insert(key, n));
        } else {
            let rejected = map.insert(key, n).unwrap_err();
            assert_eq!(rejected.reason, Error::Capacity { limit: 16 });
            assert_eq!(rejected.value, (key, n));
        }
        assert_eq!(
            map.iter().collect::<Vec<_>>(),
            reference.iter().collect::<Vec<_>>()
        );
    }
    map.clear();
    assert!(map.is_empty());
    assert_eq!(map.capacity(), 16);
}
#[test]
fn full_containers_preserve_rejected_values_and_skip_initialization() {
    let mut zero = BoundedMap::<u64, u64>::new(0).unwrap();
    assert!(
        zero.get_or_insert_with(1, || panic!("factory ran without capacity"))
            .is_err()
    );
    let mut map = BoundedMap::new(1).unwrap();
    map.insert(1, 2).unwrap();
    assert_eq!(
        *map.get_or_insert_with(1, || panic!("factory ran for existing key"))
            .unwrap(),
        2
    );
    assert_eq!(map.insert(1, 3).unwrap(), Some(2));
    let mut vector = BoundedVec::new(1).unwrap();
    vector.push(7).unwrap();
    assert_eq!(vector.push(8).unwrap_err().value, 8);
    assert_eq!(vector.as_slice(), &[7]);
    assert_eq!(vector.pop(), Some(7));
    vector.push(8).unwrap();
    assert!(BoundedVec::<u64>::new(usize::MAX).is_err());
}
#[test]
fn queue_retention_is_explicit_and_wraparound_preserves_fifo_order() {
    let mut q = BoundedQueue::new(3).unwrap();
    for i in 0..3 {
        q.push(i).unwrap();
    }
    assert_eq!(q.push(3).unwrap_err().value, 3);
    for i in 3..1000 {
        assert_eq!(q.push_overwrite(i).unwrap(), Some(i - 3));
        assert_eq!(q.len(), 3);
    }
    assert_eq!(q.iter().copied().collect::<Vec<_>>(), vec![997, 998, 999]);
    for i in 997..1000 {
        assert_eq!(q.pop(), Some(i));
    }
    assert!(q.is_empty());
    let mut empty = BoundedQueue::new(0).unwrap();
    assert_eq!(empty.push_overwrite(1).unwrap_err().value, 1);
}
#[test]
fn counters_and_histograms_reject_overflow_transactionally() {
    let mut counter = Counter::new(u64::MAX - 1);
    counter.add(1).unwrap();
    assert_eq!(counter.add(1), Err(Error::Overflow));
    assert_eq!(counter.value(), u64::MAX);
    assert_eq!(counter.reset(), u64::MAX);
    let mut h = Histogram::new(vec![0, 10], 3).unwrap();
    for (sample, bucket) in [
        (i64::MIN, 0),
        (-1, 0),
        (0, 1),
        (9, 1),
        (10, 2),
        (i64::MAX, 2),
    ] {
        assert_eq!(h.add(sample, 1).unwrap(), bucket);
    }
    assert_eq!(h.counts(), &[2, 2, 2]);
    assert_eq!(h.total(), 6);
    h.reset();
    h.add(-1, u64::MAX).unwrap();
    assert_eq!(h.add(1, 1), Err(Error::Overflow));
    assert_eq!(h.counts(), &[u64::MAX, 0, 0]);
    assert_eq!(h.total(), u64::MAX);
    assert!(matches!(
        Histogram::new(vec![1, 1], 3),
        Err(Error::InvalidBounds)
    ));
    assert!(matches!(
        Histogram::new(vec![2, 1], 3),
        Err(Error::InvalidBounds)
    ));
    assert!(matches!(
        Histogram::new(vec![0], 1),
        Err(Error::Capacity { limit: 1 })
    ));
    let mut single = Histogram::new(vec![], 1).unwrap();
    single.add(i64::MAX, 2).unwrap();
    assert_eq!(single.counts(), &[2]);
}
fn key(program: u64, configuration: u64, generation: u64) -> Key {
    Key {
        program,
        configuration,
        object: ObjectKey {
            run: 1,
            launch: 2,
            slot: 3,
            generation,
        },
    }
}
#[test]
fn scoped_state_separates_program_configuration_and_reused_object_generations() {
    let mut store = Store::new(4).unwrap();
    for (i, k) in [key(1, 1, 1), key(1, 2, 1), key(2, 1, 1), key(1, 1, 2)]
        .into_iter()
        .enumerate()
    {
        *store.get_or_create(k, || 0).unwrap() = i;
    }
    assert_eq!(store.get(&key(1, 1, 1)), Some(&0));
    assert_eq!(store.get(&key(1, 1, 2)), Some(&3));
    assert_eq!(store.remove_configuration(1, 1), 2);
    assert_eq!(store.remove_program(2), 1);
    assert_eq!(store.remove_object(key(1, 2, 1).object), 1);
    assert!(store.is_empty());
    let mut store = Store::new(4).unwrap();
    for generation in 1..=10000 {
        let k = key(1, 1, generation);
        assert_eq!(
            *store.get_or_create(k, Counter::default).unwrap(),
            Counter::default()
        );
        store.get_mut(&k).unwrap().add(1).unwrap();
        assert_eq!(store.remove_launch(1, 2), 1);
    }
    assert!(store.is_empty());
}

#[test]
fn bounded_set_handles_duplicates_at_capacity_without_eviction() {
    let mut set = fingolfin::util::containers::BoundedSet::new(2).unwrap();
    assert!(set.insert(2).unwrap());
    assert!(set.insert(1).unwrap());
    assert!(!set.insert(2).unwrap());
    assert_eq!(set.insert(3).unwrap_err().value, 3);
    assert_eq!(set.iter().copied().collect::<Vec<_>>(), vec![1, 2]);
    assert!(set.remove(&1));
    assert!(set.insert(3).unwrap());
    assert!(set.contains(&2));
}
