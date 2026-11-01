use std::process::Command;
#[test]
fn parse_and_inspect_emit_machine_readable_ast() {
    for args in [
        ["parse", "examples/fixture/cb_watch.fg"],
        ["inspect", "models/fixture"],
    ] {
        let out = Command::new(env!("CARGO_BIN_EXE_fingolfin"))
            .args(args)
            .output()
            .unwrap();
        assert!(
            out.status.success(),
            "{}",
            String::from_utf8_lossy(&out.stderr)
        );
        let value: serde_json::Value = serde_json::from_slice(&out.stdout).unwrap();
        if args[0] == "parse" {
            assert_eq!(value["items"].as_array().unwrap().len(), 3);
        } else {
            assert_eq!(value["modules"].as_object().unwrap().len(), 4);
        }
    }
}
#[test]
fn missing_file_and_unknown_command_fail() {
    for args in [
        ["parse", "does-not-exist.fg"],
        ["build", "examples/fixture/cb_watch.fg"],
    ] {
        let out = Command::new(env!("CARGO_BIN_EXE_fingolfin"))
            .args(args)
            .output()
            .unwrap();
        assert!(!out.status.success());
        assert!(out.stdout.is_empty());
        assert!(!out.stderr.is_empty());
    }
}

#[test]
fn inspect_types_checks_fixture_declarations() {
    let out = Command::new(env!("CARGO_BIN_EXE_fingolfin"))
        .args(["inspect", "models/fixture", "--types"])
        .output()
        .unwrap();
    assert!(
        out.status.success(),
        "{}",
        String::from_utf8_lossy(&out.stderr)
    );
    let value: serde_json::Value = serde_json::from_slice(&out.stdout).unwrap();
    assert_eq!(value["validation"], "declarations-only");
    assert_eq!(
        value["types"]["layouts"]["fixture::types.CbHandle"]["size"],
        16
    );
}

#[test]
fn inspect_bodies_checks_real_function_errors() {
    let root = std::env::temp_dir().join(format!("fg-cli-bodies-{}", std::process::id()));
    std::fs::create_dir_all(root.join("src")).unwrap();
    std::fs::write(
        root.join("Fingolfin.toml"),
        "[package]\nname='body_test'\nversion='0.1.0'\n",
    )
    .unwrap();
    for (source, success) in [
        ("fn f(x: u8) -> u8 { return x+1; }", true),
        ("fn f(x: u8) -> u8 { return true; }", false),
    ] {
        std::fs::write(root.join("src/lib.fg"), source).unwrap();
        let out = Command::new(env!("CARGO_BIN_EXE_fingolfin"))
            .arg("inspect")
            .arg(&root)
            .arg("--bodies")
            .output()
            .unwrap();
        assert_eq!(
            out.status.success(),
            success,
            "{}",
            String::from_utf8_lossy(&out.stderr)
        );
        if success {
            let value: serde_json::Value = serde_json::from_slice(&out.stdout).unwrap();
            assert_eq!(value["validation"], "bodies-and-fixed-layout-patterns");
        } else {
            assert!(String::from_utf8_lossy(&out.stderr).contains("type mismatch"));
        }
    }
    std::fs::remove_dir_all(root).unwrap();
}

#[test]
fn check_program_and_enforce_exact_model_pin() {
    let args = [
        "check",
        "tests/fixtures/valid_observation.fg",
        "--model",
        "models/fixture",
    ];
    let output = Command::new(env!("CARGO_BIN_EXE_fingolfin"))
        .args(args)
        .output()
        .unwrap();
    assert!(
        output.status.success(),
        "{}",
        String::from_utf8_lossy(&output.stderr)
    );
    let value: serde_json::Value = serde_json::from_slice(&output.stdout).unwrap();
    assert_eq!(value["entries"].as_array().unwrap().len(), 2);
    let hash = value["model_source_hash"].as_str().unwrap();
    for (pin, success) in [(hash, true), ("wrong-hash", false)] {
        let output = Command::new(env!("CARGO_BIN_EXE_fingolfin"))
            .args(args)
            .args(["--model-hash", pin])
            .output()
            .unwrap();
        assert_eq!(output.status.success(), success);
        if !success {
            assert!(output.stdout.is_empty());
            assert!(String::from_utf8_lossy(&output.stderr).contains("required pin"));
        }
    }
}
#[test]
fn check_does_not_invent_an_output_helper() {
    let out = Command::new(env!("CARGO_BIN_EXE_fingolfin"))
        .args([
            "check",
            "examples/fixture/cb_watch.fg",
            "--model",
            "models/fixture",
        ])
        .output()
        .unwrap();
    assert!(!out.status.success());
    assert!(String::from_utf8_lossy(&out.stderr).contains("emit_pages"));
}

