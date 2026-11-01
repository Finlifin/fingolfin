//! Stable content hashes. These identify bytes, not ABI compatibility.
use sha2::{Digest, Sha256};
pub fn sha256(bytes: &[u8]) -> String {
    format!("{:x}", Sha256::digest(bytes))
}
/// Length-prefixed fields avoid ambiguous concatenations. Field lengths are
/// encoded as little-endian u64; callers must use deterministic field order.
#[derive(Default)]
pub struct ContentHash(Sha256);
impl ContentHash {
    pub fn new() -> Self {
        Self::default()
    }
    pub fn field(&mut self, bytes: &[u8]) {
        self.0.update((bytes.len() as u64).to_le_bytes());
        self.0.update(bytes);
    }
    pub fn finish(self) -> String {
        format!("{:x}", self.0.finalize())
    }
}
