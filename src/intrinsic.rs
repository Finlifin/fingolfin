//! Reserved, typed host interfaces. No arbitrary native symbol calls.
use crate::{
    types::Ty,
    util::containers::{BoundedMap, BoundedQueue, Counter, Error, Histogram},
};
use serde::{Deserialize, Serialize};
#[derive(Debug, Clone, Copy, PartialEq, Eq, PartialOrd, Ord, Serialize, Deserialize)]
pub enum Intrinsic {
    CounterAdd,
    Emit,
    MapAdd,
    HistogramAdd,
}
impl Intrinsic {
    pub fn lookup(path: &str) -> Option<Self> {
        match path {
            "builtin.counter_add" => Some(Self::CounterAdd),
            "builtin.emit" => Some(Self::Emit),
            "builtin.map_add" => Some(Self::MapAdd),
            "builtin.histogram_add" => Some(Self::HistogramAdd),
            _ => None,
        }
    }
    pub fn id(self) -> u32 {
        match self {
            Self::CounterAdd => 1,
            Self::Emit => 2,
            Self::MapAdd => 3,
            Self::HistogramAdd => 4,
        }
    }
    pub fn parameters(self) -> &'static [Ty] {
        static TWO: [Ty; 2] = [
            Ty::Integer {
                signed: false,
                bits: 32,
            },
            Ty::Integer {
                signed: false,
                bits: 64,
            },
        ];
        static MAP: [Ty; 3] = [
            Ty::Integer {
                signed: false,
                bits: 32,
            },
            Ty::Integer {
                signed: false,
                bits: 64,
            },
            Ty::Integer {
                signed: false,
                bits: 64,
            },
        ];
        static HIST: [Ty; 3] = [
            Ty::Integer {
                signed: false,
                bits: 32,
            },
            Ty::Integer {
                signed: true,
                bits: 64,
            },
            Ty::Integer {
                signed: false,
                bits: 64,
            },
        ];
        match self {
            Self::CounterAdd | Self::Emit => &TWO,
            Self::MapAdd => &MAP,
            Self::HistogramAdd => &HIST,
        }
    }
}
#[derive(Debug, Clone, Copy, PartialEq, Eq, Serialize)]
pub struct Record {
    pub tag: u32,
    pub value: u64,
}
#[derive(Debug, Clone, PartialEq, Eq)]
pub enum HostError {
    MissingCounter(u32),
    DuplicateCounter(u32),
    Container(Error),
    MissingMap(u32),
    MissingHistogram(u32),
    DuplicateSlot(u32),
    InvalidArguments,
}
/// One instance per attachment/configuration/object scope. Predeclare counter
/// slots before publication; operations never allocate new state slots.
pub struct Host {
    counters: BoundedMap<u32, Counter>,
    output: BoundedQueue<Record>,
    maps: BoundedMap<u32, BoundedMap<u64, Counter>>,
    histograms: BoundedMap<u32, Histogram>,
}
impl Host {
    pub fn new(counter_capacity: usize, output_capacity: usize) -> Result<Self, Error> {
        Self::with_capacities(counter_capacity, 0, 0, output_capacity)
    }
    pub fn with_capacities(
        counter_capacity: usize,
        map_slots: usize,
        histogram_slots: usize,
        output_capacity: usize,
    ) -> Result<Self, Error> {
        Ok(Self {
            counters: BoundedMap::new(counter_capacity)?,
            output: BoundedQueue::new(output_capacity)?,
            maps: BoundedMap::new(map_slots)?,
            histograms: BoundedMap::new(histogram_slots)?,
        })
    }
    pub fn declare_counter(&mut self, slot: u32, initial: u64) -> Result<(), HostError> {
        if self.counters.get(&slot).is_some() {
            return Err(HostError::DuplicateCounter(slot));
        }
        self.counters
            .insert(slot, Counter::new(initial))
            .map(|_| ())
            .map_err(|e| HostError::Container(e.reason))
    }
    pub fn counter(&self, slot: u32) -> Option<u64> {
        self.counters.get(&slot).map(Counter::value)
    }
    pub fn pop_output(&mut self) -> Option<Record> {
        self.output.pop()
    }
    pub fn declare_map(&mut self, slot: u32, capacity: usize) -> Result<(), HostError> {
        if self.maps.get(&slot).is_some() {
            return Err(HostError::DuplicateSlot(slot));
        }
        if self.maps.len() == self.maps.capacity() {
            return Err(HostError::Container(Error::Capacity {
                limit: self.maps.capacity(),
            }));
        }
        let map = BoundedMap::new(capacity).map_err(HostError::Container)?;
        self.maps
            .insert(slot, map)
            .map(|_| ())
            .map_err(|e| HostError::Container(e.reason))
    }
    pub fn declare_histogram(
        &mut self,
        slot: u32,
        bounds: Vec<i64>,
        max_buckets: usize,
    ) -> Result<(), HostError> {
        if self.histograms.get(&slot).is_some() {
            return Err(HostError::DuplicateSlot(slot));
        }
        if self.histograms.len() == self.histograms.capacity() {
            return Err(HostError::Container(Error::Capacity {
                limit: self.histograms.capacity(),
            }));
        }
        let histogram = Histogram::new(bounds, max_buckets).map_err(HostError::Container)?;
        self.histograms
            .insert(slot, histogram)
            .map(|_| ())
            .map_err(|e| HostError::Container(e.reason))
    }
    pub fn map(&self, slot: u32) -> Option<&BoundedMap<u64, Counter>> {
        self.maps.get(&slot)
    }
    pub fn histogram(&self, slot: u32) -> Option<&Histogram> {
        self.histograms.get(&slot)
    }
    pub fn invoke(&mut self, intrinsic: Intrinsic, slot: u32, value: u64) -> Result<(), HostError> {
        if intrinsic.parameters().len() != 2 {
            return Err(HostError::InvalidArguments);
        }
        self.invoke_words(intrinsic, [u64::from(slot), value, 0, 0, 0])
    }
    pub fn invoke_values(
        &mut self,
        intrinsic: Intrinsic,
        values: &[crate::pattern::Value],
    ) -> Result<(), HostError> {
        let types = intrinsic.parameters();
        if values.len() != types.len() {
            return Err(HostError::InvalidArguments);
        }
        let mut words = [0u64; 5];
        for (i, (value, ty)) in values.iter().zip(types).enumerate() {
            let (crate::pattern::Value::Integer(n), Ty::Integer { signed, bits }) = (value, ty)
            else {
                return Err(HostError::InvalidArguments);
            };
            if !crate::types::integer_fits(*n, *signed, *bits) {
                return Err(HostError::InvalidArguments);
            }
            words[i] = *n as u64;
        }
        self.invoke_words(intrinsic, words)
    }
    /// Five-register ABI. Signed samples use their 64-bit two's-complement bits.
    pub fn invoke_words(&mut self, intrinsic: Intrinsic, args: [u64; 5]) -> Result<(), HostError> {
        let slot = u32::try_from(args[0]).map_err(|_| HostError::InvalidArguments)?;
        if args[intrinsic.parameters().len()..]
            .iter()
            .any(|word| *word != 0)
        {
            return Err(HostError::InvalidArguments);
        }
        match intrinsic {
            Intrinsic::CounterAdd => {
                self.counters
                    .get_mut(&slot)
                    .ok_or(HostError::MissingCounter(slot))?
                    .add(args[1])
                    .map_err(HostError::Container)?;
                Ok(())
            }
            Intrinsic::Emit => self
                .output
                .push(Record {
                    tag: slot,
                    value: args[1],
                })
                .map_err(|e| HostError::Container(e.reason)),
            Intrinsic::MapAdd => {
                let map = self
                    .maps
                    .get_mut(&slot)
                    .ok_or(HostError::MissingMap(slot))?;
                map.get_or_insert_with(args[1], Counter::default)
                    .map_err(HostError::Container)?
                    .add(args[2])
                    .map_err(HostError::Container)?;
                Ok(())
            }
            Intrinsic::HistogramAdd => {
                self.histograms
                    .get_mut(&slot)
                    .ok_or(HostError::MissingHistogram(slot))?
                    .add(args[1] as i64, args[2])
                    .map_err(HostError::Container)?;
                Ok(())
            }
        }
    }
}

