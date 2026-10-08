// Debug harness: parse a .np file, walk the CST, print array-related type info
// for every variable declaration (recursing into function bodies).
use nepa_parser::Parser;

fn stmt_body(body: &Option<Box<nepa_cst::CstStmt>>) -> Vec<&nepa_cst::CstStmt> {
    match body.as_deref() {
        Some(nepa_cst::CstStmt { data: nepa_cst::CstStmtData::Compound(inner), .. }) => {
            inner.iter().collect()
        }
        _ => Vec::new(),
    }
}

fn print_decl(d: &nepa_cst::CstDecl, depth: usize) {
    let pad = "  ".repeat(depth);
    match &d.data {
        nepa_cst::CstDeclData::Class { category_name, .. } => {
            let cat = category_name.as_ref().map(|c| format!(" ({})", c)).unwrap_or_default();
            let kind_str = match d.kind {
                nepa_cst::CstDeclKind::ClassInterface => "interface",
                nepa_cst::CstDeclKind::ClassImplementation => "implementation",
                _ => "class",
            };
            println!("{}{} {}{}", pad, kind_str, d.name.as_deref().unwrap_or("?"), cat);
        }
        _ => {}
    }
    if let nepa_cst::CstDeclData::Variable { var_type, .. } = &d.data {
        if let Some(t) = var_type {
            println!(
                "{}var '{}' is_array={} size={} size_name={:?} prim={:?} ptr={} block={}",
                pad,
                d.name.as_deref().unwrap_or("?"),
                t.is_array,
                t.array_size,
                t.array_size_name,
                t.prim,
                t.is_pointer,
                t.is_block
            );
        }
    }
    match &d.data {
        nepa_cst::CstDeclData::Function { body, .. } => {
            walk_stmts(&stmt_body(body), depth + 1);
        }
        nepa_cst::CstDeclData::Class { methods, .. } => {
            for m in methods {
                if let nepa_cst::CstDeclData::Function { body, .. } = &m.data {
                    walk_stmts(&stmt_body(body), depth + 2);
                }
            }
        }
        _ => {}
    }
}

fn walk_stmts(stmts: &[&nepa_cst::CstStmt], depth: usize) {
    for s in stmts {
        match &s.data {
            nepa_cst::CstStmtData::Compound(inner) => walk_stmts(&inner.iter().collect::<Vec<_>>(), depth + 1),
            nepa_cst::CstStmtData::Decl(d) => print_decl(d, depth),
            nepa_cst::CstStmtData::Expr(_) => {
                // Cannot recurse into expressions at stmt level without Expr walk; skip
            }
            _ => {}
        }
    }
}

fn main() {
    let args: Vec<String> = std::env::args().collect();
    let inline = args.iter().any(|a| a == "--inline");
    let path = args.iter().filter(|a| !a.starts_with('-')).nth(1)
        .expect("usage: cst_dump [--inline] <file.np>")
        .clone();
    let text = if inline {
        // Replicate pipeline's preprocess step: Foundation etc. gets inlined.
        let dir = std::path::Path::new(&path).parent().map(|p| p.to_string_lossy().to_string()).unwrap_or_default();
        let search_dirs: Vec<String> = vec![dir, "include".to_string(), "include/Foundation".to_string()];
        let pre = nepa_preprocessor::Preprocessor::process_file(
            &path, &search_dirs, &["__clang__", "__GNUC__", "__NEPA__"])
            .expect("preprocess failed");
        std::fs::write("/tmp/inline_dump.np", &pre.resolved_nepa).ok();
        pre.resolved_nepa
    } else {
        std::fs::read_to_string(&path).expect("read file")
    };
    let mut p = Parser::new(&text);
    let unit = p.parse_translation_unit().expect("parse failed");
    eprintln!("parse error: {:?}", p.last_error());
    for d in &unit.decls {
        print_decl(d, 0);
    }
}
// --- inline-source mode: replicate pipeline preprocess then dump ---
// usage: cst_dump --inline <file.np>
