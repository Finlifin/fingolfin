use fingolfin::syntax::*;
fn parse_ok(s: &str) -> File {
    parse("test.fg", s).unwrap()
}
fn function(file: &File, n: usize) -> &Function {
    match &file.items[n].value.kind {
        ItemKind::Function(f) => f,
        _ => panic!("expected function"),
    }
}
#[test]
fn plan_example_parses_with_independent_entries() {
    let source = include_str!("../Fingolfin_Project_Plan_v0.1.md");
    let start = source.find("use tt_emule.types").unwrap();
    let end = source[start..].find("```").unwrap() + start;
    let file = parse_ok(&source[start..end]);
    assert_eq!(file.items.len(), 5);
    assert_eq!(function(&file, 3).name, "watch_push");
    assert_eq!(
        function(&file, 4).hook.as_ref().unwrap().value,
        "tt.device.core.cb.post_pop"
    );
    assert_eq!(function(&file, 3).parameters.len(), 3);
    assert!(function(&file, 3).pattern.is_some());
}
#[test]
fn precedence_and_associativity() {
    let file = parse_ok("fn f() { let x = 1 + 2 * 3 - 4; }");
    let Stmt::Variable { value, .. } = &function(&file, 0).body[0].value else {
        panic!()
    };
    let Expr::Binary(op, lhs, _) = &value.value else {
        panic!()
    };
    assert_eq!(op, "-");
    let Expr::Binary(op, _, rhs) = &lhs.value else {
        panic!()
    };
    assert_eq!(op, "+");
    assert!(matches!(&rhs.value, Expr::Binary(op, _, _) if op == "*"));
}
#[test]
fn patterns_ranges_or_not_and_binding() {
    let file = parse_ok(
        "fn f on root.hit() where root: { id: 1..=9 as n } or not root: { id: 0..1 } { emit(n); }",
    );
    let Pattern::Or(branches) = &function(&file, 0).pattern.as_ref().unwrap().value else {
        panic!()
    };
    let Pattern::Object(_, fields) = &branches[0].value else {
        panic!()
    };
    assert_eq!(fields[0].value.binding.as_ref().unwrap().value, "n");
    assert!(matches!(
        fields[0].value.matcher.value,
        Match::Range {
            inclusive: true,
            ..
        }
    ));
    assert!(matches!(branches[1].value, Pattern::Not(_)));
}
#[test]
fn declarations_and_types() {
    let file = parse_ok(
        "pub struct View { id: u32, data: [u8; 16], next: *View } enum Role: u8 { reader = 0, writer = 1 } pub hook root.hit(view: *View); object root { id: u32 }",
    );
    assert_eq!(file.items.len(), 4);
    assert!(file.items[0].value.public);
}
#[test]
fn comments_keyword_prefixes_and_unicode_spans() {
    let source = "-- 中文\nfn function() { let where_value = 1; }";
    let file = parse_ok(source);
    let item = &file.items[0];
    assert_eq!(
        &source[item.span.start..item.span.end],
        "fn function() { let where_value = 1; }"
    );
    assert!(parse("bad.fg", "fn where() {}").is_err());
    assert!(parse("bad.fg", "fn f() { let x = true_value; }").is_ok());
}
#[test]
fn invalid_inputs_have_valid_spans() {
    for source in [
        "fn f on root.hit() where root: { id: 2..1 } {}",
        "fn f on root.hit() where root: { id: 1..1 } {}",
        "fn f() where root: {} {}",
        "fn f on root.hit() -> u32 {}",
        "fn f() { let x = 99999999999999999999999999999999999999999999999999; }",
        "fn f() {",
        "fn f(x: [u8; -1]) {}",
        "fn f() {} junk",
    ] {
        let err = parse("bad.fg", source).unwrap_err();
        assert_eq!(err.file, "bad.fg");
        assert!(err.span.start <= err.span.end && err.span.end <= source.len());
    }
}
#[test]
fn control_flow_and_unary() {
    let file = parse_ok(
        "fn abs(x: i32) -> i32 { if x < 0 { return -x; } else if x == 0 { return 0; } else { return x; } }",
    );
    assert!(matches!(function(&file, 0).body[0].value, Stmt::If { .. }));
}

#[test]
fn keywords_cannot_be_glued_to_identifiers() {
    for source in [
        "fnfoo() {}",
        "pubstruct A {}",
        "fn f() { letx = 1; }",
        "fn f onroot.hit() {}",
        "fn f() { return1; }",
    ] {
        // letx=1 and return1 are syntactically valid assignment/path statements,
        // but must not be mistaken for declarations or returns.
        if source.contains("letx") {
            let f = parse_ok(source);
            assert!(matches!(function(&f, 0).body[0].value, Stmt::Assign(..)));
        } else if source.contains("return1") {
            let f = parse_ok(source);
            assert!(matches!(function(&f, 0).body[0].value, Stmt::Expr(..)));
        } else {
            assert!(parse("bad.fg", source).is_err(), "{source}");
        }
    }
}
#[test]
fn paths_ignore_trivia_and_integers_cover_signed_boundary() {
    let file = parse_ok(
        "fn f on root . -- comment\n hit(x: types . Handle) { emit(-170141183460469231731687303715884105728); }",
    );
    assert_eq!(function(&file, 0).hook.as_ref().unwrap().value, "root.hit");
    assert_eq!(
        function(&file, 0).parameters[0].value.ty.value,
        Type::Named("types.Handle".into())
    );
}
