use fingolfin::{
    gate::{self, Facts, Plan, Predicate, Property, Read, Stage, Stamp, Test},
    pattern::Value,
    sdk::ObjectKey,
    syntax::Literal,
    util::budget::Budget,
};
use std::{collections::BTreeMap, fmt::Write, fs, process::Command};
fn leaf(read: usize) -> Predicate {
    Predicate::Test {
        read,
        test: Test::Literal(Literal::Bool(true)),
    }
}
#[test]
fn lowered_topology_matches_rust_reference_in_actual_c() {
    let stages = [
        Stage::Attachment,
        Stage::Object,
        Stage::Update,
        Stage::Invocation,
    ];
    let reads = (0..3)
        .map(|i| Read {
            property: Property {
                object: "root".into(),
                fields: vec![format!("x{i}")],
            },
            stage: stages[i],
        })
        .collect::<Vec<_>>();
    let predicates = vec![
        Predicate::True,
        Predicate::And(vec![]),
        Predicate::Or(vec![]),
        Predicate::Not(Box::new(leaf(0))),
        Predicate::And(vec![leaf(0), leaf(1), leaf(2)]),
        Predicate::Or(vec![leaf(0), leaf(1), leaf(2)]),
        Predicate::Not(Box::new(Predicate::Or(vec![
            Predicate::And(vec![leaf(0), leaf(1)]),
            leaf(2),
        ]))),
    ];
    let stamp = Stamp {
        configuration: 2,
        object: ObjectKey {
            run: 3,
            launch: 4,
            slot: 5,
            generation: 6,
        },
    };
    let mut c = String::from(
        "#include \"fingolfin/gate.h\"\n#include <assert.h>\nstruct Input {const unsigned *reads;const unsigned *values;};\nstatic FgStatus test(void *context,uint32_t id,uint64_t *budget,FgTruth *out){struct Input *in=context;if(!*budget)return FG_STATUS_BUDGET;(*budget)--;*out=(FgTruth)in->values[in->reads[id]];return FG_STATUS_OK;}\nint main(void){FgStateKey stamp={1,2,3,4,5,6};\n",
    );
    let mut cases = 0;
    for predicate in predicates {
        let plan = Plan {
            version: 1,
            reads: reads.clone(),
            predicate,
        };
        let lowered = gate::lower_c(&plan).unwrap();
        c.push_str("{static const FgGateNode nodes[]={");
        for n in &lowered.nodes {
            write!(
                c,
                "{{{},{},{},{},{}}},",
                n.op, n.left, n.right, n.test, n.stage
            )
            .unwrap();
        }
        c.push_str("};static const unsigned reads[]={");
        if lowered.tests.is_empty() {
            c.push_str("0,");
        }
        for test in &lowered.tests {
            let i = reads
                .iter()
                .position(|r| r.property == test.read.property)
                .unwrap();
            assert_eq!(test.read.stage, stages[i]);
            assert!(matches!(test.test, Test::Literal(Literal::Bool(true))));
            write!(c, "{i},").unwrap();
        }
        c.push_str("};\n");
        for a in 0..3 {
            for b in 0..3 {
                for d in 0..3 {
                    for (stage_id, stage) in stages.iter().enumerate() {
                        let values = [a, b, d];
                        let fields = values
                            .iter()
                            .enumerate()
                            .map(|(i, v)| {
                                (
                                    format!("x{i}"),
                                    match v {
                                        0 => Value::Unknown,
                                        1 => Value::Bool(false),
                                        _ => Value::Bool(true),
                                    },
                                )
                            })
                            .collect();
                        let facts = Facts::new(
                            stamp,
                            BTreeMap::from([("root".into(), Value::Record(fields))]),
                        );
                        let expected = plan
                            .evaluate(&facts, stamp, *stage, &mut Budget::new(10000))
                            .allow;
                        writeln!(c,"{{unsigned values[]={{{a},{b},{d}}};struct Input input={{reads,values}};FgGateDecision result=fg_gate_evaluate((FgGatePlan){{nodes,sizeof(nodes)/sizeof(nodes[0])}},&stamp,&stamp,(FgGateStage){stage_id},10000,test,&input);assert(result.allow=={});}}",usize::from(expected)).unwrap();
                        cases += 1;
                    }
                }
            }
        }
        c.push_str("}\n");
    }
    c.push_str("return 0;}\n");
    let dir = std::env::temp_dir().join(format!("fg-gate-codegen-{}", std::process::id()));
    fs::create_dir_all(&dir).unwrap();
    fs::write(dir.join("gate.c"), c).unwrap();
    let exe = dir.join("gate");
    let compiled = Command::new("clang")
        .args([
            "-std=c11",
            "-Wall",
            "-Wextra",
            "-Werror",
            "-fsanitize=undefined",
            "-fno-sanitize-recover=all",
            "-I",
            "runtime/include",
        ])
        .arg(dir.join("gate.c"))
        .args(["runtime/src/gate.c", "-o"])
        .arg(&exe)
        .output()
        .unwrap();
    assert!(
        compiled.status.success(),
        "{}",
        String::from_utf8_lossy(&compiled.stderr)
    );
    assert!(Command::new(&exe).status().unwrap().success());
    assert_eq!(cases, 756);
    fs::remove_dir_all(dir).unwrap();
}
#[test]
fn lowering_rejects_invalid_or_unbounded_plans() {
    let mut plan = Plan {
        version: 2,
        reads: vec![],
        predicate: Predicate::True,
    };
    assert!(gate::lower_c(&plan).is_err());
    plan.version = 1;
    plan.predicate = leaf(0);
    assert!(gate::lower_c(&plan).is_err());
    plan.predicate = Predicate::And(vec![Predicate::True; 1025]);
    assert!(gate::lower_c(&plan).is_err());
    plan.predicate = Predicate::True;
    for _ in 0..130 {
        plan.predicate = Predicate::Not(Box::new(plan.predicate));
    }
    assert!(gate::lower_c(&plan).is_err());
}

