// Debug harness: parse a .jeti file, walk the CST, print array-related type info
// for every variable declaration (recursing into function bodies).
use jeti_parser::Parser;

fn stmt_body(body: &Option<Box<jeti_cst::CstStmt>>) -> Vec<&jeti_cst::CstStmt> {
    match body.as_deref() {
        Some(jeti_cst::CstStmt { data: jeti_cst::CstStmtData::Compound(inner), .. }) => {
            inner.iter().collect()
        }
        _ => Vec::new(),
    }
}

fn print_decl(d: &jeti_cst::CstDecl, depth: usize) {
    let pad = "  ".repeat(depth);
    match &d.data {
        jeti_cst::CstDeclData::Class { category_name, .. } => {
            let cat = category_name.as_ref().map(|c| format!(" ({})", c)).unwrap_or_default();
            let kind_str = match d.kind {
                jeti_cst::CstDeclKind::ClassInterface => "interface",
                jeti_cst::CstDeclKind::ClassImplementation => "implementation",
                _ => "class",
            };
            println!("{}{} {}{}", pad, kind_str, d.name.as_deref().unwrap_or("?"), cat);
        }
        _ => {}
    }
    if let jeti_cst::CstDeclData::Variable { var_type, .. } = &d.data {
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
        jeti_cst::CstDeclData::Function { body, .. } => {
            walk_stmts(&stmt_body(body), depth + 1);
        }
        jeti_cst::CstDeclData::Class { methods, .. } => {
            for m in methods {
                if let jeti_cst::CstDeclData::Function { body, .. } = &m.data {
                    walk_stmts(&stmt_body(body), depth + 2);
                }
            }
        }
        _ => {}
    }
}

fn walk_stmts(stmts: &[&jeti_cst::CstStmt], depth: usize) {
    for s in stmts {
        match &s.data {
            jeti_cst::CstStmtData::Compound(inner) => walk_stmts(&inner.iter().collect::<Vec<_>>(), depth + 1),
            jeti_cst::CstStmtData::Decl(d) => print_decl(d, depth),
            jeti_cst::CstStmtData::Expr(_) => {
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
        .expect("usage: cst_dump [--inline] <file.jeti>")
        .clone();
    let text = if inline {
        // Replicate pipeline's preprocess step: Foundation etc. gets inlined.
        let dir = std::path::Path::new(&path).parent().map(|p| p.to_string_lossy().to_string()).unwrap_or_default();
        let search_dirs: Vec<String> = vec![dir, "include".to_string(), "include/Foundation".to_string()];
        let pre = jeti_preprocessor::Preprocessor::process_file(
            &path, &search_dirs, &["__clang__", "__GNUC__", "__JETI__"])
            .expect("preprocess failed");
        std::fs::write("/tmp/inline_dump.jeti", &pre.resolved_jeti).ok();
        pre.resolved_jeti
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
// usage: cst_dump --inline <file.jeti>
