//! C resolves `(X *)p` versus `x * y` by the *symbol table*, not by token shape:
//! a name is a type name or it is an ordinary identifier. ovelc hands the parser
//! a type-name table recovered from the C preprocessor (`ovelc::ctype_probe`)
//! and marks it authoritative; these tests pin the decisions that follow, plus
//! the fallback behaviour when no authoritative table is available (which must
//! never reject code).

use ovel_parser::Parser;

fn type_names(names: &[&str]) -> Vec<String> {
    names.iter().map(|s| s.to_string()).collect()
}

fn statements(src: &str, names: &[String], complete: bool) -> Vec<String> {
    let mut p = Parser::with_c_type_names(src, names, complete);
    let unit = p.parse_translation_unit().expect("parse");
    let main_fn = unit
        .decls
        .iter()
        .find(|d| d.name.as_deref() == Some("main"))
        .expect("main");
    let mut out = Vec::new();
    if let ovel_cst::CstDeclData::Function { body: Some(b), .. } = &main_fn.data {
        if let ovel_cst::CstStmtData::Compound(stmts) = &b.data {
            for s in stmts {
                out.push(match &s.data {
                    ovel_cst::CstStmtData::Decl(d) => format!("Decl({})", d.name.as_deref().unwrap_or("?")),
                    ovel_cst::CstStmtData::Expr(_) => "Expr".to_string(),
                    ovel_cst::CstStmtData::Return(_) => "Return".to_string(),
                    other => format!("Other({:?})", std::mem::discriminant(other)),
                });
            }
        }
    }
    out
}

/// `x * y;` on two variables is a multiplication statement. It used to be
/// misread as a declaration of `y` with type `x *`, because `IDENT *` was taken
/// as a declaration shape without consulting the symbol table.
#[test]
fn multiplication_statement_is_not_a_declaration_when_table_is_authoritative() {
    let src = "int main() {\n    int x = 2;\n    int y = 3;\n    x * y;\n    return 0;\n}\n";
    let stmts = statements(src, &type_names(&["int"]), true);
    assert_eq!(
        stmts,
        vec![
            "Decl(x)".to_string(),
            "Decl(y)".to_string(),
            "Expr".to_string(),
            "Return".to_string(),
        ],
        "x * y; must be an expression statement"
    );
}

/// `FILE *fp = ...` IS a declaration — but only because `FILE` is a known type
/// name, which is exactly how C decides it.
#[test]
fn pointer_declaration_of_a_known_typedef_is_a_declaration() {
    let src = "int main() {\n    FILE *fp = 0;\n    return 0;\n}\n";
    let stmts = statements(src, &type_names(&["FILE"]), true);
    assert_eq!(stmts, vec!["Decl(fp)".to_string(), "Return".to_string()]);
}

/// Without an authoritative table the historical shape guess stays in force, so
/// a C header typedef the parser was never told about still parses as a
/// declaration instead of being rejected.
#[test]
fn pointer_declaration_of_an_unknown_name_falls_back_to_the_shape_guess() {
    let src = "int main() {\n    FILE *fp = 0;\n    return 0;\n}\n";
    let stmts = statements(src, &type_names(&[]), false);
    assert_eq!(stmts, vec!["Decl(fp)".to_string(), "Return".to_string()]);
}

/// A cast to a typedef from an included C header (`sigset_t` is real on macOS
/// and absent from the builtin list) resolves once the name is in the table.
#[test]
fn cast_to_a_header_typedef_resolves() {
    let src = "int main() {\n    long v = 0;\n    sigset_t s = (sigset_t)v;\n    return 0;\n}\n";
    let mut p = Parser::with_c_type_names(src, &type_names(&["sigset_t", "long"]), true);
    assert!(p.parse_translation_unit().is_some());
    assert!(!p.has_error(), "cast to a known typedef must parse: {}", p.last_error());
}

/// An unknown name in the unambiguous cast shape `(X *)` is rejected with the
/// reason C would give — not with a cascade — when the table is authoritative.
#[test]
fn unknown_cast_target_is_reported_when_table_is_authoritative() {
    let src = "int main() {\n    void *p = 0;\n    NotAType *n = (NotAType *)p;\n    return 0;\n}\n";
    let mut p = Parser::with_c_type_names(src, &type_names(&["void"]), true);
    let _ = p.parse_translation_unit();
    assert!(p.has_error());
    let msg = p.last_error().to_string();
    assert!(
        msg.contains("unknown type name 'NotAType'"),
        "expected the C-style diagnostic, got: {msg}"
    );
}

/// The same input without an authoritative table must NOT produce that error:
/// the name may be a typedef from a header the probe could not read.
#[test]
fn unknown_cast_target_is_not_rejected_without_an_authoritative_table() {
    let src = "int main() {\n    void *p = 0;\n    NotAType *n = (NotAType *)p;\n    return 0;\n}\n";
    let mut p = Parser::with_c_type_names(src, &type_names(&["void"]), false);
    let _ = p.parse_translation_unit();
    let msg = p.last_error().to_string();
    assert!(
        !msg.contains("unknown type name"),
        "must not claim an unknown type without the C preprocessor's table: {msg}"
    );
}

/// The shape lookahead itself: `*` inside the parens is a cast, `*` after the
/// parens (a multiplication) is not.
#[test]
fn cast_shape_lookahead_distinguishes_inside_from_outside_stars() {
    // `(x *)` — star inside → cast shape.
    let mut p = Parser::with_c_type_names("int main() { int *n = 0; return 0; }", &type_names(&["int"]), true);
    assert!(p.parse_translation_unit().is_some());
    // `(x) * y` — star outside → multiplication, no declaration is created.
    let stmts = statements(
        "int main() {\n    int x = 2;\n    int y = 3;\n    (x) * y;\n    return 0;\n}\n",
        &type_names(&["int"]),
        true,
    );
    assert_eq!(
        stmts,
        vec![
            "Decl(x)".to_string(),
            "Decl(y)".to_string(),
            "Expr".to_string(),
            "Return".to_string(),
        ]
    );
}