#[test]
fn abi_command_writes_compilable_model_header() {
    let path = std::env::temp_dir().join(format!("fg-cli-abi-{}.h", std::process::id()));
    let output = Command::new(env!("CARGO_BIN_EXE_fingolfin"))
        .args(["abi", "--model", "models/fixture", "--output"])
        .arg(&path)
        .output()
        .unwrap();
    assert!(
        output.status.success(),
        "{}",
        String::from_utf8_lossy(&output.stderr)
    );
    let metadata: serde_json::Value = serde_json::from_slice(&output.stdout).unwrap();
    assert!(
        metadata["symbols"]["fixture::types.CbHandle"]
            .as_str()
            .unwrap()
            .starts_with("fg_")
    );
    let output = Command::new("clang")
        .args(["-x", "c-header", "-std=c11", "-fsyntax-only"])
        .arg(&path)
        .output()
        .unwrap();
    assert!(
        output.status.success(),
        "{}",
        String::from_utf8_lossy(&output.stderr)
    );
    std::fs::remove_file(&path).unwrap();
    let rust_path = path.with_extension("rs");
    let output = Command::new(env!("CARGO_BIN_EXE_fingolfin"))
        .args(["abi", "--model", "models/fixture", "--rust", "--output"])
        .arg(&rust_path)
        .output()
        .unwrap();
    assert!(
        output.status.success(),
        "{}",
        String::from_utf8_lossy(&output.stderr)
    );
    let metadata_path = path.with_extension("rmeta");
    let output = Command::new("rustc")
        .args([
            "--edition=2024",
            "--crate-name",
            "generated_cli_abi",
            "--crate-type=lib",
            "--emit=metadata",
        ])
        .arg(&rust_path)
        .arg("-o")
        .arg(&metadata_path)
        .output()
        .unwrap();
    assert!(
        output.status.success(),
        "{}",
        String::from_utf8_lossy(&output.stderr)
    );
    std::fs::remove_file(rust_path).unwrap();
    std::fs::remove_file(metadata_path).unwrap();
}

#[test]
fn reference_eval_cli_executes_and_reports_budget_failures() {
    let args = [
        "eval",
        "tests/fixtures/valid_observation.fg",
        "--model",
        "models/fixture",
        "--function",
        "program::increment",
        "--args",
        "[{\"Integer\":41}]",
    ];
    let out = Command::new(env!("CARGO_BIN_EXE_fingolfin"))
        .args(args)
        .output()
        .unwrap();
    assert!(
        out.status.success(),
        "{}",
        String::from_utf8_lossy(&out.stderr)
    );
    let value: serde_json::Value = serde_json::from_slice(&out.stdout).unwrap();
    assert_eq!(value["result"]["Integer"], 42);
    assert_eq!(value["engine"], "reference");
    let failed = Command::new(env!("CARGO_BIN_EXE_fingolfin"))
        .args(args)
        .args(["--steps", "1"])
        .output()
        .unwrap();
    assert!(!failed.status.success());
    assert!(String::from_utf8_lossy(&failed.stderr).contains("BudgetExceeded"));
}

#[test]
fn dispatch_cli_executes_fixture_entry_with_pattern_binding() {
    let out = Command::new(env!("CARGO_BIN_EXE_fingolfin")).args([
        "dispatch","tests/fixtures/valid_observation.fg","--model","models/fixture",
        "--hook","fixture.device.core.cb.post_push","--snapshot","tests/fixtures/cb_snapshot.json",
        "--args",r#"[{"Pointer":0},{"Record":{"slot":{"Integer":1},"generation":{"Integer":1}}},{"Integer":2}]"#
    ]).output().unwrap();
    assert!(
        out.status.success(),
        "{}",
        String::from_utf8_lossy(&out.stderr)
    );
    let value: serde_json::Value = serde_json::from_slice(&out.stdout).unwrap();
    assert_eq!(value["result"]["incomplete"], false);
    let rows = value["result"]["invocations"].as_array().unwrap();
    assert_eq!(rows.len(), 1);
    assert_eq!(rows[0]["entry"], "program::push");
    assert_eq!(rows[0]["bindings"]["cb_id"]["Integer"], 16);
}

#[test]
fn ir_cli_emits_typed_functions() {
    let out = Command::new(env!("CARGO_BIN_EXE_fingolfin"))
        .args([
            "ir",
            "tests/fixtures/valid_observation.fg",
            "--model",
            "models/fixture",
        ])
        .output()
        .unwrap();
    assert!(
        out.status.success(),
        "{}",
        String::from_utf8_lossy(&out.stderr)
    );
    let value: serde_json::Value = serde_json::from_slice(&out.stdout).unwrap();
    assert_eq!(value["version"], 1);
    assert!(value["functions"]["program::push"]["bindings"]["cb_id"].is_number());
    assert_eq!(
        value["functions"]["program::increment"]["returns"]["Integer"]["bits"],
        32
    );
}

