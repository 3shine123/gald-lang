use ovel_parser::Parser;

#[test]
fn array_decl_inside_block_literal_keeps_size() {
    let src = r#"
int main() {
    void (^blk)(void) = ^{
        char buf[128];
        buf[0] = 'x';
    };
    return 0;
}
"#;
    let mut p = Parser::new(src);
    let unit = p.parse_translation_unit().expect("parse");
    let main_fn = unit.decls.iter()
        .find(|d| d.name.as_deref() == Some("main"))
        .expect("main");
    let mut found = false;
    if let ovel_cst::CstDeclData::Function { body: Some(b), .. } = &main_fn.data {
        if let ovel_cst::CstStmtData::Compound(stmts) = &b.data {
            for s in stmts {
                if let ovel_cst::CstStmtData::Decl(d) = &s.data {
                    if let ovel_cst::CstDeclData::Variable { var_type: Some(_t), initializer, .. } = &d.data {
                        walk_init(initializer.as_deref(), &mut found);
                    }
                }
            }
        }
    }
    assert!(found, "block literal with array decl not found in CST");
}

fn walk_init(e: Option<&ovel_cst::CstExpr>, found: &mut bool) {
    if let Some(e) = e {
        if let ovel_cst::CstExprData::Block { body: Some(b), .. } = &e.data {
            if let ovel_cst::CstStmtData::Compound(stmts) = &b.data {
                for s in stmts {
                    if let ovel_cst::CstStmtData::Decl(d) = &s.data {
                        if let ovel_cst::CstDeclData::Variable { var_type: Some(t), .. } = &d.data {
                            if d.name.as_deref() == Some("buf") {
                                *found = true;
                                assert!(t.is_array, "block-local array decl must keep is_array (got is_array={})", t.is_array);
                                assert_eq!(t.array_size, 128, "block-local array decl must keep size");
                            }
                        }
                    }
                }
            }
        }
    }
}
// NOTE: array_suffix emission is a codegen concern; the parser CST test above
// pins the parser side. The full end-to-end check lives in
// tests/golden/29_block_array/ (added alongside this fix).
