//! Typed encoding of the development BPF frame. All offsets share codegen's layout.
use crate::{
    compiler::CheckedProgram,
    ir,
    pattern::Value,
    syntax::ItemKind,
    types::{self, Ty},
};
use std::collections::BTreeMap;
#[derive(Debug, Clone)]
enum Kind {
    Integer {
        signed: bool,
        bits: u8,
    },
    Bool,
    Pointer,
    Enum {
        signed: bool,
        bits: u8,
        variants: BTreeMap<String, i128>,
    },
    Record(Vec<(String, u64, Shape)>),
    Array(Box<Shape>, u64),
}
#[derive(Debug, Clone)]
struct Shape {
    size: u64,
    align: u64,
    kind: Kind,
}
impl Shape {
    fn new(program: &CheckedProgram, ty: &Ty) -> Result<Self, String> {
        let (size, align, kind) = match ty {
            Ty::Integer { signed, bits } => (
                u64::from(*bits) / 8,
                u64::from(*bits) / 8,
                Kind::Integer {
                    signed: *signed,
                    bits: *bits,
                },
            ),
            Ty::Bool => (1, 1, Kind::Bool),
            Ty::Pointer(_) => (8, 8, Kind::Pointer),
            Ty::Array(inner, n) => {
                let inner = Self::new(program, inner)?;
                (
                    inner.size.checked_mul(*n).ok_or("array size overflow")?,
                    inner.align,
                    Kind::Array(Box::new(inner), *n),
                )
            }
            Ty::Struct(id) => {
                let layout = &program.types.layouts[id];
                let fields = program.types.fields[id]
                    .iter()
                    .map(|(name, ty)| {
                        Ok((name.clone(), layout.fields[name], Self::new(program, ty)?))
                    })
                    .collect::<Result<_, String>>()?;
                (layout.size, layout.align, Kind::Record(fields))
            }
            Ty::Enum(id) => {
                let symbol = &program.index.symbols[id];
                let ItemKind::Enum { repr, variants, .. } = &symbol.declaration.value.kind else {
                    return Err("expected enum declaration".into());
                };
                let Ty::Integer { signed, bits } =
                    types::resolve_type(&program.index, &symbol.module, repr)?
                else {
                    return Err("expected integer enum representation".into());
                };
                let layout = &program.types.layouts[id];
                (
                    layout.size,
                    layout.align,
                    Kind::Enum {
                        signed,
                        bits,
                        variants: variants.iter().map(|v| v.value.clone()).collect(),
                    },
                )
            }
        };
        Ok(Self { size, align, kind })
    }
    fn encode(&self, value: &Value, bytes: &mut [u8]) -> Result<(), String> {
        match (&self.kind, value) {
            (Kind::Integer { signed, bits }, Value::Integer(n))
                if types::integer_fits(*n, *signed, *bits) =>
            {
                bytes.copy_from_slice(&n.to_le_bytes()[..bytes.len()])
            }
            (Kind::Bool, Value::Bool(b)) => bytes[0] = u8::from(*b),
            (Kind::Pointer, Value::Pointer(n)) => bytes.copy_from_slice(&n.to_le_bytes()),
            (Kind::Enum { variants, .. }, Value::Variant(name)) => {
                let n = variants.get(name).ok_or("unknown enum variant")?;
                bytes.copy_from_slice(&n.to_le_bytes()[..bytes.len()]);
            }
            (Kind::Record(fields), Value::Record(values)) if fields.len() == values.len() => {
                for (name, offset, shape) in fields {
                    shape.encode(
                        values.get(name).ok_or("missing record field")?,
                        &mut bytes[*offset as usize..(*offset + shape.size) as usize],
                    )?;
                }
            }
            (Kind::Array(inner, count), Value::Array(values)) if values.len() as u64 == *count => {
                for (value, part) in values
                    .iter()
                    .zip(bytes.chunks_exact_mut(inner.size as usize))
                {
                    inner.encode(value, part)?;
                }
            }
            _ => return Err("frame value does not match ABI type or range".into()),
        }
        Ok(())
    }
    fn decode(&self, bytes: &[u8]) -> Result<Value, String> {
        fn integer(bytes: &[u8], signed: bool) -> i128 {
            let mut full = if signed && bytes[bytes.len() - 1] & 0x80 != 0 {
                [255; 16]
            } else {
                [0; 16]
            };
            full[..bytes.len()].copy_from_slice(bytes);
            i128::from_le_bytes(full)
        }
        Ok(match &self.kind {
            Kind::Integer { signed, .. } => Value::Integer(integer(bytes, *signed)),
            Kind::Bool => match bytes[0] {
                0 => Value::Bool(false),
                1 => Value::Bool(true),
                _ => return Err("invalid boolean result".into()),
            },
            Kind::Pointer => Value::Pointer(u64::from_le_bytes(
                bytes.try_into().map_err(|_| "pointer width")?,
            )),
            Kind::Enum {
                signed,
                bits,
                variants,
            } => {
                let n = integer(bytes, *signed);
                if !types::integer_fits(n, *signed, *bits) {
                    return Err("invalid enum result width".into());
                }
                Value::Variant(
                    variants
                        .iter()
                        .find(|(_, v)| **v == n)
                        .ok_or("invalid enum result")?
                        .0
                        .clone(),
                )
            }
            Kind::Record(fields) => Value::Record(
                fields
                    .iter()
                    .map(|(name, offset, shape)| {
                        Ok((
                            name.clone(),
                            shape.decode(
                                &bytes[*offset as usize..(*offset + shape.size) as usize],
                            )?,
                        ))
                    })
                    .collect::<Result<_, String>>()?,
            ),
            Kind::Array(inner, _) => Value::Array(
                bytes
                    .chunks_exact(inner.size as usize)
                    .map(|part| inner.decode(part))
                    .collect::<Result<_, _>>()?,
            ),
        })
    }
}
#[derive(Debug, Clone)]
pub struct Layout {
    pub(crate) size: u64,
    pub(crate) align: u64,
    pub(crate) offsets: Vec<u64>,
    arguments: Vec<Shape>,
    parameter_count: usize,
    bindings: Vec<String>,
    result: Option<Shape>,
}
fn aligned(value: u64, align: u64) -> Result<u64, String> {
    value
        .checked_add(align - 1)
        .map(|v| v / align * align)
        .ok_or_else(|| "frame layout overflow".into())
}
impl Layout {
    pub fn for_function(program: &CheckedProgram, function: &str) -> Result<Self, String> {
        let module = ir::lower(program)?;
        Self::new(
            program,
            module
                .functions
                .get(function)
                .ok_or("unknown frame function")?,
        )
    }
    pub(crate) fn new(program: &CheckedProgram, f: &ir::Function) -> Result<Self, String> {
        let arguments = f
            .parameters
            .iter()
            .chain(f.bindings.values())
            .map(|slot| Shape::new(program, &f.locals[*slot as usize].ty))
            .collect::<Result<Vec<_>, _>>()?;
        let result = f
            .returns
            .as_ref()
            .map(|ty| Shape::new(program, ty))
            .transpose()?;
        let mut offset = 16;
        let mut align = 8;
        let mut offsets = Vec::new();
        for shape in arguments.iter().chain(result.iter()) {
            align = align.max(shape.align);
            offset = aligned(offset, shape.align)?;
            offsets.push(offset);
            offset = offset
                .checked_add(shape.size)
                .ok_or("frame layout overflow")?;
        }
        Ok(Self {
            size: aligned(offset, align)?,
            align,
            offsets,
            arguments,
            parameter_count: f.parameters.len(),
            bindings: f.bindings.keys().cloned().collect(),
            result,
        })
    }
    pub fn size(&self) -> u64 {
        self.size
    }
    pub fn alignment(&self) -> u64 {
        self.align
    }
    /// Limits bytes before allocation. Checked source owns layout authority.
    pub fn allocate(self, max_bytes: usize) -> Result<Frame, String> {
        let size = usize::try_from(self.size).map_err(|_| "frame exceeds host address space")?;
        if size > max_bytes {
            return Err("frame exceeds configured capacity".into());
        }
        let mut bytes = Vec::new();
        bytes
            .try_reserve_exact(size)
            .map_err(|_| "frame allocation failed")?;
        bytes.resize(size, 0);
        Ok(Frame {
            layout: self,
            bytes,
        })
    }
}
pub struct Frame {
    layout: Layout,
    bytes: Vec<u8>,
}
impl Frame {
    /// Clears previous results and padding. Callers must not execute after an error.
    pub fn prepare(
        &mut self,
        args: &[Value],
        bindings: &BTreeMap<String, Value>,
        steps: u64,
        max_depth: u32,
    ) -> Result<(), String> {
        self.bytes.fill(0);
        if args.len() != self.layout.parameter_count || bindings.len() != self.layout.bindings.len()
        {
            return Err("argument or binding count mismatch".into());
        }
        self.bytes[..8].copy_from_slice(&steps.to_le_bytes());
        self.bytes[8..12].copy_from_slice(&max_depth.to_le_bytes());
        for (i, shape) in self.layout.arguments.iter().enumerate() {
            let value = if i < args.len() {
                &args[i]
            } else {
                bindings
                    .get(&self.layout.bindings[i - args.len()])
                    .ok_or("missing pattern binding")?
            };
            let start = self.layout.offsets[i] as usize;
            shape.encode(value, &mut self.bytes[start..start + shape.size as usize])?;
        }
        Ok(())
    }
    pub fn bytes(&self) -> &[u8] {
        &self.bytes
    }
    pub fn bytes_mut(&mut self) -> &mut [u8] {
        &mut self.bytes
    }
    pub fn remaining(&self) -> u64 {
        u64::from_le_bytes(self.bytes[..8].try_into().unwrap())
    }
    pub fn error(&self) -> u32 {
        u32::from_le_bytes(self.bytes[12..16].try_into().unwrap())
    }
    pub fn result(&self) -> Result<Option<Value>, String> {
        if self.error() != 0 {
            return Err(format!("entry error {}", self.error()));
        }
        self.layout
            .result
            .as_ref()
            .map(|shape| {
                let start = self.layout.offsets[self.layout.arguments.len()] as usize;
                shape.decode(&self.bytes[start..start + shape.size as usize])
            })
            .transpose()
    }
}

