#![cfg(all(target_os = "linux", target_arch = "x86_64", target_endian = "little"))]
use fingolfin::sdk::*;
use std::{
    ffi::{CString, c_char, c_void},
    path::PathBuf,
    process::Command,
};
unsafe extern "C" {
    fn dlopen(file: *const c_char, flags: i32) -> *mut c_void;
    fn dlsym(handle: *mut c_void, name: *const c_char) -> *mut c_void;
    fn dlclose(handle: *mut c_void) -> i32;
    fn dlerror() -> *const c_char;
}
struct Library(*mut c_void);
impl Library {
    fn open(path: &std::path::Path) -> Self {
        let name = CString::new(path.as_os_str().as_encoded_bytes()).unwrap();
        let handle = unsafe { dlopen(name.as_ptr(), 2) };
        if handle.is_null() {
            let message = unsafe { std::ffi::CStr::from_ptr(dlerror()) };
            panic!("dlopen: {message:?}");
        }
        Self(handle)
    }
    fn symbol(&self, name: &str) -> *mut c_void {
        let name = CString::new(name).unwrap();
        let pointer = unsafe { dlsym(self.0, name.as_ptr()) };
        assert!(!pointer.is_null(), "missing symbol {name:?}");
        pointer
    }
}
impl Drop for Library {
    fn drop(&mut self) {
        unsafe {
            dlclose(self.0);
        }
    }
}
struct Temp(PathBuf);
impl Drop for Temp {
    fn drop(&mut self) {
        std::fs::remove_dir_all(&self.0).unwrap();
    }
}
struct State {
    enabled: u8,
    enables: u32,
    dispatches: u32,
    object: ObjectKey,
}
unsafe extern "C" fn enabled(context: *mut c_void, hook: u64, key: *const ObjectKey) -> u8 {
    let state = unsafe { &mut *context.cast::<State>() };
    state.enables += 1;
    if hook != 42 || unsafe { *key } != state.object {
        return 0;
    }
    state.enabled
}
unsafe extern "C" fn dispatch(context: *mut c_void, frame: *const CallHeader) -> i32 {
    let state = unsafe { &mut *context.cast::<State>() };
    let frame = unsafe { &*frame };
    if frame.frame_bytes as usize != size_of::<CallHeader>() {
        return ERROR_FRAME;
    }
    if frame.hook_id != 42 || frame.object != state.object || frame.config_generation != 3 {
        return -99;
    }
    state.dispatches += 1;
    OK
}
unsafe extern "C" fn callback(mut key: ObjectKey, increment: u32) -> ObjectKey {
    key.launch += increment as u64;
    key.generation += 2;
    key
}
#[test]
fn c_cpp_rust_layout_calling_convention_and_guard() {
    let root = Temp(std::env::temp_dir().join(format!("fg-sdk-abi-{}", std::process::id())));
    std::fs::create_dir_all(&root.0).unwrap();
    for (compiler, language, standard) in [("clang", "c", "c11"), ("clang++", "c++", "c++20")] {
        let library = root.0.join(format!("probe-{language}.so"));
        let output = Command::new(compiler)
            .args([
                "-shared", "-fPIC", "-Wall", "-Wextra", "-Werror", "-x", language,
            ])
            .arg(format!("-std={standard}"))
            .args(["-I", "sdk/include", "sdk/tests/abi_probe.c", "-o"])
            .arg(&library)
            .output()
            .unwrap();
        assert!(
            output.status.success(),
            "{compiler}: {}",
            String::from_utf8_lossy(&output.stderr)
        );
        let lib = Library::open(&library);
        let layout: unsafe extern "C" fn(u32) -> u64 =
            unsafe { std::mem::transmute(lib.symbol("fg_probe_layout")) };
        let expected = [
            size_of::<ObjectKey>(),
            align_of::<ObjectKey>(),
            size_of::<CallHeader>(),
            align_of::<CallHeader>(),
            std::mem::offset_of!(CallHeader, object),
            size_of::<RuntimeV1>(),
            std::mem::offset_of!(RuntimeV1, context),
            std::mem::offset_of!(RuntimeV1, enabled),
            std::mem::offset_of!(RuntimeV1, dispatch),
        ];
        for (field, value) in expected.iter().enumerate() {
            assert_eq!(
                unsafe { layout(field as u32) },
                *value as u64,
                "{language} field {field}"
            );
        }
        let roundtrip: unsafe extern "C" fn(ObjectKey, u32) -> ObjectKey =
            unsafe { std::mem::transmute(lib.symbol("fg_probe_roundtrip")) };
        let call: unsafe extern "C" fn(
            ObjectKey,
            unsafe extern "C" fn(ObjectKey, u32) -> ObjectKey,
        ) -> ObjectKey = unsafe { std::mem::transmute(lib.symbol("fg_probe_callback")) };
        let key = ObjectKey {
            run: 0x0123456789abcdef,
            launch: 11,
            slot: 23,
            generation: 41,
        };
        assert_eq!(
            unsafe { roundtrip(key, 5) },
            ObjectKey {
                slot: 28,
                generation: 42,
                ..key
            }
        );
        assert_eq!(
            unsafe { call(key, callback) },
            ObjectKey {
                launch: 18,
                generation: 43,
                ..key
            }
        );
        let guard: unsafe extern "C" fn(*const RuntimeV1, *const ObjectKey) -> i32 =
            unsafe { std::mem::transmute(lib.symbol("fg_probe_guard")) };
        let direct: unsafe extern "C" fn(*const RuntimeV1, *const CallHeader) -> i32 =
            unsafe { std::mem::transmute(lib.symbol("fg_probe_dispatch")) };
        let mut state = State {
            enabled: 0,
            enables: 0,
            dispatches: 0,
            object: key,
        };
        let mut runtime = RuntimeV1 {
            abi_version: ABI_VERSION,
            struct_bytes: size_of::<RuntimeV1>() as u32,
            context: (&mut state as *mut State).cast(),
            enabled: Some(enabled),
            dispatch: Some(dispatch),
        };
        assert!(runtime.is_compatible());
        assert_eq!(unsafe { guard(&runtime, &key) }, 0);
        assert_eq!((state.enables, state.dispatches), (1, 0));
        state.enabled = 1;
        assert_eq!(unsafe { guard(&runtime, &key) }, 1);
        assert_eq!((state.enables, state.dispatches), (2, 1));
        state.enabled = 2; // Invalid wire bool must not execute the frame expression.
        assert_eq!(unsafe { guard(&runtime, &key) }, 0);
        runtime.abi_version = 999;
        assert!(!runtime.is_compatible());
        assert_eq!(unsafe { guard(&runtime, &key) }, 0);
        assert_eq!(state.enables, 3);
        assert_eq!(unsafe { direct(&runtime, std::ptr::null()) }, ERROR_ABI);
        runtime.abi_version = ABI_VERSION;
        assert_eq!(unsafe { direct(&runtime, std::ptr::null()) }, ERROR_FRAME);
        let mut frame = CallHeader {
            abi_version: ABI_VERSION,
            frame_bytes: size_of::<CallHeader>() as u32,
            hook_id: 42,
            config_generation: 3,
            object: key,
        };
        let dispatch_at: unsafe extern "C" fn(
            *const RuntimeV1,
            *const ObjectKey,
            *const CallHeader,
        ) -> i32 = unsafe { std::mem::transmute(lib.symbol("fg_probe_dispatch_at")) };
        let wrong_key = ObjectKey {
            generation: key.generation + 1,
            ..key
        };
        assert_eq!(
            unsafe { dispatch_at(&runtime, &wrong_key, &frame) },
            ERROR_FRAME
        );
        let oversized = CallHeader {
            frame_bytes: frame.frame_bytes + 1,
            ..frame
        };
        assert_eq!(unsafe { direct(&runtime, &oversized) }, ERROR_FRAME);
        assert!(frame.is_compatible());
        assert_eq!(unsafe { direct(&runtime, &frame) }, OK);
        frame.frame_bytes -= 1;
        assert!(!frame.is_compatible());
        assert_eq!(unsafe { direct(&runtime, &frame) }, ERROR_FRAME);
        runtime.dispatch = None;
        assert!(!runtime.is_compatible());
        assert_eq!(unsafe { direct(&runtime, &frame) }, ERROR_ABI);
    }
}
