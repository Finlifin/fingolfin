use fingolfin::{codegen, compiler, source};
use std::{fs, path::Path, process::Command};

#[test]
fn generated_c_adapter_covers_scalar_record_array_pointer_and_empty_frames() {
    let model = source::load(Path::new("models/fixture")).unwrap();
    let dir = std::env::temp_dir().join(format!("fg-adapter-test-{}", std::process::id()));
    fs::create_dir_all(&dir).unwrap();
    for (i, text) in [
        "fn f(){}",
        "fn f(a:u8,b:i64,c:bool,p:*u8)->i64{return b;}",
        "enum E:i8 {low=-128, high=127} struct Inner {z:u8,a:i64} struct S {z:bool,inner:Inner,e:E,values:[u32;3]} fn f(s:S)->S{return s;}",
    ].iter().enumerate() {
        let program = compiler::check_source("adapter.fg", text, &model).unwrap();
        let header = codegen::generate_adapter(&program, "program::f").unwrap();
        assert_eq!(header, codegen::generate_adapter(&program, "program::f").unwrap());
        assert!(codegen::generate_adapter(&program, "program::missing").is_err());
        let path = dir.join(format!("adapter{i}.h"));
        fs::write(&path, header).unwrap();
        let unit = dir.join(format!("adapter{i}.c"));
        fs::write(&unit, format!("#include \"adapter{i}.h\"\n")).unwrap();
        for compiler in ["clang", "gcc"] {
        let result = Command::new(compiler).args([
            "-std=c11", "-Wall", "-Wextra", "-Werror", "-pedantic", "-x", "c", "-fsyntax-only",
            "-I", "runtime/include", "-I", "sdk/include",
        ]).arg(&unit).output().unwrap();
        assert!(result.status.success(), "{}: {}", compiler, String::from_utf8_lossy(&result.stderr));
        }
    }
    let array = compiler::check_source("adapter.fg", "fn f(a:[u32;3]){}", &model).unwrap();
    assert!(
        codegen::generate_adapter(&array, "program::f")
            .unwrap_err()
            .contains("struct wrapper")
    );
    fs::remove_dir_all(dir).unwrap();
}

#[test]
fn generated_adapter_rejects_nested_invalid_values_with_bounded_c_validation() {
    use fingolfin::{abi, frame::Layout};
    let model = source::load(Path::new("models/fixture")).unwrap();
    let program = compiler::check_source(
        "values.fg",
        "enum E:i8 {low=-128, high=127} struct S {flags:[bool;2],e:E} fn f(s:S)->E{return s.e;}",
        &model,
    )
    .unwrap();
    let dir = std::env::temp_dir().join(format!("fg-adapter-values-{}", std::process::id()));
    fs::create_dir_all(&dir).unwrap();
    fs::write(
        dir.join("adapter.h"),
        codegen::generate_adapter(&program, "program::f").unwrap(),
    )
    .unwrap();
    let schema = Layout::for_function(&program, "program::f")
        .unwrap()
        .schema_bytes()
        .unwrap();
    let bytes = schema
        .iter()
        .map(u8::to_string)
        .collect::<Vec<_>>()
        .join(",");
    let code = format!(
        r#"
#include "adapter.h"
#include <assert.h>
static const unsigned char schema_bytes[]={{{bytes}}};
int main(void) {{
 FgFrameSchema schema; assert(fg_frame_schema_check((FgBytes){{schema_bytes,sizeof(schema_bytes)}},&schema,NULL,0)==FG_STATUS_OK);
 struct FgNativeFrame native={{.header={{.abi_version=FG_ABI_VERSION,.frame_bytes=sizeof(native)}}}};
 struct FgBpfFrame output;
 native.arg0.{e}=-128;
 for(unsigned i=0;i<256;i++) {{
   native.arg0.{flags}[1]=(uint8_t)i;
   FgStatus status=fg_generated_encode(&native.header,&schema,&output,sizeof(output));
   assert(status==(i<=1?FG_STATUS_OK:FG_STATUS_INVALID));
 }}
 native.arg0.{flags}[1]=1;
 for(unsigned i=0;i<256;i++) {{
   uint8_t repr=(uint8_t)i; memcpy(&native.arg0.{e},&repr,1);
   FgStatus status=fg_generated_encode(&native.header,&schema,&output,sizeof(output));
   assert(status==((i==128||i==127)?FG_STATUS_OK:FG_STATUS_INVALID));
 }}
 native.arg0.{e}=127;
 assert(fg_generated_encode(&native.header,&schema,&output,sizeof(output))==FG_STATUS_OK);
 output.result=0; /* Invalid enum result is intentionally not an input. */
 assert(fg_frame_schema_validate_inputs(&schema,(FgBytes){{(unsigned char *)&output,sizeof(output)}},100)==FG_STATUS_OK);
 assert(fg_frame_schema_validate_inputs(&schema,(FgBytes){{(unsigned char *)&output,sizeof(output)}},0)==FG_STATUS_BUDGET);
 assert(fg_frame_schema_validate_inputs(&schema,(FgBytes){{(unsigned char *)&output,sizeof(output)-1}},100)==FG_STATUS_INVALID);
 return 0;
}}
"#,
        e = abi::identifier("e"),
        flags = abi::identifier("flags")
    );
    fs::write(dir.join("values.c"), code).unwrap();
    let exe = dir.join("values");
    let built = Command::new("clang")
        .args([
            "-std=c11",
            "-Wall",
            "-Wextra",
            "-Werror",
            "-fsanitize=undefined",
            "-fno-sanitize-recover=all",
            "-I",
            "runtime/include",
            "-I",
            "sdk/include",
        ])
        .arg(dir.join("values.c"))
        .args([
            "runtime/src/schema.c",
            "runtime/src/bytes.c",
            "runtime/src/util.c",
            "-o",
        ])
        .arg(&exe)
        .output()
        .unwrap();
    assert!(
        built.status.success(),
        "{}",
        String::from_utf8_lossy(&built.stderr)
    );
    assert!(Command::new(&exe).status().unwrap().success());
    fs::remove_dir_all(dir).unwrap();
}