impl Layout {
    /// Portable ABI schema, independently checked by the C loader. Types form a
    /// postorder forest (no shared child nodes), avoiding recursive references.
    pub fn schema_bytes(&self) -> Result<Vec<u8>, String> {
        struct Writer {
            nodes: Vec<[u8; 40]>,
            members: Vec<[u8; 24]>,
            names: Vec<u8>,
        }
        fn word(out: &mut [u8], at: usize, value: u32) {
            out[at..at + 4].copy_from_slice(&value.to_le_bytes());
        }
        fn wide(out: &mut [u8], at: usize, value: u64) {
            out[at..at + 8].copy_from_slice(&value.to_le_bytes());
        }
        fn number(n: usize) -> Result<u32, String> {
            u32::try_from(n).map_err(|_| "schema size overflow".into())
        }
        impl Writer {
            fn name(&mut self, member: &mut [u8; 24], name: &str) -> Result<(), String> {
                word(member, 12, number(self.names.len())?);
                word(member, 16, number(name.len())?);
                self.names.extend_from_slice(name.as_bytes());
                Ok(())
            }
            fn shape(&mut self, shape: &Shape) -> Result<u32, String> {
                let mut node = [0; 40];
                word(&mut node, 4, shape.align as u32);
                wide(&mut node, 8, shape.size);
                let mut members = Vec::new();
                match &shape.kind {
                    Kind::Integer { signed, bits } => {
                        node[0] = 1;
                        node[1] = u8::from(*signed);
                        node[2] = *bits;
                    }
                    Kind::Bool => node[0] = 2,
                    Kind::Pointer => node[0] = 3,
                    Kind::Enum {
                        signed,
                        bits,
                        variants,
                    } => {
                        node[0] = 4;
                        node[1] = u8::from(*signed);
                        node[2] = *bits;
                        for (name, value) in variants {
                            let mut member = [0; 24];
                            wide(&mut member, 0, *value as u64);
                            word(&mut member, 8, u32::MAX);
                            self.name(&mut member, name)?;
                            members.push(member);
                        }
                    }
                    Kind::Record(fields) => {
                        node[0] = 5;
                        let mut ordered = fields.iter().collect::<Vec<_>>();
                        ordered.sort_by_key(|(_, offset, _)| *offset);
                        for (name, offset, shape) in ordered {
                            let child = self.shape(shape)?;
                            let mut member = [0; 24];
                            wide(&mut member, 0, *offset);
                            word(&mut member, 8, child);
                            self.name(&mut member, name)?;
                            members.push(member);
                        }
                    }
                    Kind::Array(element, length) => {
                        node[0] = 6;
                        word(&mut node, 16, self.shape(element)?);
                        wide(&mut node, 32, *length);
                    }
                }
                if !members.is_empty() {
                    word(&mut node, 20, number(self.members.len())?);
                    word(&mut node, 24, number(members.len())?);
                    self.members.extend(members);
                }
                let index = number(self.nodes.len())?;
                self.nodes.push(node);
                Ok(index)
            }
        }
        let mut writer = Writer {
            nodes: vec![],
            members: vec![],
            names: vec![],
        };
        let mut slots = Vec::new();
        for (i, shape) in self.arguments.iter().chain(self.result.iter()).enumerate() {
            let index = writer.shape(shape)?;
            let mut slot = [0; 24];
            wide(&mut slot, 0, self.offsets[i]);
            word(&mut slot, 8, index);
            let (role, name) = if i < self.parameter_count {
                (0, format!("arg{i}"))
            } else if i < self.arguments.len() {
                (1, self.bindings[i - self.parameter_count].clone())
            } else {
                (2, "result".into())
            };
            writer.name(&mut slot, &name)?;
            word(&mut slot, 20, role);
            slots.push(slot);
        }
        // Names occupy one canonical non-overlapping string region in member/slot order.
        let mut names = Vec::new();
        for record in writer.members.iter_mut().chain(slots.iter_mut()) {
            let offset = u32::from_le_bytes(record[12..16].try_into().unwrap()) as usize;
            let len = u32::from_le_bytes(record[16..20].try_into().unwrap()) as usize;
            word(record, 12, number(names.len())?);
            names.extend_from_slice(&writer.names[offset..offset + len]);
        }
        writer.names = names;
        let mut out = vec![0; 64];
        out[..8].copy_from_slice(b"FGFRAME\0");
        word(&mut out, 8, 1);
        wide(&mut out, 16, self.size);
        word(&mut out, 24, self.align as u32);
        word(&mut out, 28, number(self.parameter_count)?);
        word(&mut out, 32, number(self.bindings.len())?);
        word(&mut out, 36, u32::from(self.result.is_some()));
        word(&mut out, 40, number(writer.nodes.len())?);
        word(&mut out, 44, number(writer.members.len())?);
        word(&mut out, 48, number(writer.names.len())?);
        word(&mut out, 56, number(slots.len())?);
        for node in writer.nodes {
            out.extend_from_slice(&node);
        }
        for member in writer.members {
            out.extend_from_slice(&member);
        }
        for slot in slots {
            out.extend_from_slice(&slot);
        }
        out.extend(writer.names);
        let size = number(out.len())?;
        word(&mut out, 12, size);
        crate::frame_schema::validate(&out)?;
        Ok(out)
    }
}
