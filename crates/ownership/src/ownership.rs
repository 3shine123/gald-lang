use ovic_ast::*;

// ─── Ownership states ────────────────────────────────────────────────────────

#[derive(Debug, Clone, Copy, PartialEq, Eq)]
pub enum Ownership {
    Unknown,
    Retained,    // +1 retain (caller must release)
    Unretained,  // no retain (do not release)
    Autoreleased, // will be released at end of current autorelease pool
}

pub fn ownership_name(o: Ownership) -> &'static str {
    match o {
        Ownership::Unknown => "unknown",
        Ownership::Retained => "retained",
        Ownership::Unretained => "unretained",
        Ownership::Autoreleased => "autoreleased",
    }
}

fn starts_with(s: &str, prefix: &str) -> bool {
    s.starts_with(prefix)
}

// Returns the implied ownership for a method based on its selector name.
pub fn ownership_for_method(name: &str) -> Ownership {
    if name == "init" || (starts_with(name, "init") && name.as_bytes().get(4).map_or(false, |&c| c.is_ascii_uppercase())) {
        return Ownership::Unretained;
    }
    if starts_with(name, "alloc") { return Ownership::Retained; }
    if name == "new" || (starts_with(name, "new") && name.as_bytes().get(3).map_or(false, |&c| c.is_ascii_uppercase())) {
        return Ownership::Retained;
    }
    if name == "copy" || (starts_with(name, "copy") && name.as_bytes().get(4).map_or(false, |&c| c.is_ascii_uppercase())) {
        return Ownership::Retained;
    }
    if name == "mutableCopy" || (starts_with(name, "mutableCopy") && name.as_bytes().get(11).map_or(false, |&c| c.is_ascii_uppercase())) {
        return Ownership::Retained;
    }
    // ObjC convention: any method not in the alloc/new/copy/mutableCopy/init
    // families returns an object the caller does NOT own (autoreleased or
    // unretained). Treating these as retained double-released convenience
    // constructors like +arrayWithObject: / +stringWithUTF8String: under ARC
    // (caller release + autorelease pool pop).
    Ownership::Unretained
}

pub fn ownership_for_expr(e: &AstExpr) -> Ownership {
    match e.kind {
        AstExprKind::Int | AstExprKind::Float | AstExprKind::Bool |
        AstExprKind::Nil | AstExprKind::Null => Ownership::Unretained,
        AstExprKind::MsgSend => {
            if let AstExprData::MsgSend { ref selector, ref receiver, .. } = e.data {
                let ow = ownership_for_method(&selector);
                // init on a retained receiver (e.g. [[Cls alloc] init]) → Retained
                if ow == Ownership::Unretained && (selector == "init" || selector.starts_with("init")) {
                    let recv_ow = ownership_for_expr(receiver);
                    if recv_ow == Ownership::Retained {
                        return Ownership::Retained;
                    }
                }
                ow
            } else {
                Ownership::Retained
            }
        }
        AstExprKind::FuncCall => Ownership::Unretained,
        AstExprKind::VarRef | AstExprKind::Self_ | AstExprKind::Super => Ownership::Unretained,
        AstExprKind::IvarRef => Ownership::Unretained,
        AstExprKind::Cast => {
            if let AstExprData::Cast { ref expr, .. } = e.data {
                ownership_for_expr(expr)
            } else {
                Ownership::Unknown
            }
        }
        AstExprKind::Unary => {
            if let AstExprData::Unary { ref operand, .. } = e.data {
                ownership_for_expr(operand)
            } else {
                Ownership::Unknown
            }
        }
        _ => Ownership::Unknown,
    }
}