#[test]
fn emit_c_cli_generates_compilable_function_bodies() {
    let dir = std::env::temp_dir().join(format!("fg-emit-cli-{}", std::process::id()));
    std::fs::create_dir_all(&dir).unwrap();
    let source = dir.join("generated.c");
    let out = Command::new(env!("CARGO_BIN_EXE_fingolfin"))
        .args([
            "emit-c",
            "tests/fixtures/valid_observation.fg",
            "--model",
            "models/fixture",
            "--output",
        ])
        .arg(&source)
        .output()
        .unwrap();
    assert!(
        out.status.success(),
        "{}",
        String::from_utf8_lossy(&out.stderr)
    );
    let value: serde_json::Value = serde_json::from_slice(&out.stdout).unwrap();
    assert_eq!(value["backend"], "native-c11");
    let compiled = Command::new("clang")
        .args(["-std=c11", "-Wall", "-Wextra", "-Werror", "-c"])
        .arg(&source)
        .arg("-o")
        .arg(dir.join("generated.o"))
        .output()
        .unwrap();
    assert!(
        compiled.status.success(),
        "{}",
        String::from_utf8_lossy(&compiled.stderr)
    );
    std::fs::remove_dir_all(dir).unwrap();
}

#[test]
fn eval_cli_attaches_explicit_bounded_counter_and_output() {
    let out = Command::new(env!("CARGO_BIN_EXE_fingolfin"))
        .args([
            "eval",
            "integration/state-helpers/record.fg",
            "--model",
            "models/fixture",
            "--function",
            "program::record",
            "--args",
            r#"[{"Integer":0},{"Integer":9}]"#,
            "--counter",
            "0",
            "--output-capacity",
            "2",
        ])
        .output()
        .unwrap();
    assert!(
        out.status.success(),
        "{}",
        String::from_utf8_lossy(&out.stderr)
    );
    let value: serde_json::Value = serde_json::from_slice(&out.stdout).unwrap();
    assert_eq!(value["state"]["counters"]["0"], 9);
    assert_eq!(
        value["state"]["records"][0],
        serde_json::json!({"tag":7,"value":9})
    );
}

#[test]
fn eval_cli_preallocates_map_and_histogram_from_host_configuration() {
    let out = Command::new(env!("CARGO_BIN_EXE_fingolfin"))
        .args([
            "eval",
            "integration/aggregate-helpers/aggregate.fg",
            "--model",
            "models/fixture",
            "--function",
            "program::aggregate",
            "--args",
            r#"[{"Integer":7},{"Integer":-1},{"Integer":2}]"#,
            "--host-config",
            "integration/aggregate-helpers/config.json",
        ])
        .output()
        .unwrap();
    assert!(
        out.status.success(),
        "{}",
        String::from_utf8_lossy(&out.stderr)
    );
    let value: serde_json::Value = serde_json::from_slice(&out.stdout).unwrap();
    assert_eq!(value["state"]["maps"]["0"]["7"], 2);
    assert_eq!(
        value["state"]["histograms"]["0"]["counts"],
        serde_json::json!([2, 0, 0])
    );
}

#[test]
fn invalid_property_contract_is_rejected_before_publishing_artifacts() {
    let root = std::env::temp_dir().join(format!("fg-cli-contract-{}", std::process::id()));
    std::fs::create_dir_all(root.join("src")).unwrap();
    std::fs::write(
        root.join("src/lib.fg"),
        "pub object fixture { id:u32 }\npub hook fixture.tick(v:u32);",
    )
    .unwrap();
    let program = root.join("watch.fg");
    std::fs::write(
        &program,
        "fn watch on fixture.tick(v:u32) where fixture:{id:0} {}",
    )
    .unwrap();
    let artifact = root.join("out");
    let bin = env!("CARGO_BIN_EXE_fingolfin");
    for fields in ["['missing']", "['id','x']"] {
        std::fs::write(root.join("Fingolfin.toml"),format!("[package]\nname='fixture'\nversion='0.1.0'\n[[availability]]\nobject='fixture'\nfields={fields}\nstage='Object'\n")).unwrap();
        // Raw inspection is discovery/parsing, intentionally not semantic validation.
        assert!(
            Command::new(bin)
                .arg("inspect")
                .arg(&root)
                .output()
                .unwrap()
                .status
                .success()
        );
        for mode in ["--types", "--bodies"] {
            let out = Command::new(bin)
                .arg("inspect")
                .arg(&root)
                .arg(mode)
                .output()
                .unwrap();
            assert!(!out.status.success() && out.stdout.is_empty());
            assert!(String::from_utf8_lossy(&out.stderr).contains("availability"));
        }
        for command in ["abi", "check", "emit-adapter", "build"] {
            std::fs::write(&artifact, b"existing artifact").unwrap();
            let mut cmd = Command::new(bin);
            cmd.arg(command);
            if command != "abi" {
                cmd.arg(&program);
            }
            cmd.arg("--model").arg(&root);
            if command != "check" {
                cmd.arg("--output").arg(&artifact);
            }
            if command == "emit-adapter" {
                cmd.args(["--function", "program::watch"]);
            }
            let out = cmd.output().unwrap();
            assert!(!out.status.success() && out.stdout.is_empty(), "{command}");
            assert!(
                String::from_utf8_lossy(&out.stderr).contains("availability"),
                "{command}: {:?}",
                out.stderr
            );
            assert_eq!(std::fs::read(&artifact).unwrap(), b"existing artifact");
        }
    }
    std::fs::remove_dir_all(root).unwrap();
}
