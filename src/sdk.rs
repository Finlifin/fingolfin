//! Layout-matched native SDK declarations. Native callbacks and their context
//! are trusted; these types do not provide a sandbox or memory reclamation.
#[cfg(not(all(target_os = "linux", target_arch = "x86_64", target_endian = "little")))]
compile_error!("Fingolfin SDK v1 requires Linux x86-64 little-endian");
use std::ffi::c_void;
pub const ABI_VERSION: u32 = 1;
pub const OK: i32 = 0;
pub const ERROR_ABI: i32 = -1;
pub const ERROR_FRAME: i32 = -2;

#[repr(C)]
#[derive(Debug, Default, Clone, Copy, PartialEq, Eq, PartialOrd, Ord)]
pub struct ObjectKey {
    pub run: u64,
    pub launch: u64,
    pub slot: u64,
    pub generation: u64,
}
#[repr(C)]
#[derive(Debug, Default, Clone, Copy, PartialEq, Eq)]
pub struct CallHeader {
    pub abi_version: u32,
    pub frame_bytes: u32,
    pub hook_id: u64,
    pub config_generation: u64,
    pub object: ObjectKey,
}
pub type EnabledFn = unsafe extern "C" fn(*mut c_void, u64, *const ObjectKey) -> u8;
pub type DispatchFn = unsafe extern "C" fn(*mut c_void, *const CallHeader) -> i32;
#[repr(C)]
#[derive(Clone, Copy)]
pub struct RuntimeV1 {
    pub abi_version: u32,
    pub struct_bytes: u32,
    pub context: *mut c_void,
    pub enabled: Option<EnabledFn>,
    pub dispatch: Option<DispatchFn>,
}
impl RuntimeV1 {
    pub fn is_compatible(&self) -> bool {
        self.abi_version == ABI_VERSION
            && self.struct_bytes as usize >= size_of::<Self>()
            && self.enabled.is_some()
            && self.dispatch.is_some()
    }
}
impl CallHeader {
    pub fn is_compatible(&self) -> bool {
        self.abi_version == ABI_VERSION && self.frame_bytes as usize >= size_of::<Self>()
    }
}