#[test]
fn generated_predicates_match_model_pattern_reference() {
    use fingolfin::{codegen, compiler, ir, source, syntax};
    let mut model = source::load(std::path::Path::new("models/fixture")).unwrap();
    model.modules.get_mut("").unwrap().ast.items.extend(
        syntax::parse(
            "extra.fg",
            "pub object root {a:i8,b:i8} pub hook root.hit();",
        )
        .unwrap()
        .items,
    );
    let stamp = Stamp {
        configuration: 2,
        object: ObjectKey {
            run: 3,
            launch: 4,
            slot: 5,
            generation: 6,
        },
    };
    let dir = std::env::temp_dir().join(format!("fg-predicates-{}", std::process::id()));
    fs::create_dir_all(&dir).unwrap();
    for (n, pattern) in [
        "root: {a: 1, b: 0..=2}",
        "root: {a: 1} or root: {b: 2}",
        "not root: {a: 1, b: 2}",
    ]
    .iter()
    .enumerate()
    {
        let program = compiler::check_source(
            "p.fg",
            &format!("fn watch on root.hit() where {pattern} {{}}"),
            &model,
        )
        .unwrap();
        let plan = ir::lower(&program).unwrap().functions["program::watch"]
            .gate
            .clone();
        fs::write(
            dir.join("adapter.h"),
            codegen::generate_adapter(&program, "program::watch").unwrap(),
        )
        .unwrap();
        let mut c = String::from(
            "#include \"adapter.h\"\n#include <assert.h>\nint main(void){FgStateKey stamp={1,2,3,4,5,6};\n",
        );
        c = c.replace("int main(void)", r#"struct Model { int64_t a,b; };
static FgStatus provider(void *context,const FgPropertyPath *path,uint64_t *budget,FgFactValue *out) {
 if(!*budget)return FG_STATUS_BUDGET;(*budget)--;
 struct Model *m=context;
 if(strcmp(path->object,"root"))return FG_STATUS_INVALID;
 if(!path->field_count){out->kind=FG_FACT_RECORD;return FG_STATUS_OK;}
 if(path->field_count!=1)return FG_STATUS_INVALID;
 int64_t value;
 if(!strcmp(path->fields[0],"a"))value=m->a;
 else if(!strcmp(path->fields[0],"b"))value=m->b;
 else return FG_STATUS_INVALID;
 if(value==-4)return FG_STATUS_NOT_FOUND;
 out->kind=FG_FACT_INTEGER;out->integer=(FgFactInteger){(uint64_t)value,value<0?UINT64_MAX:0};return FG_STATUS_OK;
}
int main(void)"#);
        for a in -4..4 {
            for b in -4..4 {
                let val = |x| {
                    if x == -4 {
                        Value::Unknown
                    } else {
                        Value::Integer(x)
                    }
                };
                let facts = Facts::new(
                    stamp,
                    BTreeMap::from([(
                        "root".into(),
                        Value::Record(BTreeMap::from([("a".into(), val(a)), ("b".into(), val(b))])),
                    )]),
                );
                let expected = plan
                    .evaluate(&facts, stamp, Stage::Invocation, &mut Budget::new(10000))
                    .allow;
                writeln!(c, "{{FgFactValue values[{}]={{0}};", plan.reads.len()).unwrap();
                writeln!(c,"uint64_t budget=10000;struct Model model={{{a},{b}}};assert(fg_generated_collect(FG_GATE_INVOCATION,provider,&model,&budget,values,sizeof(values)/sizeof(values[0]))==FG_STATUS_OK);").unwrap();
                // Binding signature is const facts+size; gate core uses a context.
                writeln!(
                    c,
                    "FgTruth truths[{}];",
                    gate::lower_c(&plan).unwrap().tests.len()
                )
                .unwrap();
                for i in 0..gate::lower_c(&plan).unwrap().tests.len() {
                    writeln!(c,"assert(fg_generated_predicate(values,sizeof(values),{i},&budget,&truths[{i}])==FG_STATUS_OK);").unwrap();
                }
                writeln!(c,"FgGateDecision d=fg_gate_evaluate(fg_generated_gate_plan(),&stamp,&stamp,FG_GATE_INVOCATION,10000,read_truth,truths);assert(d.allow=={});}}",usize::from(expected)).unwrap();
            }
        }
        c.push_str("return 0;}\n");
        c=c.replace("int main(void)","static FgStatus read_truth(void *ctx,uint32_t index,uint64_t *budget,FgTruth *truth){if(!*budget)return FG_STATUS_BUDGET;(*budget)--;*truth=((FgTruth *)ctx)[index];return FG_STATUS_OK;}\nint main(void)");
        fs::write(dir.join("main.c"), c).unwrap();
        let exe = dir.join(format!("test{n}"));
        let result = Command::new("clang")
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
            .arg(dir.join("main.c"))
            .args([
                "runtime/src/facts.c",
                "runtime/src/gate.c",
                "runtime/src/util.c",
                "-o",
            ])
            .arg(&exe)
            .output()
            .unwrap();
        assert!(
            result.status.success(),
            "{}",
            String::from_utf8_lossy(&result.stderr)
        );
        assert!(Command::new(&exe).status().unwrap().success());
    }
    fs::remove_dir_all(dir).unwrap();
}
