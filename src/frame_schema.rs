//! Portable development frame schema v1. Wire fields are little endian; no native pointers.
pub const MAX_BYTES: usize = 1024 * 1024;
pub const MAX_NODES: usize = 4096;
fn u32_at(b: &[u8], n: usize) -> u32 {
    u32::from_le_bytes(b[n..n + 4].try_into().unwrap())
}
fn u64_at(b: &[u8], n: usize) -> u64 {
    u64::from_le_bytes(b[n..n + 8].try_into().unwrap())
}
fn align(n: u64, a: u64) -> Result<u64, String> {
    n.checked_add(a - 1)
        .map(|n| n / a * a)
        .ok_or("schema alignment overflow".into())
}
fn fail() -> String {
    "invalid frame schema".into()
}
pub fn validate(b: &[u8]) -> Result<(), String> {
    if b.len() < 64
        || b.len() > MAX_BYTES
        || &b[..8] != b"FGFRAME\0"
        || u32_at(b, 8) != 1
        || u32_at(b, 12) as usize != b.len()
        || u32_at(b, 52) != 0
        || u32_at(b, 60) != 0
    {
        return Err(fail());
    }
    let nc = u32_at(b, 40) as usize;
    let mc = u32_at(b, 44) as usize;
    let sc = u32_at(b, 56) as usize;
    let names = u32_at(b, 48) as usize;
    if nc > MAX_NODES || mc > MAX_BYTES / 24 || sc > MAX_NODES {
        return Err(fail());
    }
    let member_start = 64 + nc * 40;
    let slot_start = member_start + mc * 24;
    let name_start = slot_start + sc * 24;
    if name_start.checked_add(names) != Some(b.len()) {
        return Err(fail());
    }
    let params = u32_at(b, 28) as usize;
    let bindings = u32_at(b, 32) as usize;
    let result = u32_at(b, 36) as usize;
    if result > 1
        || params
            .checked_add(bindings)
            .and_then(|n| n.checked_add(result))
            != Some(sc)
    {
        return Err(fail());
    }
    let node = |i: usize| &b[64 + i * 40..64 + (i + 1) * 40];
    let name = |m: &[u8]| -> Result<&str, String> {
        let offset = u32_at(m, 12) as usize;
        let len = u32_at(m, 16) as usize;
        if len == 0 || len > 4096 || offset > names || len > names - offset {
            return Err(fail());
        }
        let s = std::str::from_utf8(&b[name_start + offset..name_start + offset + len])
            .map_err(|_| fail())?;
        if s.as_bytes().contains(&0) {
            return Err(fail());
        }
        Ok(s)
    };
    let mut next_name = 0;
    for i in 0..mc + sc {
        let start = member_start + i * 24;
        let record = &b[start..start + 24];
        if u32_at(record, 12) as usize != next_name {
            return Err(fail());
        }
        next_name += name(record)?.len();
    }
    if next_name != names {
        return Err(fail());
    }
    let mut used = vec![false; nc];
    let mut depths = vec![0usize; nc];
    let mut next_member = 0;
    for i in 0..nc {
        let n = node(i);
        let kind = n[0];
        let signed = n[1];
        let bits = n[2];
        let a = u64::from(u32_at(n, 4));
        let size = u64_at(n, 8);
        let element = u32_at(n, 16) as usize;
        let start = u32_at(n, 20) as usize;
        let count = u32_at(n, 24) as usize;
        let length = u64_at(n, 32);
        if n[3] != 0
            || u32_at(n, 28) != 0
            || ![1, 2, 4, 8].contains(&a)
            || size == 0
            || size > 32 * 1024 * 1024
            || !size.is_multiple_of(a)
            || signed > 1
        {
            return Err(fail());
        }
        if kind != 6 && (element != 0 || length != 0) {
            return Err(fail());
        }
        if ![4, 5].contains(&kind) && (start != 0 || count != 0) {
            return Err(fail());
        }
        if ![1, 4].contains(&kind) && (signed != 0 || bits != 0) {
            return Err(fail());
        }
        let mut depth = 1;
        match kind {
            1 | 4 => {
                if ![8, 16, 32, 64].contains(&bits) || size != u64::from(bits) / 8 || a != size {
                    return Err(fail());
                }
            }
            2 => {
                if size != 1 || a != 1 {
                    return Err(fail());
                }
            }
            3 => {
                if size != 8 || a != 8 {
                    return Err(fail());
                }
            }
            5 => {}
            6 => {
                if element >= i || used[element] || length == 0 {
                    return Err(fail());
                }
                used[element] = true;
                if u64_at(node(element), 8).checked_mul(length) != Some(size)
                    || u32_at(node(element), 4) as u64 != a
                {
                    return Err(fail());
                }
                depth = depths[element] + 1;
            }
            _ => return Err(fail()),
        }
        if kind == 4 || kind == 5 {
            if count == 0 || count > 256 || start != next_member || count > mc.saturating_sub(start)
            {
                return Err(fail());
            }
            next_member += count;
            let mut names_seen = std::collections::BTreeSet::new();
            let mut values_seen = std::collections::BTreeSet::new();
            let mut end = 0;
            let mut max_align = 1;
            for j in start..start + count {
                let m = &b[member_start + j * 24..member_start + (j + 1) * 24];
                if u32_at(m, 20) != 0 || !names_seen.insert(name(m)?) {
                    return Err(fail());
                }
                let value = u64_at(m, 0);
                let child = u32_at(m, 8) as usize;
                if kind == 4 {
                    if child != u32::MAX as usize || !values_seen.insert(value) {
                        return Err(fail());
                    }
                    let v = if signed == 1 {
                        value as i64 as i128
                    } else {
                        i128::from(value)
                    };
                    if !crate::types::integer_fits(v, signed == 1, bits) {
                        return Err(fail());
                    }
                } else {
                    if child >= i || used[child] {
                        return Err(fail());
                    }
                    used[child] = true;
                    let ca = u64::from(u32_at(node(child), 4));
                    if value != align(end, ca)? {
                        return Err(fail());
                    }
                    end = value.checked_add(u64_at(node(child), 8)).ok_or_else(fail)?;
                    max_align = max_align.max(ca);
                    depth = depth.max(depths[child] + 1);
                }
            }
            if kind == 5 && (max_align != a || align(end, a)? != size) {
                return Err(fail());
            }
        }
        if depth > 32 {
            return Err(fail());
        }
        depths[i] = depth;
    }
    if next_member != mc {
        return Err(fail());
    }
    let mut end = 16;
    let mut max_align = 8;
    let mut previous_binding = None;
    for i in 0..sc {
        let slot = &b[slot_start + i * 24..slot_start + (i + 1) * 24];
        let child = u32_at(slot, 8) as usize;
        let role = u32_at(slot, 20);
        let text = name(slot)?;
        if child >= nc || used[child] {
            return Err(fail());
        }
        used[child] = true;
        if i < params {
            if role != 0 || text != format!("arg{i}") {
                return Err(fail());
            }
        } else if i < params + bindings {
            if role != 1 || previous_binding.is_some_and(|p| p >= text) {
                return Err(fail());
            }
            previous_binding = Some(text);
        } else if role != 2 || text != "result" {
            return Err(fail());
        }
        let a = u64::from(u32_at(node(child), 4));
        let offset = u64_at(slot, 0);
        if offset != align(end, a)? {
            return Err(fail());
        }
        end = offset
            .checked_add(u64_at(node(child), 8))
            .ok_or_else(fail)?;
        max_align = max_align.max(a);
    }
    if used.iter().any(|used| !*used)
        || u32_at(b, 24) as u64 != max_align
        || u64_at(b, 16) != align(end, max_align)?
        || u64_at(b, 16) > 32 * 1024 * 1024
    {
        return Err(fail());
    }
    Ok(())
}
