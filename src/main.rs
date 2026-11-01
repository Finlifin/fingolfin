use clap::{Parser, Subcommand};
use std::{path::PathBuf, process::ExitCode};

#[derive(Parser)]
#[command(
    version,
    about = "Fingolfin observation-language compiler (development)"
)]
struct Cli {
    #[command(subcommand)]
    command: Command,
}
#[derive(Subcommand)]
enum Command {
    /// Build a development .fgo with BPF code for all entries or selected functions.
    Build {
        source: PathBuf,
        #[arg(long)]
        model: PathBuf,
        #[arg(long)]
        output: PathBuf,
        #[arg(long)]
        function: Vec<String>,
        #[arg(long, default_value = "clang")]
        clang: PathBuf,
        #[arg(long)]
        model_hash: Option<String>,
    },
    /// Validate a development .fgo and explain its entries.
    Explain {
        package: PathBuf,
        #[arg(long)]
        entry: Option<String>,
    },
    /// Generate BPF C and a host invocation-frame header.
    EmitBpf {
        source: PathBuf,
        #[arg(long)]
        model: PathBuf,
        #[arg(long)]
        function: String,
        #[arg(long)]
        output: PathBuf,
        #[arg(long)]
        header: PathBuf,
    },
    /// Generate a C11 SDK frame and explicit-function binding encoder.
    EmitAdapter {
        source: PathBuf,
        #[arg(long)]
        model: PathBuf,
        #[arg(long)]
        function: String,
        #[arg(long)]
        output: PathBuf,
    },
    /// Generate native C11 function bodies (not a VM artifact).
    EmitC {
        source: PathBuf,
        #[arg(long)]
        model: PathBuf,
        #[arg(long)]
        output: PathBuf,
    },
    /// Lower checked source into structured typed IR.
    Ir {
        source: PathBuf,
        #[arg(long)]
        model: PathBuf,
        #[arg(long)]
        output: Option<PathBuf>,
    },
    /// Reference-dispatch a hook using a JSON object snapshot.
    Dispatch {
        source: PathBuf,
        #[arg(long)]
        model: PathBuf,
        #[arg(long)]
        hook: String,
        #[arg(long)]
        snapshot: PathBuf,
        #[arg(long, default_value = "[]")]
        args: String,
        #[arg(long, default_value_t = 100_000)]
        steps: u64,
        #[arg(long, default_value_t = 64)]
        depth: usize,
    },
    /// Execute an ordinary checked function with the reference evaluator.
    Eval {
        source: PathBuf,
        #[arg(long)]
        model: PathBuf,
        /// Canonical function ID, e.g. program::increment.
        #[arg(long)]
        function: String,
        /// JSON array of tagged Value arguments, e.g. [{"Integer": 3}].
        #[arg(long, default_value = "[]")]
        args: String,
        #[arg(long, default_value_t = 100_000)]
        steps: u64,
        #[arg(long, default_value_t = 64)]
        depth: usize,
        /// Predeclare a zero-initialized counter slot (repeatable).
        #[arg(long)]
        counter: Vec<u32>,
        /// JSON configuration for preallocated counters, maps, histograms and output.
        #[arg(long,conflicts_with_all=["counter","output_capacity"])]
        host_config: Option<PathBuf>,
        /// Attach a bounded output queue to the reference invocation.
        #[arg(long)]
        output_capacity: Option<usize>,
    },
    /// Generate native C/C++ ABI declarations and compile-time layout assertions.
    Abi {
        #[arg(long)]
        model: PathBuf,
        #[arg(long)]
        output: PathBuf,
        /// Emit Rust repr(C) bindings instead of a C/C++ header.
        #[arg(long)]
        rust: bool,
    },
    /// Check an observation program against an independently checked model package.
    Check {
        source: PathBuf,
        #[arg(long)]
        model: PathBuf,
        /// Require this exact model source hash (not an ABI compatibility hash).
        #[arg(long)]
        model_hash: Option<String>,
    },
    /// Parse a source file and print its AST. Does not perform semantic checking.
    Parse { source: PathBuf },
    /// Discover and parse a model package without activating any entries.
    Inspect {
        package: PathBuf,
        /// Resolve imports and check declaration types/layouts (not function bodies).
        #[arg(long)]
        types: bool,
        /// Also check function bodies and fixed-layout pattern bindings.
        #[arg(long)]
        bodies: bool,
    },
}
fn run(cli: Cli) -> Result<(), String> {
    let value = match cli.command {
        Command::Build {
            source,
            model,
            output,
            function,
            clang,
            model_hash,
        } => {
            let program = fingolfin::compiler::check(&source, &model, model_hash.as_deref())
                .map_err(|errors| {
                    errors
                        .iter()
                        .map(ToString::to_string)
                        .collect::<Vec<_>>()
                        .join("\n")
                })?;
            let bytes = fingolfin::build::compile(&program, &function, &clang)?;
            fingolfin::util::artifact::write_atomic(&output, &bytes)
                .map_err(|e| format!("{}: {e}", output.display()))?;
            serde_json::json!({"output":output,"bytes":bytes.len(),"profile":"development-function-frames","sha256":fingolfin::util::hash::sha256(&bytes)})
        }
        Command::Explain { package, entry } => {
            let manifest = fingolfin::package::read(&package)?;
            if let Some(id) = &entry
                && !manifest.entries.contains_key(id)
            {
                return Err(format!("package has no entry {id}"));
            }
            let entries: Vec<_>=manifest.entries.iter().filter(|(id,_)|entry.as_ref().is_none_or(|wanted|*id==wanted)).map(|(id,e)|serde_json::json!({"function":id,"hook":e.hook,"dependencies":e.dependencies,"helpers":e.helpers,"signature":e.signature,"signature_hash":e.signature_hash,"elf_bytes":e.elf.len(),"elf_hash":e.elf_hash,"frame_hash":e.frame_hash,"frame_schema_bytes":e.frame_schema.len(),"frame_schema_hash":e.frame_schema_hash,"gate":manifest.typed_ir["functions"][id]["gate"]})).collect();
            serde_json::json!({"version":manifest.version,"profile":manifest.profile,"target":manifest.target,"program_source_hash":manifest.program_source_hash,"model_source_hash":manifest.model_source_hash,"clang_version":manifest.clang_version,"entries":entries})
        }
        Command::EmitBpf {
            source,
            model,
            function,
            output,
            header,
        } => {
            if output == header {
                return Err("BPF source and header require different paths".into());
            }
            let program = fingolfin::compiler::check(&source, &model, None).map_err(|errors| {
                errors
                    .iter()
                    .map(ToString::to_string)
                    .collect::<Vec<_>>()
                    .join("\n")
            })?;
            let generated = fingolfin::codegen::generate_bpf(&program, &function)?;
            fingolfin::util::artifact::write_atomic(&output, generated.source.as_bytes())
                .map_err(|e| format!("{}: {e}", output.display()))?;
            fingolfin::util::artifact::write_atomic(&header, generated.header.as_bytes())
                .map_err(|e| format!("{}: {e}", header.display()))?;
            serde_json::json!({"output":output,"header":header,"function":function,"program_source_hash":program.program_source_hash,"model_source_hash":program.model_source_hash,"backend":"bpf-c"})
        }
        Command::EmitAdapter {
            source,
            model,
            function,
            output,
        } => {
            let program = fingolfin::compiler::check(&source, &model, None).map_err(|errors| {
                errors
                    .iter()
                    .map(ToString::to_string)
                    .collect::<Vec<_>>()
                    .join("\n")
            })?;
            let header = fingolfin::codegen::generate_adapter(&program, &function)?;
            fingolfin::util::artifact::write_atomic(&output, header.as_bytes())
                .map_err(|e| format!("{}: {e}", output.display()))?;
            serde_json::json!({"output":output,"function":function,"backend":"c-binding-adapter","gate":fingolfin::gate::lower_c(&fingolfin::ir::lower(&program)?.functions[&function].gate)?,"program_source_hash":program.program_source_hash,"model_source_hash":program.model_source_hash})
        }
        Command::EmitC {
            source,
            model,
            output,
        } => {
            let program = fingolfin::compiler::check(&source, &model, None).map_err(|errors| {
                errors
                    .iter()
                    .map(ToString::to_string)
                    .collect::<Vec<_>>()
                    .join("\n")
            })?;
            let generated = fingolfin::codegen::generate(&program)?;
            fingolfin::util::artifact::write_atomic(&output, generated.source.as_bytes())
                .map_err(|e| format!("{}: {e}", output.display()))?;
            serde_json::json!({"output":output,"symbols":generated.symbols,"program_source_hash":program.program_source_hash,"model_source_hash":program.model_source_hash,"backend":"native-c11"})
        }
        Command::Ir {
            source,
            model,
            output,
        } => {
            let program = fingolfin::compiler::check(&source, &model, None).map_err(|errors| {
                errors
                    .iter()
                    .map(ToString::to_string)
                    .collect::<Vec<_>>()
                    .join("\n")
            })?;
            let ir = fingolfin::ir::lower(&program)?;
            let value = serde_json::to_value(&ir).map_err(|e| e.to_string())?;
            if let Some(path) = output {
                let bytes = serde_json::to_vec_pretty(&ir).map_err(|e| e.to_string())?;
                fingolfin::util::artifact::write_atomic(&path, &bytes)
                    .map_err(|e| format!("{}: {e}", path.display()))?;
                serde_json::json!({"output":path,"functions":ir.functions.len(),"program_source_hash":ir.program_source_hash})
            } else {
                value
            }
        }
        Command::Dispatch {
            source,
            model,
            hook,
            snapshot,
            args,
            steps,
            depth,
        } => {
            let program = fingolfin::compiler::check(&source, &model, None).map_err(|errors| {
                errors
                    .iter()
                    .map(ToString::to_string)
                    .collect::<Vec<_>>()
                    .join("\n")
            })?;
            let args = serde_json::from_str(&args).map_err(|e| format!("arguments: {e}"))?;
            let bytes =
                std::fs::read(&snapshot).map_err(|e| format!("{}: {e}", snapshot.display()))?;
            let objects = serde_json::from_slice(&bytes).map_err(|e| format!("snapshot: {e}"))?;
            let mut evaluator = fingolfin::eval::Evaluator::new(&program, steps, depth);
            let result = evaluator
                .dispatch(&hook, args, &objects)
                .map_err(|e| format!("reference dispatch: {e:?}"))?;
            serde_json::json!({"result": result, "steps": evaluator.consumed(), "engine":"reference", "program_source_hash":program.program_source_hash})
        }
        Command::Eval {
            source,
            model,
            function,
            args,
            steps,
            depth,
            counter,
            host_config,
            output_capacity,
        } => {
            let program = fingolfin::compiler::check(&source, &model, None).map_err(|errors| {
                errors
                    .iter()
                    .map(ToString::to_string)
                    .collect::<Vec<_>>()
                    .join("\n")
            })?;
            let args = serde_json::from_str(&args).map_err(|e| format!("arguments: {e}"))?;
            let mut evaluator = fingolfin::eval::Evaluator::new(&program, steps, depth);
            if let Some(path) = host_config {
                use std::io::Read;
                let file =
                    std::fs::File::open(&path).map_err(|e| format!("{}: {e}", path.display()))?;
                let mut bytes = vec![];
                file.take(1024 * 1024 + 1)
                    .read_to_end(&mut bytes)
                    .map_err(|e| e.to_string())?;
                if bytes.len() > 1024 * 1024 {
                    return Err("host configuration exceeds 1 MiB".into());
                }
                let config: fingolfin::intrinsic::HostConfig =
                    serde_json::from_slice(&bytes).map_err(|e| format!("host config: {e}"))?;
                evaluator.set_host(
                    config
                        .prepare()
                        .map_err(|e| format!("host config: {e:?}"))?,
                );
            } else if !counter.is_empty() || output_capacity.is_some() {
                let mut host =
                    fingolfin::intrinsic::Host::new(counter.len(), output_capacity.unwrap_or(0))
                        .map_err(|e| format!("host capacity: {e:?}"))?;
                for slot in &counter {
                    host.declare_counter(*slot, 0)
                        .map_err(|e| format!("counter: {e:?}"))?;
                }
                evaluator.set_host(host);
            }
            let result = evaluator
                .call(&function, args)
                .map_err(|e| format!("reference execution: {e:?}"))?;
            let state = evaluator.host_mut().map(|host| host.snapshot());
            serde_json::json!({"state":state,"result": result, "steps": evaluator.consumed(), "engine": "reference", "program_source_hash": program.program_source_hash})
        }
        Command::Abi {
            model,
            output,
            rust,
        } => {
            let errors = |errors: Vec<fingolfin::syntax::Diagnostic>| {
                errors
                    .iter()
                    .map(ToString::to_string)
                    .collect::<Vec<_>>()
                    .join("\n")
            };
            let package = fingolfin::source::load(&model).map_err(errors)?;
            let index = fingolfin::resolve::collect(&package).map_err(errors)?;
            let checked = fingolfin::types::check(&index).map_err(errors)?;
            fingolfin::compiler::validate_availability(&package, &index, &checked)
                .map_err(errors)?;
            let header = fingolfin::abi::generate(&index, &checked)?;
            fingolfin::util::artifact::write_atomic(
                &output,
                (if rust {
                    fingolfin::abi::generate_rust(&index, &checked)?
                } else {
                    header.source
                })
                .as_bytes(),
            )
            .map_err(|e| format!("{}: {e}", output.display()))?;
            serde_json::json!({"header":output,"model_source_hash":package.source_hash,"symbols":header.symbols})
        }
        Command::Check {
            source,
            model,
            model_hash,
        } => {
            let program = fingolfin::compiler::check(&source, &model, model_hash.as_deref())
                .map_err(|errors| {
                    errors
                        .iter()
                        .map(ToString::to_string)
                        .collect::<Vec<_>>()
                        .join("\n")
                })?;
            serde_json::to_value(program).map_err(|e| e.to_string())?
        }
        Command::Parse { source } => {
            let text = std::fs::read_to_string(&source)
                .map_err(|e| format!("{}: {e}", source.display()))?;
            let ast = fingolfin::syntax::parse(&source.display().to_string(), &text)
                .map_err(|e| e.to_string())?;
            serde_json::to_value(ast).map_err(|e| e.to_string())?
        }
        Command::Inspect {
            package,
            types,
            bodies,
        } => {
            let model = fingolfin::source::load(&package).map_err(|errors| {
                errors
                    .iter()
                    .map(ToString::to_string)
                    .collect::<Vec<_>>()
                    .join("\n")
            })?;
            if types || bodies {
                let format_errors = |errors: Vec<fingolfin::syntax::Diagnostic>| {
                    errors
                        .iter()
                        .map(ToString::to_string)
                        .collect::<Vec<_>>()
                        .join("\n")
                };
                let index = fingolfin::resolve::collect(&model).map_err(format_errors)?;
                let checked = fingolfin::types::check(&index).map_err(format_errors)?;
                fingolfin::compiler::validate_availability(&model, &index, &checked)
                    .map_err(format_errors)?;
                if bodies {
                    let checked_bodies =
                        fingolfin::sema::check(&index, &checked).map_err(format_errors)?;
                    serde_json::json!({"validation": "bodies-and-fixed-layout-patterns", "symbols": index, "types": checked, "bodies": checked_bodies})
                } else {
                    serde_json::json!({"validation": "declarations-only", "symbols": index, "types": checked})
                }
            } else {
                serde_json::to_value(model).map_err(|e| e.to_string())?
            }
        }
    };
    // Report closed pipes as normal I/O failures rather than panicking.
    use std::io::Write;
    let mut out = std::io::BufWriter::new(std::io::stdout().lock());
    serde_json::to_writer_pretty(&mut out, &value).map_err(|e| e.to_string())?;
    writeln!(out).map_err(|e| e.to_string())?;
    out.flush().map_err(|e| e.to_string())
}
fn main() -> ExitCode {
    match run(Cli::parse()) {
        Ok(()) => ExitCode::SUCCESS,
        Err(e) => {
            eprintln!("{e}");
            ExitCode::FAILURE
        }
    }
}
