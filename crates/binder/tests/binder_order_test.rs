use gald_binder::Binder;
use gald_parser::Parser;

#[test]
fn subclass_impl_after_empty_base_impl_is_found() {
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

int main() { return 0; }
"#;
    let mut p = Parser::new(src);
    let mut unit = p.parse_translation_unit().expect("parse");
    let mut binder = Binder::new(gald_symbol::SymbolTable::new());
    let rc = binder.bind(&mut unit);
    assert_eq!(rc, 0, "binder errors: {}", binder.last_error());
}
