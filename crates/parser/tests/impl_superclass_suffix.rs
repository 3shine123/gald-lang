// Bug #2 regression: `@implementation Base : NFObject` with an empty body used
// to leave the `: NFObject` superclass suffix unconsumed; the parser's fallback
// token-skip then swallowed the following `@interface Sub ... @end`, so `Sub`
// never reached the binder ("cannot find class 'Sub' for @implementation").
//
// Fix: parse_class_implementation now consumes the optional `: Super` suffix
// (and records it in the CST), matching parse_class_interface.
use nopa_parser::Parser;

fn decl_classes(src: &str) -> Vec<(String, String)> {
    let mut p = Parser::new(src);
    let unit = p.parse_translation_unit().expect("parse failed");
    let mut out = Vec::new();
    for d in &unit.decls {
        if matches!(
            d.kind,
            nopa_cst::CstDeclKind::ClassInterface | nopa_cst::CstDeclKind::ClassImplementation
        ) {
            let kind = if d.kind == nopa_cst::CstDeclKind::ClassInterface {
                "interface"
            } else {
                "implementation"
            };
            out.push((
                kind.to_string(),
                d.name.clone().unwrap_or_default(),
            ));
        }
    }
    out
}

#[test]
fn impl_with_superclass_suffix_and_empty_body_keeps_next_interface() {
    let src = r#"
@interface Base : NFObject
@end

@implementation Base : NFObject
@end

@interface Sub : Base
- (void)m;
@end

@implementation Sub : Base
- (void)m { }
@end
"#;
    let decls = decl_classes(src);
    assert_eq!(
        decls,
        vec![
            ("interface".into(), "Base".into()),
            ("implementation".into(), "Base".into()),
            ("interface".into(), "Sub".into()),
            ("implementation".into(), "Sub".into()),
        ],
        "the @interface Sub following an empty @implementation Base : NFObject was swallowed"
    );
}

#[test]
fn impl_superclass_suffix_parsed_before_methods() {
    let src = r#"
@implementation Base : NFObject
- (void)ping { }
@end

@interface Sub : Base
- (void)m;
@end
"#;
    let decls = decl_classes(src);
    assert_eq!(
        decls,
        vec![
            ("implementation".into(), "Base".into()),
            ("interface".into(), "Sub".into()),
        ]
    );
}

#[test]
fn impl_without_superclass_still_parses() {
    let src = r#"
@implementation Base
- (void)ping { }
@end

int main() { return 0; }
"#;
    let decls = decl_classes(src);
    assert_eq!(decls, vec![("implementation".into(), "Base".into())]);
    // main must survive too
    let mut p = Parser::new(src);
    let unit = p.parse_translation_unit().unwrap();
    assert!(unit
        .decls
        .iter()
        .any(|d| matches!(d.kind, nopa_cst::CstDeclKind::Function)));
}
