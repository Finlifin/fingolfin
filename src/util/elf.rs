//! Structural checks for the supported ELF64 little-endian BPF relocatable format.
//! This is not an instruction verifier; execution still requires VM validation.
fn slice(bytes: &[u8], offset: u64, size: u64) -> Result<&[u8], String> {
    let end = offset.checked_add(size).ok_or("ELF range overflow")?;
    bytes
        .get(
            usize::try_from(offset).map_err(|_| "ELF offset")?
                ..usize::try_from(end).map_err(|_| "ELF end")?,
        )
        .ok_or("truncated ELF range".into())
}
fn u16_at(b: &[u8], n: usize) -> u16 {
    u16::from_le_bytes(b[n..n + 2].try_into().unwrap())
}
fn u32_at(b: &[u8], n: usize) -> u32 {
    u32::from_le_bytes(b[n..n + 4].try_into().unwrap())
}
fn u64_at(b: &[u8], n: usize) -> u64 {
    u64::from_le_bytes(b[n..n + 8].try_into().unwrap())
}
fn name(strings: &[u8], offset: u32) -> Result<&str, String> {
    let bytes = strings.get(offset as usize..).ok_or("ELF string offset")?;
    let end = bytes
        .iter()
        .position(|b| *b == 0)
        .ok_or("unterminated ELF string")?;
    std::str::from_utf8(&bytes[..end]).map_err(|_| "non UTF-8 ELF symbol".into())
}
pub fn validate_bpf(bytes: &[u8]) -> Result<(), String> {
    let h = slice(bytes, 0, 64)?;
    if &h[..7] != b"\x7fELF\x02\x01\x01"
        || u16_at(h, 16) != 1
        || u16_at(h, 18) != 247
        || u32_at(h, 20) != 1
        || u16_at(h, 52) != 64
        || u16_at(h, 58) != 64
    {
        return Err("unsupported ELF format/target".into());
    }
    let count = u16_at(h, 60) as u64;
    if count == 0 || count > 4096 {
        return Err("unsupported ELF section count".into());
    }
    let table = slice(bytes, u64_at(h, 40), count * 64)?;
    let sections: Vec<_> = table
        .as_chunks::<64>()
        .0
        .iter()
        .map(|s| s.as_slice())
        .collect();
    for section in &sections {
        if u32_at(section, 4) != 8 {
            slice(bytes, u64_at(section, 24), u64_at(section, 32))?;
        }
    }
    let mut entries = 0;
    for section in &sections {
        if u32_at(section, 4) != 2 {
            continue;
        }
        if u64_at(section, 56) != 24 || !u64_at(section, 32).is_multiple_of(24) {
            return Err("invalid ELF symbol table".into());
        }
        let string_section = sections
            .get(u32_at(section, 40) as usize)
            .ok_or("invalid ELF string table index")?;
        if u32_at(string_section, 4) != 3 {
            return Err("invalid ELF string table".into());
        }
        let strings = slice(
            bytes,
            u64_at(string_section, 24),
            u64_at(string_section, 32),
        )?;
        for (i, symbol) in slice(bytes, u64_at(section, 24), u64_at(section, 32))?
            .as_chunks::<24>()
            .0
            .iter()
            .enumerate()
        {
            let n = name(strings, u32_at(symbol, 0))?;
            let index = u16_at(symbol, 6) as usize;
            if i != 0 && index == 0 {
                return Err(format!("unresolved ELF symbol `{n}`"));
            }
            if n == "entry" {
                let code = sections.get(index).ok_or("invalid entry section")?;
                let offset = u64_at(symbol, 8);
                let size = u64_at(symbol, 16);
                if symbol[4] != 0x12
                    || u64_at(code, 8) & 4 == 0
                    || size == 0
                    || !offset.is_multiple_of(8)
                    || !size.is_multiple_of(8)
                    || offset
                        .checked_add(size)
                        .is_none_or(|end| end > u64_at(code, 32))
                {
                    return Err("invalid BPF entry symbol".into());
                }
                entries += 1;
            }
        }
    }
    if entries != 1 {
        return Err("ELF requires exactly one global entry function".into());
    }
    Ok(())
}

/// Fixed helper IDs used by executable sections (pseudo local calls excluded).
pub fn helper_ids(bytes: &[u8]) -> Result<Vec<u32>, String> {
    validate_bpf(bytes)?;
    let h = slice(bytes, 0, 64)?;
    let table = slice(bytes, u64_at(h, 40), u64::from(u16_at(h, 60)) * 64)?;
    let mut ids = std::collections::BTreeSet::new();
    for section in table.as_chunks::<64>().0 {
        if u32_at(section, 4) != 1 || u64_at(section, 8) & 4 == 0 {
            continue;
        }
        let code = slice(bytes, u64_at(section, 24), u64_at(section, 32))?;
        if !code.len().is_multiple_of(8) {
            return Err("unaligned BPF code".into());
        }
        let mut offset = 0;
        while offset < code.len() {
            let insn = &code[offset..offset + 8];
            if insn[0] == 0x85 && insn[1] >> 4 == 0 {
                ids.insert(u32_at(insn, 4));
            }
            offset += if insn[0] == 0x18 { 16 } else { 8 };
            if offset > code.len() {
                return Err("truncated BPF wide immediate".into());
            }
        }
    }
    Ok(ids.into_iter().collect())
}