#[derive(Debug, Default, Serialize, Deserialize)]
#[serde(deny_unknown_fields)]
pub struct HostConfig {
    #[serde(default)]
    pub counters: Vec<CounterConfig>,
    #[serde(default)]
    pub maps: Vec<MapConfig>,
    #[serde(default)]
    pub histograms: Vec<HistogramConfig>,
    #[serde(default)]
    pub output_capacity: usize,
}
#[derive(Debug, Serialize, Deserialize)]
#[serde(deny_unknown_fields)]
pub struct CounterConfig {
    pub slot: u32,
    #[serde(default)]
    pub initial: u64,
}
#[derive(Debug, Serialize, Deserialize)]
#[serde(deny_unknown_fields)]
pub struct MapConfig {
    pub slot: u32,
    pub capacity: usize,
}
#[derive(Debug, Serialize, Deserialize)]
#[serde(deny_unknown_fields)]
pub struct HistogramConfig {
    pub slot: u32,
    pub bounds: Vec<i64>,
    pub max_buckets: usize,
}
impl HostConfig {
    pub fn prepare(self) -> Result<Host, HostError> {
        let mut host = Host::with_capacities(
            self.counters.len(),
            self.maps.len(),
            self.histograms.len(),
            self.output_capacity,
        )
        .map_err(HostError::Container)?;
        for config in self.counters {
            host.declare_counter(config.slot, config.initial)?;
        }
        for config in self.maps {
            host.declare_map(config.slot, config.capacity)?;
        }
        for config in self.histograms {
            host.declare_histogram(config.slot, config.bounds, config.max_buckets)?;
        }
        Ok(host)
    }
}
impl Host {
    /// Allocating diagnostic snapshot; keep outside the observation hot path.
    pub fn snapshot(&self) -> serde_json::Value {
        use std::collections::BTreeMap;
        let counters: BTreeMap<_, _> = self
            .counters
            .iter()
            .map(|(slot, c)| (*slot, c.value()))
            .collect();
        let maps: BTreeMap<_, _> = self
            .maps
            .iter()
            .map(|(slot, map)| {
                (
                    *slot,
                    map.iter()
                        .map(|(key, c)| (*key, c.value()))
                        .collect::<BTreeMap<_, _>>(),
                )
            })
            .collect();
        let histograms: BTreeMap<_, _> = self
            .histograms
            .iter()
            .map(|(slot, h)| {
                (
                    *slot,
                    serde_json::json!({"bounds":h.bounds(),"counts":h.counts(),"total":h.total()}),
                )
            })
            .collect();
        let records: Vec<_> = self.output.iter().collect();
        serde_json::json!({"counters":counters,"maps":maps,"histograms":histograms,"records":records})
    }
}
