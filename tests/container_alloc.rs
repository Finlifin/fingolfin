use fingolfin::{
    sdk::ObjectKey,
    state::{Key, Store},
    util::containers::{BoundedMap, BoundedQueue, BoundedVec, Counter, Histogram},
};
use std::{
    alloc::{GlobalAlloc, Layout, System},
    cell::Cell,
};
thread_local! {static ENABLED:Cell<bool>=const{Cell::new(false)};static ALLOCS:Cell<usize>=const{Cell::new(0)};}
fn record() {
    if ENABLED.try_with(Cell::get).unwrap_or(false) {
        let _ = ALLOCS.try_with(|n| n.set(n.get() + 1));
    }
}
struct Counting;
unsafe impl GlobalAlloc for Counting {
    unsafe fn alloc(&self, l: Layout) -> *mut u8 {
        record();
        unsafe { System.alloc(l) }
    }
    unsafe fn alloc_zeroed(&self, l: Layout) -> *mut u8 {
        record();
        unsafe { System.alloc_zeroed(l) }
    }
    unsafe fn realloc(&self, p: *mut u8, l: Layout, n: usize) -> *mut u8 {
        record();
        unsafe { System.realloc(p, l, n) }
    }
    unsafe fn dealloc(&self, p: *mut u8, l: Layout) {
        unsafe { System.dealloc(p, l) }
    }
}
#[global_allocator]
static ALLOCATOR: Counting = Counting;
#[test]
fn fixed_layout_container_operations_do_not_allocate_after_initialization() {
    let mut map = BoundedMap::new(32).unwrap();
    let mut vector = BoundedVec::new(32).unwrap();
    let mut queue = BoundedQueue::new(32).unwrap();
    let mut hist = Histogram::new(vec![0, 10, 100], 4).unwrap();
    let mut store = Store::new(32).unwrap();
    let mut set = fingolfin::util::containers::BoundedSet::new(32).unwrap();
    use fingolfin::intrinsic::{Host, Intrinsic};
    let mut host = Host::with_capacities(1, 1, 1, 1).unwrap();
    host.declare_counter(0, 0).unwrap();
    host.declare_map(0, 32).unwrap();
    host.declare_histogram(0, vec![0, 100], 3).unwrap();
    ALLOCS.with(|n| n.set(0));
    ENABLED.with(|v| v.set(true));
    for i in 0..10000u64 {
        host.invoke_words(Intrinsic::CounterAdd, [0, 1, 0, 0, 0])
            .unwrap();
        host.invoke_words(Intrinsic::MapAdd, [0, i % 32, 1, 0, 0])
            .unwrap();
        host.invoke_words(Intrinsic::HistogramAdd, [0, i, 1, 0, 0])
            .unwrap();
        host.invoke_words(Intrinsic::Emit, [0, i, 0, 0, 0]).unwrap();
        host.pop_output().unwrap();
        let key = i % 32;
        set.insert(key).unwrap();
        if i % 3 == 0 {
            set.remove(&key);
        }
        map.insert(key, i).unwrap();
        *map.get_or_insert_with(key, || 0).unwrap() += 1;
        if i % 3 == 0 {
            map.remove(&key);
        }
        if vector.len() == 32 {
            vector.pop();
        }
        vector.push(i).unwrap();
        queue.push_overwrite(i).unwrap();
        if i % 3 == 0 {
            queue.pop();
        }
        hist.add((i % 200) as i64, 1).unwrap();
        let key = Key {
            program: 1,
            configuration: 1,
            object: ObjectKey {
                run: 1,
                launch: 1,
                slot: i % 32,
                generation: 1,
            },
        };
        store
            .get_or_create(key, Counter::default)
            .unwrap()
            .add(1)
            .unwrap();
        if i % 3 == 0 {
            store.remove(&key);
        }
    }
    map.clear();
    queue.clear();
    vector.clear();
    hist.reset();
    store.remove_program(1);
    ENABLED.with(|v| v.set(false));
    assert_eq!(ALLOCS.with(Cell::get), 0);
}
