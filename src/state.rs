//! Explicit ownership scope for fixed-layout runtime state. No process-global
//! current program, configuration, or object. Synchronization belongs to owner.
use crate::{
    sdk::ObjectKey,
    util::containers::{BoundedMap, Error},
};
#[derive(Debug, Clone, Copy, PartialEq, Eq, PartialOrd, Ord)]
pub struct Key {
    pub program: u64,
    pub configuration: u64,
    pub object: ObjectKey,
}
pub struct Store<T> {
    values: BoundedMap<Key, T>,
}
impl<T> Store<T> {
    pub fn new(max_instances: usize) -> Result<Self, Error> {
        Ok(Self {
            values: BoundedMap::new(max_instances)?,
        })
    }
    pub fn get(&self, key: &Key) -> Option<&T> {
        self.values.get(key)
    }
    pub fn get_mut(&mut self, key: &Key) -> Option<&mut T> {
        self.values.get_mut(key)
    }
    pub fn get_or_create(&mut self, key: Key, create: impl FnOnce() -> T) -> Result<&mut T, Error> {
        self.values.get_or_insert_with(key, create)
    }
    pub fn remove(&mut self, key: &Key) -> Option<T> {
        self.values.remove(key)
    }
    pub fn remove_object(&mut self, object: ObjectKey) -> usize {
        self.remove_where(|key| key.object == object)
    }
    pub fn remove_program(&mut self, program: u64) -> usize {
        self.remove_where(|key| key.program == program)
    }
    pub fn remove_configuration(&mut self, program: u64, configuration: u64) -> usize {
        self.remove_where(|key| key.program == program && key.configuration == configuration)
    }
    pub fn remove_launch(&mut self, run: u64, launch: u64) -> usize {
        self.remove_where(|key| key.object.run == run && key.object.launch == launch)
    }
    fn remove_where(&mut self, mut remove: impl FnMut(&Key) -> bool) -> usize {
        let before = self.values.len();
        self.values.retain(|key, _| !remove(key));
        before - self.values.len()
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
}
