//! UTF-8 source positions. Lines and Unicode-scalar columns are one-based;
//! byte offsets stay compatible with parser spans. Columns are not display cells.
#[derive(Debug, Clone, Copy, PartialEq, Eq)]
pub struct Position {
    pub line: usize,
    pub column: usize,
}
pub struct LineIndex<'a> {
    source: &'a str,
    starts: Vec<usize>,
}
impl<'a> LineIndex<'a> {
    pub fn new(source: &'a str) -> Self {
        let mut starts = vec![0];
        starts.extend(
            source
                .bytes()
                .enumerate()
                .filter_map(|(i, b)| (b == b'\n').then_some(i + 1)),
        );
        Self { source, starts }
    }
    /// Returns None for out-of-range offsets or offsets inside UTF-8 scalars.
    pub fn position(&self, offset: usize) -> Option<Position> {
        if !self.source.is_char_boundary(offset) {
            return None;
        }
        let line = self.starts.partition_point(|start| *start <= offset) - 1;
        Some(Position {
            line: line + 1,
            column: self.source[self.starts[line]..offset].chars().count() + 1,
        })
    }
    /// One-based line, excluding LF and an optional preceding CR.
    pub fn line(&self, number: usize) -> Option<&'a str> {
        let index = number.checked_sub(1)?;
        let start = *self.starts.get(index)?;
        let end = self
            .starts
            .get(index + 1)
            .copied()
            .unwrap_or(self.source.len());
        let line = &self.source[start..end];
        Some(
            line.strip_suffix('\n')
                .map(|s| s.strip_suffix('\r').unwrap_or(s))
                .unwrap_or(line),
        )
    }
}
