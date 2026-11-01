//! Preallocated, capacity-bounded host containers. Operations do not grow backing
//! storage after construction. Heap allocations inside caller-owned K/V/T are
//! outside this guarantee; runtime state should use fixed-layout values.
use std::collections::VecDeque;
#[derive(Debug, Clone, Copy, PartialEq, Eq)]
pub enum Error {
    Capacity { limit: usize },
    Allocation,
    Overflow,
    InvalidBounds,
}
#[derive(Debug, PartialEq, Eq)]
pub struct Rejected<T> {
    pub reason: Error,
    pub value: T,
}
pub struct BoundedVec<T> {
    values: Vec<T>,
    limit: usize,
}
impl<T> BoundedVec<T> {
    pub fn new(limit: usize) -> Result<Self, Error> {
        let mut values = Vec::new();
        values
            .try_reserve_exact(limit)
            .map_err(|_| Error::Allocation)?;
        Ok(Self { values, limit })
    }
    pub fn push(&mut self, value: T) -> Result<(), Rejected<T>> {
        if self.values.len() == self.limit {
            return Err(Rejected {
                reason: Error::Capacity { limit: self.limit },
                value,
            });
        }
        self.values.push(value);
        Ok(())
    }
    pub fn pop(&mut self) -> Option<T> {
        self.values.pop()
    }
    pub fn as_slice(&self) -> &[T] {
        &self.values
    }
    pub fn len(&self) -> usize {
        self.values.len()
    }
    pub fn is_empty(&self) -> bool {
        self.values.is_empty()
    }
    pub fn capacity(&self) -> usize {
        self.limit
    }
    pub fn clear(&mut self) {
        self.values.clear();
    }
}
/// Deterministic sorted-vector map: O(log n) lookup, O(n) insert/remove, no
/// allocation per insertion. Keys never exposed mutably, preserving ordering.
pub struct BoundedMap<K, V> {
    entries: Vec<(K, V)>,
    limit: usize,
}
impl<K: Ord, V> BoundedMap<K, V> {
    pub fn new(limit: usize) -> Result<Self, Error> {
        let mut entries = Vec::new();
        entries
            .try_reserve_exact(limit)
            .map_err(|_| Error::Allocation)?;
        Ok(Self { entries, limit })
    }
    fn position(&self, key: &K) -> Result<usize, usize> {
        self.entries.binary_search_by(|(k, _)| k.cmp(key))
    }
    pub fn get(&self, key: &K) -> Option<&V> {
        self.position(key).ok().map(|i| &self.entries[i].1)
    }
    pub fn get_mut(&mut self, key: &K) -> Option<&mut V> {
        self.position(key).ok().map(|i| &mut self.entries[i].1)
    }
    pub fn insert(&mut self, key: K, value: V) -> Result<Option<V>, Rejected<(K, V)>> {
        match self.position(&key) {
            Ok(i) => Ok(Some(std::mem::replace(&mut self.entries[i].1, value))),
            Err(i) => {
                if self.entries.len() == self.limit {
                    return Err(Rejected {
                        reason: Error::Capacity { limit: self.limit },
                        value: (key, value),
                    });
                }
                self.entries.insert(i, (key, value));
                Ok(None)
            }
        }
    }
    /// Factory runs only after a vacant slot is available. A failed capacity
    /// check has no side effects and never invokes the factory.
    pub fn get_or_insert_with(
        &mut self,
        key: K,
        create: impl FnOnce() -> V,
    ) -> Result<&mut V, Error> {
        let i = match self.position(&key) {
            Ok(i) => i,
            Err(i) => {
                if self.entries.len() == self.limit {
                    return Err(Error::Capacity { limit: self.limit });
                }
                let value = create();
                self.entries.insert(i, (key, value));
                i
            }
        };
        Ok(&mut self.entries[i].1)
    }
    pub fn remove(&mut self, key: &K) -> Option<V> {
        self.position(key).ok().map(|i| self.entries.remove(i).1)
    }
    pub fn retain(&mut self, mut keep: impl FnMut(&K, &V) -> bool) {
        self.entries.retain(|(k, v)| keep(k, v));
    }
    pub fn iter(&self) -> impl ExactSizeIterator<Item = (&K, &V)> {
        self.entries.iter().map(|(k, v)| (k, v))
    }
    pub fn len(&self) -> usize {
        self.entries.len()
    }
    pub fn is_empty(&self) -> bool {
        self.entries.is_empty()
    }
    pub fn capacity(&self) -> usize {
        self.limit
    }
    pub fn clear(&mut self) {
        self.entries.clear();
    }
}
/// FIFO with explicit rejection or explicit oldest-record replacement.
pub struct BoundedQueue<T> {
    values: VecDeque<T>,
    limit: usize,
}
impl<T> BoundedQueue<T> {
    pub fn new(limit: usize) -> Result<Self, Error> {
        let mut values = VecDeque::new();
        values
            .try_reserve_exact(limit)
            .map_err(|_| Error::Allocation)?;
        Ok(Self { values, limit })
    }
    pub fn push(&mut self, value: T) -> Result<(), Rejected<T>> {
        if self.values.len() == self.limit {
            return Err(Rejected {
                reason: Error::Capacity { limit: self.limit },
                value,
            });
        }
        self.values.push_back(value);
        Ok(())
    }
    /// Returns the displaced record, making evidence loss observable.
    pub fn push_overwrite(&mut self, value: T) -> Result<Option<T>, Rejected<T>> {
        if self.limit == 0 {
            return Err(Rejected {
                reason: Error::Capacity { limit: 0 },
                value,
            });
        }
        let old = if self.values.len() == self.limit {
            self.values.pop_front()
        } else {
            None
        };
        self.values.push_back(value);
        Ok(old)
    }
    pub fn pop(&mut self) -> Option<T> {
        self.values.pop_front()
    }
    pub fn iter(&self) -> impl ExactSizeIterator<Item = &T> {
        self.values.iter()
    }
    pub fn len(&self) -> usize {
        self.values.len()
    }
    pub fn is_empty(&self) -> bool {
        self.values.is_empty()
    }
    pub fn capacity(&self) -> usize {
        self.limit
    }
    pub fn clear(&mut self) {
        self.values.clear();
    }
}
#[derive(Debug, Default, Clone, Copy, PartialEq, Eq)]
pub struct Counter(u64);
impl Counter {
    pub fn new(value: u64) -> Self {
        Self(value)
    }
    pub fn value(&self) -> u64 {
        self.0
    }
    pub fn add(&mut self, amount: u64) -> Result<u64, Error> {
        let next = self.0.checked_add(amount).ok_or(Error::Overflow)?;
        self.0 = next;
        Ok(next)
    }
    pub fn reset(&mut self) -> u64 {
        std::mem::take(&mut self.0)
    }
}
/// Upper-exclusive cut points: [-inf,b0), [b0,b1), ..., [last,+inf).
/// Bucket and total updates are transactional on overflow.
pub struct Histogram {
    bounds: Vec<i64>,
    counts: Vec<u64>,
    total: u64,
}
impl Histogram {
    pub fn new(bounds: Vec<i64>, max_buckets: usize) -> Result<Self, Error> {
        if bounds.windows(2).any(|w| w[0] >= w[1]) {
            return Err(Error::InvalidBounds);
        }
        let n = bounds.len().checked_add(1).ok_or(Error::Overflow)?;
        if n > max_buckets {
            return Err(Error::Capacity { limit: max_buckets });
        }
        let mut counts = Vec::new();
        counts.try_reserve_exact(n).map_err(|_| Error::Allocation)?;
        counts.resize(n, 0);
        Ok(Self {
            bounds,
            counts,
            total: 0,
        })
    }
    pub fn add(&mut self, sample: i64, weight: u64) -> Result<usize, Error> {
        let i = self.bounds.partition_point(|bound| *bound <= sample);
        let count = self.counts[i].checked_add(weight).ok_or(Error::Overflow)?;
        let total = self.total.checked_add(weight).ok_or(Error::Overflow)?;
        self.counts[i] = count;
        self.total = total;
        Ok(i)
    }
    pub fn bounds(&self) -> &[i64] {
        &self.bounds
    }
    pub fn counts(&self) -> &[u64] {
        &self.counts
    }
    pub fn total(&self) -> u64 {
        self.total
    }
    pub fn reset(&mut self) {
        self.counts.fill(0);
        self.total = 0;
    }
}

/// Ordered bounded membership set backed by the same preallocated map storage.
pub struct BoundedSet<K> {
    values: BoundedMap<K, ()>,
}
impl<K: Ord> BoundedSet<K> {
    pub fn new(limit: usize) -> Result<Self, Error> {
        Ok(Self {
            values: BoundedMap::new(limit)?,
        })
    }
    pub fn insert(&mut self, key: K) -> Result<bool, Rejected<K>> {
        self.values
            .insert(key, ())
            .map(|old| old.is_none())
            .map_err(|rejected| Rejected {
                reason: rejected.reason,
                value: rejected.value.0,
            })
    }
    pub fn contains(&self, key: &K) -> bool {
        self.values.get(key).is_some()
    }
    pub fn remove(&mut self, key: &K) -> bool {
        self.values.remove(key).is_some()
    }
    pub fn iter(&self) -> impl ExactSizeIterator<Item = &K> {
        self.values.iter().map(|(key, _)| key)
    }
    pub fn len(&self) -> usize {
        self.values.len()
    }
    pub fn is_empty(&self) -> bool {
        self.values.is_empty()
    }
    pub fn capacity(&self) -> usize {
        self.values.capacity()
    }
    pub fn clear(&mut self) {
        self.values.clear();
    }
}
