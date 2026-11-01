//! Optional pinned uBPF safe-interpreter bridge to the actual Rust Host.
use crate::{
    intrinsic::{Host, HostError, Intrinsic},
    package::Entry,
    util::{elf, hash::sha256},
};
use std::{
    ffi::{CStr, c_char, c_void},
    marker::PhantomData,
    ptr::NonNull,
    rc::Rc,
};
type Callback = unsafe extern "C" fn(*mut c_void, u32, *const u64) -> u64;
unsafe extern "C" {
    fn fg_vm_create(
        elf: *const u8,
        len: usize,
        limit: u32,
        capacity: usize,
        error: *mut c_char,
        error_len: usize,
    ) -> *mut c_void;
    fn fg_vm_destroy(vm: *mut c_void);
    fn fg_vm_execute(
        vm: *mut c_void,
        frame: *mut u8,
        len: usize,
        callback: Callback,
        user: *mut c_void,
        result: *mut u64,
    ) -> i32;
}
/// Owns a VM and aligned, preallocated invocation storage. Not Send or Sync.
pub struct Vm {
    raw: NonNull<c_void>,
    capacity: usize,
    _local: PhantomData<Rc<()>>,
}
#[derive(Debug, PartialEq, Eq)]
pub enum Error {
    FrameCapacity,
    Runtime(i32),
    Host(HostError),
    CallbackPanicked,
}
struct Invocation<'a> {
    host: &'a mut Host,
    error: Option<Error>,
}
unsafe extern "C" fn invoke(user: *mut c_void, id: u32, args: *const u64) -> u64 {
    // C calls synchronously with a live Invocation and a five-word argument array.
    let context = unsafe { &mut *user.cast::<Invocation<'_>>() };
    if context.error.is_some() {
        return 1;
    }
    let words = unsafe { *args.cast::<[u64; 5]>() };
    let result = std::panic::catch_unwind(std::panic::AssertUnwindSafe(|| {
        let intrinsic = match id {
            1 => Intrinsic::CounterAdd,
            2 => Intrinsic::Emit,
            3 => Intrinsic::MapAdd,
            4 => Intrinsic::HistogramAdd,
            _ => return Err(HostError::InvalidArguments),
        };
        context.host.invoke_words(intrinsic, words)
    }));
    context.error = match result {
        Ok(Ok(())) => None,
        Ok(Err(e)) => Some(Error::Host(e)),
        Err(_) => Some(Error::CallbackPanicked),
    };
    u64::from(context.error.is_some())
}
impl Vm {
    pub fn load(entry: &Entry, instruction_limit: u32, max_frame: usize) -> Result<Self, String> {
        if instruction_limit == 0 || max_frame == 0 {
            return Err("instruction limit and frame capacity must be positive".into());
        }
        if sha256(&entry.elf) != entry.elf_hash
            || sha256(&entry.frame_schema) != entry.frame_schema_hash
            || sha256(entry.frame_header.as_bytes()) != entry.frame_hash
            || sha256(&serde_json::to_vec(&entry.signature).map_err(|e| e.to_string())?)
                != entry.signature_hash
        {
            return Err("entry integrity mismatch".into());
        }
        crate::frame_schema::validate(&entry.frame_schema)?;
        elf::validate_bpf(&entry.elf)?;
        if elf::helper_ids(&entry.elf)? != entry.helpers
            || entry.helpers.iter().any(|id| !(1..=4).contains(id))
        {
            return Err("helper allowlist mismatch".into());
        }
        let mut error = [0 as c_char; 1024];
        // C copies/loads ELF and owns its VM; no Rust references are retained.
        let raw = unsafe {
            fg_vm_create(
                entry.elf.as_ptr(),
                entry.elf.len(),
                instruction_limit,
                max_frame,
                error.as_mut_ptr(),
                error.len(),
            )
        };
        let raw = NonNull::new(raw).ok_or_else(|| {
            unsafe { CStr::from_ptr(error.as_ptr()) }
                .to_string_lossy()
                .into_owned()
        })?;
        Ok(Self {
            raw,
            capacity: max_frame,
            _local: PhantomData,
        })
    }
    /// Returns generated entry status (zero is success), or a VM/host failure.
    /// Mutated frame and earlier successful helper operations survive errors.
    pub fn execute(&mut self, frame: &mut [u8], host: &mut Host) -> Result<u64, Error> {
        if frame.len() > self.capacity {
            return Err(Error::FrameCapacity);
        }
        let mut context = Invocation { host, error: None };
        let mut result = 0;
        // Exclusive access prevents concurrent execution; callback context lives until return.
        let status = unsafe {
            fg_vm_execute(
                self.raw.as_ptr(),
                frame.as_mut_ptr(),
                frame.len(),
                invoke,
                (&mut context as *mut Invocation<'_>).cast(),
                &mut result,
            )
        };
        if let Some(error) = context.error {
            return Err(error);
        }
        if status != 0 {
            return Err(Error::Runtime(status));
        }
        Ok(result)
    }
}
impl Drop for Vm {
    fn drop(&mut self) {
        unsafe { fg_vm_destroy(self.raw.as_ptr()) }
    }
}

/// Source-checked development function adapter. Hook matching remains the caller's duty.
pub struct Function {
    vm: Vm,
    frame: crate::frame::Frame,
}
#[derive(Debug, PartialEq, Eq)]
pub enum CallError {
    Arguments(String),
    Execution(Error),
    Entry(u64),
    Result(String),
}
#[derive(Debug, PartialEq, Eq)]
pub struct Returned {
    pub value: Option<crate::pattern::Value>,
    pub remaining: u64,
}
impl Function {
    /// Requires the checked source/model that produced the frame ABI.
    pub fn load(
        program: &crate::compiler::CheckedProgram,
        entry: &Entry,
        instruction_limit: u32,
        max_frame: usize,
    ) -> Result<Self, String> {
        let generated = crate::codegen::generate_bpf(program, &entry.function)?;
        if generated.header != entry.frame_header {
            return Err("entry frame does not match checked program".into());
        }
        let layout = crate::frame::Layout::for_function(program, &entry.function)?;
        let frame = layout.allocate(max_frame)?;
        let vm = Vm::load(entry, instruction_limit, frame.bytes().len())?;
        Ok(Self { vm, frame })
    }
    pub fn call(
        &mut self,
        args: &[crate::pattern::Value],
        bindings: &std::collections::BTreeMap<String, crate::pattern::Value>,
        steps: u64,
        max_depth: u32,
        host: &mut Host,
    ) -> Result<Returned, CallError> {
        self.frame
            .prepare(args, bindings, steps, max_depth)
            .map_err(CallError::Arguments)?;
        let status = self
            .vm
            .execute(self.frame.bytes_mut(), host)
            .map_err(CallError::Execution)?;
        if status != 0 {
            return Err(CallError::Entry(status));
        }
        Ok(Returned {
            value: self.frame.result().map_err(CallError::Result)?,
            remaining: self.frame.remaining(),
        })
    }
}
