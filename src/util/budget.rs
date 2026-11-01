//! Explicit, non-replenishing work budget. Charge before performing work.
#[derive(Debug, Clone, Copy, PartialEq, Eq)]
pub struct Exhausted {
    pub requested: u64,
    pub remaining: u64,
}
#[derive(Debug, Clone, PartialEq, Eq)]
pub struct Budget {
    limit: u64,
    remaining: u64,
}
impl Budget {
    pub fn new(limit: u64) -> Self {
        Self {
            limit,
            remaining: limit,
        }
    }
    /// Failed charges leave the budget unchanged; callers must propagate failure.
    pub fn charge(&mut self, amount: u64) -> Result<(), Exhausted> {
        self.remaining = self.remaining.checked_sub(amount).ok_or(Exhausted {
            requested: amount,
            remaining: self.remaining,
        })?;
        Ok(())
    }
    pub fn remaining(&self) -> u64 {
        self.remaining
    }
    pub fn consumed(&self) -> u64 {
        self.limit - self.remaining
    }
}
