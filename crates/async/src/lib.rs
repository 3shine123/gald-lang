//! Async/await desugar (route map item #4, milestone 1).
//!
//! Methods whose body contains `await` are async: the desugar pass rewrites
//! each one into a **state-machine function** operating on a heap task, and
//! `async void` methods additionally get a **blocking wrapper** entry that
//! pumps the scheduler to completion. Design (AGENTS.md「语言特性路线图」):
//!
//! - `await e` splits the body into segments; each segment runs inside one
//!   `switch (task->state)` arm.
//! - Locals live in a per-method `NPTaskFrame` struct so they survive
//!   suspension (codegen's @try lifting precedent).
//! - break/continue across awaits become state jumps (no C break/continue
//!   across switch arms).
//! - `@try` spanning an await is rejected in milestone 1 (jmp_buf cannot
//!   survive suspension); `@noarc` is allowed.
//! - ARC settle: object locals lifted into the frame are released at task
//!   finish/cancel (single convergence point).
//!
//! This module currently provides:
//!   1. async classification (body contains `await` — chain infection),
//!   2. `@try` spanning an await rejection.
//!
//! Note (doc/async_nptask_plan.md): bare async calls from sync contexts are
//! LEGAL — form A (lazy task creation, handle is a first-class value). The
//! old M1 sync-context call check was removed when `NPTask<T>` handles
//! became real values; `[task start]` / `@await` drive them afterwards.

use ovic_ast::{AstDecl, AstDeclData, AstExpr, AstExprData, AstExprKind, AstStmt, AstStmtData};

/// A method classified as async, with the data the desugar needs.
pub struct AsyncMethod<'a> {
    /// `Owner_selector` symbol of the method (used for the frame/entry names).
    pub symbol: String,
    /// True when the method returns void (→ blocking-wrapper entry candidate).
    pub returns_void: bool,
    /// The method's body before transformation.
    pub body: &'a mut AstStmt,
}

/// Does this expression subtree contain an `await`?
pub fn expr_contains_await(e: &AstExpr) -> bool {
    match &e.data {
        AstExprData::Await(_) => true,
        AstExprData::MsgSend { receiver, args, .. } => {
            expr_contains_await(receiver) || args.iter().any(expr_contains_await)
        }
        AstExprData::FuncCall { callee, args, .. } => {
            callee.as_ref().map_or(false, |c| expr_contains_await(c))
                || args.iter().any(expr_contains_await)
        }
        AstExprData::Unary { operand, .. } => expr_contains_await(operand),
        AstExprData::Binary { left, right, .. } => {
            expr_contains_await(left) || expr_contains_await(right)
        }
        AstExprData::Assign { target, value } => {
            expr_contains_await(target) || expr_contains_await(value)
        }
        AstExprData::Cast { expr, .. } => expr_contains_await(expr),
        AstExprData::Ternary { cond, then, else_ } => {
            expr_contains_await(cond)
                || expr_contains_await(then)
                || expr_contains_await(else_)
        }
        AstExprData::Comma(exprs) => exprs.iter().any(expr_contains_await),
        AstExprData::Subscript { object, key } => {
            expr_contains_await(object) || expr_contains_await(key)
        }
        AstExprData::InitList(items) | AstExprData::ArrayLit(items) => {
            items.iter().any(expr_contains_await)
        }
        _ => false,
    }
}

/// Does this statement subtree contain an `await`?
pub fn stmt_contains_await(s: &AstStmt) -> bool {
    match &s.data {
        AstStmtData::Expr(e) => expr_contains_await(e),
        AstStmtData::Compound(stmts) => stmts.iter().any(stmt_contains_await),
        AstStmtData::If { cond, then, else_ } => {
            expr_contains_await(cond)
                || stmt_contains_await(then)
                || else_.as_ref().map_or(false, |e| stmt_contains_await(e))
        }
        AstStmtData::While { cond, body } | AstStmtData::Do { body, cond } => {
            expr_contains_await(cond) || stmt_contains_await(body)
        }
        AstStmtData::For { init, cond, incr, body } => {
            init.as_ref().map_or(false, |x| stmt_contains_await(x))
                || cond.as_ref().map_or(false, |x| expr_contains_await(x))
                || incr.as_ref().map_or(false, |x| expr_contains_await(x))
                || stmt_contains_await(body)
        }
        AstStmtData::Return(Some(e)) | AstStmtData::Throw(Some(e)) => expr_contains_await(e),
        AstStmtData::Switch { expr, body } => {
            expr_contains_await(expr) || stmt_contains_await(body)
        }
        AstStmtData::Case { value, body } => {
            expr_contains_await(value) || stmt_contains_await(body)
        }
        AstStmtData::Default(body) => stmt_contains_await(body),
        AstStmtData::Synchronized { lock, body } => {
            expr_contains_await(lock) || stmt_contains_await(body)
        }
        AstStmtData::Autoreleasepool(body) | AstStmtData::NoArc(body) => stmt_contains_await(body),
        AstStmtData::Decl(d) => decl_contains_await(d),
        _ => false,
    }
}

fn decl_contains_await(d: &AstDecl) -> bool {
    match &d.data {
        AstDeclData::Variable { init, next, .. } => {
            init.as_ref().map_or(false, |x| expr_contains_await(x))
                || next.as_ref().map_or(false, |x| decl_contains_await(x))
        }
        _ => false,
    }
}

/// Is the given method async (its body contains at least one `await`)?
pub fn method_is_async(m: &AstDecl) -> bool {
    match &m.data {
        AstDeclData::Method { body: Some(b), .. } => stmt_contains_await(b),
        _ => false,
    }
}

/// Diagnostics collected during the async pre-pass (reported by the pipeline
/// before desugar runs; any error aborts compilation).
pub struct AsyncDiagnostics {
    pub errors: Vec<String>,
    /// Recoverable issues (currently unused: async-modifier reconciliation
    /// reports errors in both directions). Purple pipeline warnings; `-Werror`
    /// escalates.
    pub warnings: Vec<String>,
}

/// Pre-desugar validation over the whole AST unit:
/// - reject `@try` blocks that contain an `await` (jmp_buf cannot survive a
///   suspension point in milestone 1),
/// - reject calling a non-void async method from a synchronous context
///   (methods not classified async). Milestone 1 reports these; call-graph
///   precision improves in later milestones.
pub fn check_unit(unit: &ovic_ast::AstUnit) -> AsyncDiagnostics {
    let mut diags = AsyncDiagnostics { errors: Vec::new(), warnings: Vec::new() };

    // ── async-modifier reconciliation (doc/async_nptask_plan.md, stage B) ──
    // The modifier is a signature-level flag; the body's awaits decide the
    // truth. Only implementations (methods WITH a body) reconcile against the
    // body — header-only `@interface` methods have no body to contradict
    // them (cross-TU safety, same rule as the proto-conformance check).
    // Interface/impl marker agreement is checked separately below.
    let mut iface_markers: std::collections::HashMap<String, bool> = std::collections::HashMap::new();
    for decl in &unit.decls {
        if let AstDeclData::Class { methods, is_implementation: false, cls_sym: Some(cls), .. } = &decl.data {
            for m in methods {
                if let (AstDeclData::Method { async_marker, .. }, Some(sym)) = (&m.data, &m.name) {
                    iface_markers.insert(format!("{}::{}", cls, sym), *async_marker);
                }
            }
        }
    }
    for decl in &unit.decls {
        match &decl.data {
            AstDeclData::Class { methods, cls_sym: Some(cls), .. } => {
                for m in methods {
                    let (marker, body) = match &m.data {
                        AstDeclData::Method { async_marker, body, .. } => (*async_marker, body),
                        _ => continue,
                    };
                    let Some(sym) = &m.name else { continue };
                    // The marker is part of the signature: @interface and
                    // @implementation must agree.
                    let key = format!("{}::{}", cls, sym);
                    if let Some(&iface_marked) = iface_markers.get(&key) {
                        if iface_marked != marker {
                            diags.errors.push(format!(
                                "{}:{}: 'async' modifier mismatch on '{}': the @interface and @implementation disagree — the modifier is part of the method signature",
                                m.line, m.col, sym));
                        }
                    }
                    // Body reconciliation — implementations only.
                    if let Some(b) = body {
                        let has_await = stmt_contains_await(b);
                        reconcile_marker(marker, has_await, sym, m.line, m.col, &mut diags);
                    }
                }
            }
            // Top-level functions (main included): `@await` here is the
            // BLOCKING-join entry form (doc/async_nptask_plan.md — sync
            // context awaits pump/drive the task inline). Legal without the
            // modifier; only class methods reconcile.
            _ => {}
        }
    }

    // Walk every method body AND top-level function body (main included):
    // @try/await check. (The M1 sync-context call check is gone — a bare
    // async call from sync code is the legal lazy form A.)
    for decl in &unit.decls {
        match &decl.data {
            AstDeclData::Class { methods, .. } => {
                for m in methods {
                    if let (AstDeclData::Method { body: Some(b), .. }, Some(sym)) = (&m.data, &m.name) {
                        check_stmt_for_try_await(b, sym, &mut diags);
                    }
                }
            }
            AstDeclData::Function { body: Some(b), .. } => {
                let sym = decl.name.clone().unwrap_or_else(|| "<function>".to_string());
                check_stmt_for_try_await(b, &sym, &mut diags);
            }
            _ => {}
        }
    }
    diags
}

/// Reconcile the `async` return-type modifier against the body's awaits
/// (doc/async_nptask_plan.md, stage B). The modifier is part of the
/// signature, so a lying signature is an error in BOTH directions:
///   marked + await     → ok
///   marked, no await   → error — the modifier must not lie (same philosophy
///                        as bare `@throws` requiring a real throw)
///   await, unmarked    → error — the signature is lying: a suspending method
///                        MUST be declared `async NPTask<T>`
///   unmarked, no await → ok
fn reconcile_marker(
    marked: bool,
    has_await: bool,
    sym: &str,
    line: usize,
    col: usize,
    diags: &mut AsyncDiagnostics,
) {
    if marked && !has_await {
        diags.errors.push(format!(
            "{}:{}: '{}' is declared 'async' but its body never suspends — drop the modifier or add an '@await'",
            line, col, sym));
    } else if !marked && has_await {
        diags.errors.push(format!(
            "{}:{}: '{}' contains '@await' but is not declared 'async NPTask<T>' — a suspending method must declare the async modifier",
            line, col, sym));
    }
}

/// Reject `@try` containing an `await` anywhere in its try/catch/finally.
fn check_stmt_for_try_await(s: &AstStmt, owner: &str, diags: &mut AsyncDiagnostics) {
    match &s.data {
        AstStmtData::Try { try_block, catches, finally_block } => {
            if stmt_contains_await(try_block)
                || catches.iter().any(stmt_contains_await)
                || finally_block.as_ref().map_or(false, |f| stmt_contains_await(f))
            {
                diags.errors.push(format!(
                    "{}: @try spanning an await is not supported yet: \
                     a jmp_buf cannot survive a suspension point — move the \
                     @try outside the loop/segment or drop the awaits inside it",
                    owner
                ));
            }
            check_stmt_for_try_await(try_block, owner, diags);
        }
        AstStmtData::Compound(stmts) => {
            for st in stmts { check_stmt_for_try_await(st, owner, diags); }
        }
        AstStmtData::If { then, else_, .. } => {
            check_stmt_for_try_await(then, owner, diags);
            if let Some(e) = else_ { check_stmt_for_try_await(e, owner, diags); }
        }
        AstStmtData::While { body, .. } | AstStmtData::Do { body, .. }
        | AstStmtData::Autoreleasepool(body) | AstStmtData::NoArc(body) => {
            check_stmt_for_try_await(body, owner, diags);
        }
        AstStmtData::For { init, body, .. } => {
            if let Some(i) = init { check_stmt_for_try_await(i, owner, diags); }
            check_stmt_for_try_await(body, owner, diags);
        }
        AstStmtData::Switch { body, .. } | AstStmtData::Case { body, .. } => {
            check_stmt_for_try_await(body, owner, diags);
        }
        AstStmtData::Default(body) => check_stmt_for_try_await(body, owner, diags),
        _ => {}
    }
}

// ─── Milestone 1 desugar: task wrapper + await lowering ─────────────────────

/// Rewrite an async method body into the M1 task-driven form:
///
/// ```text
///     { NPTask *__ovic_task = ovic_task_create(entry_stub, (NPObject *)self, 0);
///       ...original body with `await e` lowered...
///       ovic_task_join(__ovic_task); }
/// ```
///

/// `await e` lowers to `(*__ovic_task->entry)(__ovic_task), (e)` — evaluate
/// the task's next segment (M1: the entry stub is a no-op returning 1, so
/// this is a pure sequencing hook), then the awaited expression. This keeps
/// the AST/observable behavior of M1 identical to synchronous execution while
/// establishing the exact task API contract that M2's real state-machine
/// split (segment lifting, suspension) builds on.
///
/// `returns_void` async methods get the join appended (blocking wrapper
/// semantics: pumping to completion). Non-void async methods also join —
/// their return value flows through the expression normally (only callable
/// from async contexts per the check pass).
pub fn desugar_method_body(body: &mut AstStmt) {
    use ovic_ast::{AstStmtKind, AstType};
    use ovic_cst::TypePrim;

    let line = body.line;
    let col = body.col;
    // 1. Lower every `await e` in place, deepest-first is not needed: we do a
    //    full-tree rewrite where each Await becomes a Comma(call_hook, e).
    fn lower_awaits(s: &mut AstStmt) {
        match &mut s.data {
            AstStmtData::Expr(e) => lower_awaits_expr(e),
            AstStmtData::Compound(stmts) => { for st in stmts { lower_awaits(st); } }
            AstStmtData::If { cond, then, else_ } => {
                lower_awaits_expr(cond);
                lower_awaits(then);
                if let Some(e) = else_ { lower_awaits(e); }
            }
            AstStmtData::While { cond, body } | AstStmtData::Do { body, cond } => {
                lower_awaits_expr(cond);
                lower_awaits(body);
            }
            AstStmtData::For { init, cond, incr, body } => {
                if let Some(i) = init { lower_awaits(i); }
                if let Some(c) = cond { lower_awaits_expr(c); }
                if let Some(u) = incr { lower_awaits_expr(u); }
                lower_awaits(body);
            }
            AstStmtData::Return(Some(e)) => lower_awaits_expr(e),
            AstStmtData::Decl(d) => lower_awaits_decl(d),
            _ => {}
        }
    }
    fn lower_awaits_decl(d: &mut AstDecl) {
        if let AstDeclData::Variable { init, next, .. } = &mut d.data {
            if let Some(i) = init { lower_awaits_expr(i); }
            if let Some(n) = next { lower_awaits_decl(n); }
        }
    }
    fn lower_awaits_expr(e: &mut AstExpr) {
        let line = e.line;
        let col = e.col;
        match &mut e.data {
            AstExprData::Await(inner) => {
                lower_awaits_expr(inner);
                // NPTask design (doc/async_nptask_plan.md, stage D):
                // `@await e` → `ovic_task_await(e)` — the awaited expression
                // is an async call (lazy task creation) or an NPTask handle;
                // the runtime auto-starts and blocks/suspends, caching the
                // result (multi-await safe).
                let taken = std::mem::replace(&mut **inner, AstExpr {
                    kind: AstExprKind::Int, expr_type: None, line, col,
                    data: AstExprData::Int(0),
                });
                *e = AstExpr {
                    kind: AstExprKind::FuncCall, expr_type: None, line, col,
                    data: AstExprData::FuncCall {
                        func: None,
                        name: "ovic_task_await".to_string(),
                        callee: None,
                        args: vec![taken],
                    },
                };
            }
            AstExprData::MsgSend { receiver, args, .. } => {
                lower_awaits_expr(receiver);
                for a in args.iter_mut() { lower_awaits_expr(a); }
            }
            AstExprData::FuncCall { callee, args, .. } => {
                if let Some(c) = callee { lower_awaits_expr(c); }
                for a in args.iter_mut() { lower_awaits_expr(a); }
            }
            AstExprData::Unary { operand, .. } => lower_awaits_expr(operand),
            AstExprData::Binary { left, right, .. } => {
                lower_awaits_expr(left);
                lower_awaits_expr(right);
            }
            AstExprData::Assign { target, value } => {
                lower_awaits_expr(target);
                lower_awaits_expr(value);
            }
            AstExprData::Cast { expr, .. } => lower_awaits_expr(expr),
            AstExprData::Ternary { cond, then, else_ } => {
                lower_awaits_expr(cond);
                lower_awaits_expr(then);
                lower_awaits_expr(else_);
            }
            AstExprData::Comma(exprs) => {
                for x in exprs.iter_mut() { lower_awaits_expr(x); }
            }
            AstExprData::Subscript { object, key } => {
                lower_awaits_expr(object);
                lower_awaits_expr(key);
            }
            AstExprData::InitList(items) | AstExprData::ArrayLit(items) => {
                for x in items.iter_mut() { lower_awaits_expr(x); }
            }
            _ => {}
        }
    }
    lower_awaits(body);

    // 2. Wrap: prepend task creation, append join. Body must be a Compound.
    let orig = std::mem::replace(body, AstStmt {
        kind: AstStmtKind::Compound, line, col,
        data: AstStmtData::Compound(Vec::new()),
    });
    let create_call = AstExpr {
        kind: AstExprKind::FuncCall, expr_type: None, line, col,
        data: AstExprData::FuncCall {
            func: None,
            name: "ovic_task_create".to_string(),
            callee: None,
            args: vec![
                AstExpr { kind: AstExprKind::Int, expr_type: None, line, col, data: AstExprData::Int(0) }, // entry: NULL (M1)
                AstExpr { kind: AstExprKind::VarRef, expr_type: None, line, col, data: AstExprData::VarRef { sym: None, name: "self".to_string() } },
                AstExpr { kind: AstExprKind::Int, expr_type: None, line, col, data: AstExprData::Int(0) }, // frame_size: 0 (M1)
            ],
        },
    };
    let task_decl = AstDecl {
        kind: ovic_ast::AstDeclKind::Variable,
        name: Some("__ovic_task".to_string()),
        line, col,
        data: AstDeclData::Variable {
            var_type: Some(Box::new({
                let mut inner = AstType::new(TypePrim::Named);
                inner.name = Some("NPTask".to_string());
                let mut t = AstType::new(TypePrim::Named);
                t.is_pointer = true;
                t.subtype = Some(Box::new(inner));
                t
            })),
            init: Some(Box::new(create_call)),
            is_static: false, is_extern: false, is_const: false,
            is_block_qual: false, is_weak: false,
            next: None,
        },
        attributes: Vec::new(),
    };
    let join_call = AstStmt {
        kind: AstStmtKind::Expr, line, col,
        data: AstStmtData::Expr(AstExpr {
            kind: AstExprKind::FuncCall, expr_type: None, line, col,
            data: AstExprData::FuncCall {
                func: None,
                name: "ovic_task_join".to_string(),
                callee: None,
                args: vec![AstExpr {
                    kind: AstExprKind::VarRef, expr_type: None, line, col,
                    data: AstExprData::VarRef { sym: None, name: "__ovic_task".to_string() },
                }],
            },
        }),
    };
    let task_create_stmt = AstStmt { kind: AstStmtKind::Decl, line, col, data: AstStmtData::Decl(task_decl) };
    let mut out: Vec<AstStmt> = Vec::new();
    out.push(task_create_stmt);
    match orig.data {
        AstStmtData::Compound(inner) => out.extend(inner), // splice for one clean scope
        other => out.push(AstStmt { kind: orig.kind, line: orig.line, col: orig.col, data: other }),
    }
    out.push(join_call);
    *body = AstStmt { kind: AstStmtKind::Compound, line, col, data: AstStmtData::Compound(out) };
}

/// Apply the desugar over the whole unit: rewrite every async method body.
pub fn desugar_unit(unit: &mut ovic_ast::AstUnit) {
    for decl in &mut unit.decls {
        if let AstDeclData::Class { methods, .. } = &mut decl.data {
            for m in methods.iter_mut() {
                if method_is_async(m) {
                    if let AstDeclData::Method { body: Some(ref mut b), .. } = &mut m.data {
                        desugar_method_body(b);
                    }
                }
            }
        }
    }
    // `[task start]` appears in CALLER bodies too (e.g. main), which the
    // async-method pass above never visits — rewrite it unit-wide. (M1 path:
    // empty context — the checker-annotated expr_type decides. Not hosted:
    // M1 has no entry drive, so a start stays an enqueue.)
    let mut ctx = AsyncCtx::default();
    for decl in &mut unit.decls {
        lower_task_starts_decl(decl, &mut ctx, false);
    }
}

/// `[task start]` on an NPTask handle → `ovic_task_start(task)`. The handle
/// is a runtime struct, not an NPObject — a vtable message send would be
/// undefined (doc/async_nptask_plan.md, stage D).
fn is_task_start_send(e: &AstExpr) -> bool {
    if let AstExprData::MsgSend { receiver, args, .. } = &e.data {
        if args.is_empty() {
            if let Some(t) = &receiver.expr_type {
                if t.is_pointer && t.name.as_deref().map_or(false, |n| n == "NPTask" || n.starts_with("NPTask<")) {
                    return true;
                }
            }
        }
    }
    false
}

/// `drive` = this expression sits at STATEMENT position of a hosted unit: a
/// `[t start]` there becomes a blocking drive (`ovic_task_await`) instead of a
/// bare enqueue. The doc's entry idiom (`NPTask *t = [f run]; [t start];`)
/// must run while the receiver is still ARC-alive — an end-of-main pump cannot
/// guarantee that (ARC's scope-end release lands first: SIGSEGV, lldb-verified
/// on golden 37). Freestanding passes `drive = false` (enqueue only).
fn lower_task_starts_expr(e: &mut AstExpr, ctx: &AsyncCtx, drive: bool) {
    if is_task_start_send(e) || {
        // `[t start]` with t a known task var (desugar precedes the checker,
        // so expr_type is unannotated — decide from the collected context).
        matches!(&e.data, AstExprData::MsgSend { receiver, .. }
            if matches!(&receiver.data, AstExprData::VarRef { name, .. }
                if ctx.task_vars.contains(name)))
    } {
        let line = e.line;
        let col = e.col;
        let taken = std::mem::replace(e, int_expr(0, line, col));
        let AstExprData::MsgSend { receiver, .. } = taken.data else { unreachable!() };
        let start_fn = if drive { "ovic_task_await" } else { "ovic_task_start" };
        *e = AstExpr {
            kind: AstExprKind::FuncCall, expr_type: None, line, col,
            data: AstExprData::FuncCall {
                func: None, name: start_fn.to_string(), callee: None,
                args: vec![*receiver],
            },
        };
        return;
    }
    match &mut e.data {
        AstExprData::MsgSend { receiver, args, .. } => {
            lower_task_starts_expr(receiver, ctx, false);
            for a in args.iter_mut() { lower_task_starts_expr(a, ctx, false); }
        }
        AstExprData::FuncCall { callee, args, .. } => {
            if let Some(c) = callee { lower_task_starts_expr(c, ctx, false); }
            for a in args.iter_mut() { lower_task_starts_expr(a, ctx, false); }
        }
        AstExprData::Unary { operand, .. } => lower_task_starts_expr(operand, ctx, false),
        AstExprData::Binary { left, right, .. } => {
            lower_task_starts_expr(left, ctx, false);
            lower_task_starts_expr(right, ctx, false);
        }
        AstExprData::Assign { target, value } => {
            lower_task_starts_expr(target, ctx, false);
            lower_task_starts_expr(value, ctx, false);
        }
        AstExprData::Comma(parts) => { for p in parts.iter_mut() { lower_task_starts_expr(p, ctx, false); } }
        AstExprData::Paren(inner) | AstExprData::Cast { expr: inner, .. } => lower_task_starts_expr(inner, ctx, false),
        _ => {}
    }
}

fn lower_task_starts_stmt(s: &mut AstStmt, ctx: &mut AsyncCtx, hosted: bool) {
    match &mut s.data {
        AstStmtData::Expr(e) => lower_task_starts_expr(e, ctx, hosted),
        AstStmtData::Return(Some(e)) => lower_task_starts_expr(e, ctx, false),
        AstStmtData::If { cond, then, else_ } => {
            lower_task_starts_expr(cond, ctx, false);
            lower_task_starts_stmt(then, ctx, hosted);
            if let Some(e) = else_ { lower_task_starts_stmt(e, ctx, hosted); }
        }
        AstStmtData::While { cond, body } | AstStmtData::Do { body, cond } => {
            lower_task_starts_expr(cond, ctx, false);
            lower_task_starts_stmt(body, ctx, hosted);
        }
        AstStmtData::For { init, cond, incr, body } => {
            if let Some(i) = init { lower_task_starts_stmt(i, ctx, hosted); }
            if let Some(c) = cond { lower_task_starts_expr(c, ctx, false); }
            if let Some(u) = incr { lower_task_starts_expr(u, ctx, false); }
            lower_task_starts_stmt(body, ctx, hosted);
        }
        AstStmtData::Compound(stmts) => { for st in stmts.iter_mut() { lower_task_starts_stmt(st, ctx, hosted); } }
        AstStmtData::Decl(d) => lower_task_starts_decl(d, ctx, hosted),
        _ => {}
    }
}

fn lower_task_starts_decl(d: &mut AstDecl, ctx: &mut AsyncCtx, hosted: bool) {
    match &mut d.data {
        AstDeclData::Variable { var_type, init, next, .. } => {
            // Collect task-handle variables unit-wide: any variable whose
            // declared type is NPTask* (source order — later `[t start]`
            // sends in any body, main included, can then match).
            if let Some(vt) = var_type.as_deref() {
                if vt.is_pointer && vt.name.as_deref().map_or(false, |n| n == "NPTask" || n.starts_with("NPTask<")) {
                    if let Some(n) = &d.name { ctx.task_vars.insert(n.clone()); }
                }
            }
            // A variable initialiser is not statement position: keep enqueue.
            if let Some(i) = init { lower_task_starts_expr(i, ctx, false); }
            if let Some(n) = next { lower_task_starts_decl(n, ctx, hosted); }
        }
        AstDeclData::Function { body: Some(b), .. } => lower_task_starts_stmt(b, ctx, hosted),
        AstDeclData::Class { methods, .. } => {
            for m in methods.iter_mut() {
                if let AstDeclData::Method { body: Some(b), .. } = &mut m.data {
                    lower_task_starts_stmt(b, ctx, hosted);
                }
            }
        }
        _ => {}
    }
}

// ─── Milestone 2: real state machine (frame + entry fn + driver) ────────────

use ovic_ast::{AstDeclKind, AstStmtKind, AstType};
use ovic_cst::TypePrim;

/// Named struct field type for the frame: `struct <Method>_frame`.
fn frame_type_name(symbol: &str) -> String {
    format!("{}_frame", sanitize_symbol(symbol))
}

fn sanitize_symbol(sym: &str) -> String {
    sym.chars().map(|c| match c {
        ':' => '_', ' ' => '_', '<' => '_', '>' => '_', '*' => 'p', ',' => '_',
        '(' | ')' => '_',
        other => other,
    }).collect()
}

/// Build the M2 state-machine form for one async method:
///   - a `struct <M>_frame { params + locals }` typedef (top-level decl)
///   - a `static int <M>_state(NPTask *t)` entry (top-level decl)
///   - rewrite the method body to: create + fill params + join + return result
///
/// In M2's synchronous-drive model the entry runs all states in one resume
/// (each awaited expression evaluates synchronously inside its state), so
/// observable behavior matches M1 — but the frame carries the params/locals
/// and the entry is externally drivable (M3's scheduler can call resume).
fn desugar_method_m2(
    method_symbol: &str,
    params: &[(String, AstType)],
    body: &mut AstStmt,
    top_decls: &mut Vec<AstDecl>,
    return_type: Option<&AstType>,
    async_ctx: &mut AsyncCtx,
) {
    // 1. Frame carries ONLY the params in M2: the synchronous-drive model
    // never suspends between a local's declaration and its last use, so
    // locals live on the C stack inside the entry. (M3 lifts locals that
    // actually cross an await.)
    let mut frame_fields: Vec<(String, AstType)> = Vec::new();
    for (n, t) in params {
        if !frame_fields.iter().any(|(name, _)| name == n) {
            frame_fields.push((n.clone(), t.clone()));
        }
    }
    // Zero-param methods get an empty frame struct; codegen skips empty
    // struct bodies, leaving `sizeof(struct …_frame)` on an incomplete type.
    // Pad so the frame is always a complete type.
    if frame_fields.is_empty() {
        frame_fields.push(("__ovic_pad".to_string(), AstType::new(TypePrim::Char)));
    }

    let ftname = frame_type_name(method_symbol);
    let flat = sanitize_symbol(method_symbol);
    let entry_name = format!("ovic_async_state_{}", flat);

    // 2. Frame typedef decl.
    // Fields use the Ivar variant: codegen's Struct branch reads
    // AstDeclData::Ivar.ivar_type for field types (Variable is skipped).
    let fields_decls: Vec<AstDecl> = frame_fields.iter().map(|(n, t)| AstDecl {
        kind: AstDeclKind::Ivar,
        name: Some(n.clone()),
        line: body.line, col: body.col,
        data: AstDeclData::Ivar {
            ivar_sym: None,
            ivar_type: Some(Box::new(t.clone())),
            is_weak: false,
        },
        attributes: Vec::new(),
    }).collect();
    let frame_decl = AstDecl {
        kind: AstDeclKind::Struct,
        name: Some(ftname.clone()),
        line: body.line, col: body.col,
        data: AstDeclData::Aggregate { fields: fields_decls, is_union: false },
        attributes: Vec::new(),
    };
    top_decls.push(frame_decl);

    // 3. Rewrite `await e` → state-transition + synchronous eval:
    //    ((t)->state = N + 1, (e)) with N from a per-method counter.
    //    Each Await also marks its enclosing Decl to persist into the frame —
    //    in M2 the frame is the single source of truth for locals: rewrite all
    //    local VarRefs/Decls to frame access.
    //    Simplification (M2): locals are read/written through `f->` only for
    //    params (needed by the entry across resumes). Body-internal locals
    //    stay on the C stack: within one synchronous resume there is no
    //    suspension between their declaration and last use, so the frame only
    //    needs params. awaited call results flow through expressions directly.
    //    (M3 lifts cross-await locals when real suspension exists.)
    let _ = &frame_fields;

    // 3b. Lower awaits with a state counter: each await bumps the counter so
    // the state machine has one state per suspension point (all running
    // synchronously in M2's drive model).
    let mut state_counter: i32 = 1;
    let mut ctx = std::mem::take(async_ctx);
    lower_awaits_m2(body, &entry_name, &mut state_counter, &mut ctx);
    *async_ctx = ctx;
    // Final state number (the completion state).
    let final_state = state_counter;

    // 4. Entry function:
    //   static int ovic_async_state_<M>(NPTask *__ovic_task) {
    //     struct <M>_frame *__ovic_f = (struct <M>_frame *)__ovic_task->frame;
    //     switch (__ovic_task->state) {
    //       case 1: ...body with lowered awaits...
    //       case <final>: __ovic_task->state = -1; return 1;
    //     }
    //     return 1;
    //   }
    // The body runs in state 1 (one synchronous pass). States 2..final-1 are
    // the per-await re-entry points; in M2's drive model resume() loops until
    // finished, and since the body completes within state 1, later states are
    // only entered if a real suspension (M3) yields first.
    let orig_stmt = std::mem::replace(body, AstStmt {
        kind: AstStmtKind::Compound, line: body.line, col: body.col,
        data: AstStmtData::Compound(Vec::new()),
    });
    let (o_kind, o_line, o_col, body_stmts) = match orig_stmt.data {
        AstStmtData::Compound(inner) => (orig_stmt.kind, orig_stmt.line, orig_stmt.col, inner),
        other => (orig_stmt.kind, orig_stmt.line, orig_stmt.col, vec![AstStmt { kind: orig_stmt.kind, line: orig_stmt.line, col: orig_stmt.col, data: other }]),
    };
    let (body_kind, body_line, body_col) = (o_kind, o_line, o_col);
    let _ = (body_kind, body_line, body_col);

    // Build the switch: case 1 = body; case final = finish.
    // The entry can only see the task: rewrite param references to
    // `__ovic_f->param` (the frame) and `self` to `__ovic_task->self_obj`,
    // then rewrite returns to
    // the completion protocol (result/state/return 1).
    let mut case_body_stmts = body_stmts;
    // Only params are rewritten to `__ovic_f->param` (the frame carries them
    // across the entry boundary). Body-internal locals stay on the C stack:
    // in M2's synchronous-drive model there is no suspension between their
    // declaration and last use, so rewriting their reads to the (never
    // written) frame fields would read garbage.
    let params_only: Vec<(String, AstType)> = params.to_vec();
    for stmt in case_body_stmts.iter_mut() {
        rewrite_entry_refs_stmt(stmt, &params_only);
    }
    rewrite_returns_to_result(&mut case_body_stmts);
    // struct <FT> *__ovic_f = (struct <FT> *)__ovic_task->frame;  (case 1 head)
    let f_decl = AstStmt {
        kind: AstStmtKind::Decl, line: body.line, col: body.col,
        data: AstStmtData::Decl(AstDecl {
            kind: AstDeclKind::Variable,
            name: Some("__ovic_f".to_string()),
            line: body.line, col: body.col,
            data: AstDeclData::Variable {
                var_type: Some(Box::new({
                    let mut st = AstType::new(TypePrim::Named);
                    st.name = Some(ftname.clone());
                    st.is_struct = true;
                    let mut t = AstType::new(TypePrim::Named);
                    t.is_pointer = true;
                    t.subtype = Some(Box::new(st));
                    t
                })),
                init: Some(Box::new(AstExpr {
                    kind: AstExprKind::Cast, expr_type: None, line: body.line, col: body.col,
                    data: AstExprData::Cast {
                        target_type: {
                            let mut st = AstType::new(TypePrim::Named);
                            st.name = Some(ftname.clone());
                            st.is_struct = true;
                            let mut t = AstType::new(TypePrim::Named);
                            t.is_pointer = true;
                            t.subtype = Some(Box::new(st));
                            t
                        },
                        expr: Box::new(member_expr("__ovic_task", "frame", body.line, body.col)),
                    },
                })),
                is_static: false, is_extern: false, is_const: false,
                is_block_qual: false, is_weak: false, next: None,
            },
            attributes: Vec::new(),
        }),
    };
    case_body_stmts.insert(0, f_decl);
    // Append: __ovic_task->state = -1; return 1;
    case_body_stmts.push(AstStmt {
        kind: AstStmtKind::Expr, line: body.line, col: body.col,
        data: AstStmtData::Expr(AstExpr {
            kind: AstExprKind::Assign, expr_type: None, line: body.line, col: body.col,
            data: AstExprData::Assign {
                target: Box::new(member_expr("__ovic_task", "state", body.line, body.col)),
                value: Box::new(int_expr(-1, body.line, body.col)),
            },
        }),
    });
    case_body_stmts.push(AstStmt {
        kind: AstStmtKind::Return, line: body.line, col: body.col,
        data: AstStmtData::Return(Some(Box::new(int_expr(1, body.line, body.col)))),
    });

    let case1 = AstStmt {
        kind: AstStmtKind::Case, line: body.line, col: body.col,
        data: AstStmtData::Case {
            value: Box::new(int_expr(1, body.line, body.col)),
            body: Box::new(AstStmt {
                kind: AstStmtKind::Compound, line: body.line, col: body.col,
                data: AstStmtData::Compound(case_body_stmts),
            }),
        },
    };
    let case_final = AstStmt {
        kind: AstStmtKind::Case, line: body.line, col: body.col,
        data: AstStmtData::Case {
            value: Box::new(int_expr(final_state.max(2) as i64, body.line, body.col)),
            body: Box::new(AstStmt {
                kind: AstStmtKind::Compound, line: body.line, col: body.col,
                data: AstStmtData::Compound(vec![
                    AstStmt {
                        kind: AstStmtKind::Expr, line: body.line, col: body.col,
                        data: AstStmtData::Expr(AstExpr {
                            kind: AstExprKind::Assign, expr_type: None, line: body.line, col: body.col,
                            data: AstExprData::Assign {
                                target: Box::new(member_expr("__ovic_task", "state", body.line, body.col)),
                                value: Box::new(int_expr(-1, body.line, body.col)),
                            },
                        }),
                    },
                    AstStmt {
                        kind: AstStmtKind::Return, line: body.line, col: body.col,
                        data: AstStmtData::Return(Some(Box::new(int_expr(1, body.line, body.col)))),
                    },
                ]),
            }),
        },
    };
    let switch_stmt = AstStmt {
        kind: AstStmtKind::Switch, line: body.line, col: body.col,
        data: AstStmtData::Switch {
            expr: Box::new(member_expr("__ovic_task", "state", body.line, body.col)),
            body: Box::new(AstStmt {
                kind: AstStmtKind::Compound, line: body.line, col: body.col,
                data: AstStmtData::Compound(vec![case1, case_final]),
            }),
        },
    };

    // Entry params: (NPTask *__ovic_task). The name is deliberately RESERVED:
    // a bare `t` collides with a same-named user local in the rewritten body —
    // `NPTask<int> *t = [self cached]; @await t;` expanded `t->self_obj`
    // against the *uninitialised* new local (SIGSEGV at 0x18, lldb-confirmed).
    let entry_param = cst_param("NPTask", "__ovic_task");
    let entry_decl = AstDecl {
        kind: AstDeclKind::Function,
        name: Some(entry_name.clone()),
        line: body.line, col: body.col,
        data: AstDeclData::Function {
            func_sym: None,
            return_type: Some(Box::new(AstType::new(TypePrim::Int))),
            params: Some(Box::new(entry_param)),
            body: Some(Box::new(AstStmt {
                kind: AstStmtKind::Compound, line: body.line, col: body.col,
                data: AstStmtData::Compound(vec![
                    switch_stmt,
                    // Fallback: a state outside 1..=final (corrupt task, or a
                    // resume after completion) must not fall off the end of a
                    // non-void function — UB, clang -Wreturn-type (external
                    // review finding). Treat it as completion so the driver's
                    // join loop terminates instead of spinning.
                    AstStmt {
                        kind: AstStmtKind::Expr, line: body.line, col: body.col,
                        data: AstStmtData::Expr(AstExpr {
                            kind: AstExprKind::Assign, expr_type: None, line: body.line, col: body.col,
                            data: AstExprData::Assign {
                                target: Box::new(member_expr("__ovic_task", "state", body.line, body.col)),
                                value: Box::new(int_expr(-1, body.line, body.col)),
                            },
                        }),
                    },
                    AstStmt {
                        kind: AstStmtKind::Return, line: body.line, col: body.col,
                        data: AstStmtData::Return(Some(Box::new(int_expr(1, body.line, body.col)))),
                    },
                ]),
            })),
            has_variadic: false,
            throws: None,
            async_marker: false,
        },
        attributes: Vec::new(),
    };
    top_decls.push(entry_decl);

    // NPTask design (doc/async_nptask_plan.md, stage D): the driver is LAZY —
    // create the task, fill the frame params, and return the handle. No join:
    // execution starts at [task start] / @await. The method's C return type is
    // NPTask * (codegen renders NPTask<T> as NPTask *).
    let create_call = AstExpr {
        kind: AstExprKind::FuncCall, expr_type: None, line: body.line, col: body.col,
        data: AstExprData::FuncCall {
            func: None, name: "ovic_task_create".to_string(), callee: None,
            args: vec![
                ident_expr(&entry_name, body.line, body.col),
                ident_expr("self", body.line, body.col),
                AstExpr {
                    kind: AstExprKind::FuncCall, expr_type: None, line: body.line, col: body.col,
                    data: AstExprData::FuncCall {
                        func: None, name: "__builtin_sizeof_frame".to_string(), callee: None, args: vec![],
                    },
                },
            ],
        },
    };
    let _ = create_call; // replaced below by sizeof-based version
    // sizeof expr: AST Sizeof { type_expr, expr: None } → codegen emits sizeof(type)
    let sizeof_frame = AstExpr {
        kind: AstExprKind::Sizeof, expr_type: None, line: body.line, col: body.col,
        data: AstExprData::Sizeof {
            type_expr: {
                let mut t = AstType::new(TypePrim::Named);
                t.name = Some(ftname.clone());
                t.is_struct = true;
                t
            },
            expr: None,
        },
    };
    let create_call = AstExpr {
        kind: AstExprKind::FuncCall, expr_type: None, line: body.line, col: body.col,
        data: AstExprData::FuncCall {
            func: None, name: "ovic_task_create".to_string(), callee: None,
            args: vec![
                ident_expr(&entry_name, body.line, body.col),
                ident_expr("self", body.line, body.col),
                sizeof_frame,
            ],
        },
    };
    // Driver: task create FIRST, then fill frame params, then join.
    // ((struct <M>_frame *)__ovic_task->frame)->param = param;
    let mut pre: Vec<AstStmt> = Vec::new();
    let task_decl = AstDecl {
        kind: AstDeclKind::Variable,
        name: Some("__ovic_task".to_string()),
        line: body.line, col: body.col,
        data: AstDeclData::Variable {
            var_type: Some(Box::new(ovic_task_ptr_type())),
            init: Some(Box::new(create_call)),
            is_static: false, is_extern: false, is_const: false,
            is_block_qual: false, is_weak: false, next: None,
        },
        attributes: Vec::new(),
    };
    pre.push(AstStmt { kind: AstStmtKind::Decl, line: body.line, col: body.col, data: AstStmtData::Decl(task_decl) });
    for (pname, _) in params {
        // obj expr: (struct FT *)(__ovic_task->frame)
        let frame_access = AstExpr {
            kind: AstExprKind::Cast, expr_type: None, line: body.line, col: body.col,
            data: AstExprData::Cast {
                target_type: {
                    let mut st = AstType::new(TypePrim::Named);
                    st.name = Some(ftname.clone());
                    st.is_struct = true;
                    let mut t = AstType::new(TypePrim::Named);
                    t.is_pointer = true;
                    t.subtype = Some(Box::new(st));
                    t
                },
                expr: Box::new(member_expr("__ovic_task", "frame", body.line, body.col)),
            },
        };
        pre.push(AstStmt {
            kind: AstStmtKind::Expr, line: body.line, col: body.col,
            data: AstStmtData::Expr(AstExpr {
                kind: AstExprKind::Assign, expr_type: None, line: body.line, col: body.col,
                data: AstExprData::Assign {
                    target: Box::new(AstExpr {
                        kind: AstExprKind::IvarRef, expr_type: None, line: body.line, col: body.col,
                        data: AstExprData::IvarRef {
                            obj: Box::new(frame_access),
                            ivar: Some(pname.clone()), cls: None,
                        },
                    }),
                    value: Box::new(ident_expr(pname, body.line, body.col)),
                },
            }),
        });
    }

    // Join: void case → plain call; else result = (T)(unsigned long)join(t)
    // via a result variable the return statement references.
    let is_void = return_type.map_or(true, |t| t.prim == TypePrim::Void && !t.is_pointer);
    let mut post: Vec<AstStmt> = Vec::new();
    if is_void {
        post.push(call_stmt("ovic_task_join", vec![ident_expr("__ovic_task", body.line, body.col)], body.line, body.col));
    } else {
        // long __ovic_r = (long)ovic_task_join(t);
        post.push(AstStmt {
            kind: AstStmtKind::Decl, line: body.line, col: body.col,
            data: AstStmtData::Decl(AstDecl {
                kind: AstDeclKind::Variable,
                name: Some("__ovic_r".to_string()),
                line: body.line, col: body.col,
                data: AstDeclData::Variable {
                    var_type: Some(Box::new(AstType::new(TypePrim::Long))),
                    init: Some(Box::new(AstExpr {
                        kind: AstExprKind::Cast, expr_type: None, line: body.line, col: body.col,
                        data: AstExprData::Cast {
                            target_type: AstType::new(TypePrim::Long),
                            expr: Box::new(call_expr("ovic_task_join", vec![ident_expr("__ovic_task", body.line, body.col)], body.line, body.col)),
                        },
                    })),
                    is_static: false, is_extern: false, is_const: false,
                    is_block_qual: false, is_weak: false, next: None,
                },
                attributes: Vec::new(),
            }),
        });
    }
    // Lazy driver: no join — the body is just create + param fill + return
    // the handle. (The old eager driver's join/return-cast is gone; results
    // are read via ovic_task_await.)
    let _ = is_void;
    let mut out = pre;
    out.push(AstStmt {
        kind: AstStmtKind::Return, line: body.line, col: body.col,
        data: AstStmtData::Return(Some(Box::new(ident_expr("__ovic_task", body.line, body.col)))),
    });
    *body = AstStmt { kind: AstStmtKind::Compound, line: body.line, col: body.col, data: AstStmtData::Compound(out) };
}

// ─── M2 helpers ──────────────────────────────────────────────────────────────

/// Lower awaits for the M2 entry: `await e` →
/// `(t->state = N, (e))` where N is the next state number. In M2's drive
/// model the state write is bookkeeping only (the body completes within one
/// resume); states still exist so M3 can insert real suspension points.
/// Desugar context: which selectors are async methods (colon-stripped) and
/// which local variables are known to hold task handles (populated during the
/// walk, in source order). Desugar runs BEFORE the checker, so expr_type is
/// not annotated — all "does this yield a task?" decisions come from here.
#[derive(Default)]
struct AsyncCtx {
    async_sels: std::collections::HashSet<String>,
    task_vars: std::collections::HashSet<String>,
}

/// Does this expression evaluate to an NPTask handle? (async call whose
/// selector is a known async method, or a variable recorded as a task handle)
fn expr_yields_task(e: &AstExpr, ctx: &AsyncCtx) -> bool {
    match &e.data {
        AstExprData::VarRef { name, .. } => ctx.task_vars.contains(name),
        AstExprData::MsgSend { selector, .. } => {
            ctx.async_sels.contains(&selector.replace(':', ""))
        }
        AstExprData::Paren(inner) | AstExprData::Cast { expr: inner, .. } => {
            expr_yields_task(inner, ctx)
        }
        _ => false,
    }
}

fn lower_awaits_m2(s: &mut AstStmt, entry: &str, state: &mut i32, ctx: &mut AsyncCtx) {
    match &mut s.data {
        AstStmtData::Expr(e) => lower_awaits_m2_expr(e, state, ctx),
        AstStmtData::Compound(stmts) => { for st in stmts { lower_awaits_m2(st, entry, state, ctx); } }
        AstStmtData::If { cond, then, else_ } => {
            lower_awaits_m2_expr(cond, state, ctx);
            lower_awaits_m2(then, entry, state, ctx);
            if let Some(e) = else_ { lower_awaits_m2(e, entry, state, ctx); }
        }
        AstStmtData::While { cond, body } | AstStmtData::Do { body, cond } => {
            lower_awaits_m2_expr(cond, state, ctx);
            lower_awaits_m2(body, entry, state, ctx);
        }
        AstStmtData::For { init, cond, incr, body } => {
            if let Some(i) = init { lower_awaits_m2(i, entry, state, ctx); }
            if let Some(c) = cond { lower_awaits_m2_expr(c, state, ctx); }
            if let Some(u) = incr { lower_awaits_m2_expr(u, state, ctx); }
            lower_awaits_m2(body, entry, state, ctx);
        }
        AstStmtData::Return(Some(e)) => lower_awaits_m2_expr(e, state, ctx),
        AstStmtData::Decl(d) => lower_awaits_m2_decl(d, state, ctx),
        _ => {}
    }
    let _ = entry;
}

fn lower_awaits_m2_decl(d: &mut AstDecl, state: &mut i32, ctx: &mut AsyncCtx) {
    if let AstDeclData::Variable { var_type, init, next, .. } = &mut d.data {
        // A variable DECLARED as an NPTask handle is a task var for the rest of
        // the walk regardless of its initializer: a sync method may hand back a
        // task object (`NPTask<int> *t = [self loadCachedValue];`), and the
        // design allows `@await` on any `NPTask<T> *` expression — a variable,
        // ivar or return value — not only on the result of an async call
        // (doc/async_nptask_plan.md §调用点语义). The initializer-shape probe
        // below cannot see this: its selector is not an async method.
        if let Some(vt) = var_type.as_deref() {
            if vt.is_pointer
                && vt.name.as_deref().map_or(false, |n| n == "NPTask" || n.starts_with("NPTask<"))
            {
                if let Some(n) = &d.name { ctx.task_vars.insert(n.clone()); }
            }
        }
        if let Some(i) = init {
            lower_awaits_m2_expr(i, state, ctx);
            // A variable initialized from a task-yielding expression becomes
            // a task handle for the rest of the walk (source order).
            if expr_yields_task(i, ctx) {
                if let Some(n) = &d.name { ctx.task_vars.insert(n.clone()); }
            }
        }
        if let Some(n) = next { lower_awaits_m2_decl(n, state, ctx); }
    }
}

fn lower_awaits_m2_expr(e: &mut AstExpr, state: &mut i32, ctx: &mut AsyncCtx) {
    let line = e.line;
    let col = e.col;
    match &mut e.data {
        AstExprData::Await(inner) => {
            lower_awaits_m2_expr(inner, state, ctx);
            *state += 1;
            let n = *state;
            // (t->state = N, (long)ovic_task_await(e))
            // NPTask design (stage D): async calls are LAZY — the awaited
            // expression evaluates to an NPTask handle, and the await must
            // drive it (ovic_task_await blocks/drives inline, caches the
            // result) instead of synchronously evaluating the handle.
            let state_assign = AstExpr {
                kind: AstExprKind::Assign, expr_type: None, line, col,
                data: AstExprData::Assign {
                    target: Box::new(member_expr("__ovic_task", "state", line, col)),
                    value: Box::new(int_expr(n as i64, line, col)),
                },
            };
            let taken = std::mem::replace(&mut **inner, int_expr(0, line, col));
            // Only wrap when the awaited expression yields a task handle.
            // NOTE: desugar runs BEFORE the checker, so expr_type is not
            // annotated yet — decide from the AST shape: a message send whose
            // selector names an async method, or a variable known to hold a
            // task handle (both collected unit-wide in desugar_unit_m2).
            let yields_task = expr_yields_task(&taken, ctx);
            if !yields_task {
                *e = AstExpr {
                    kind: AstExprKind::Comma, expr_type: None, line, col,
                    data: AstExprData::Comma(vec![state_assign, taken]),
                };
                return;
            }
            let await_call = AstExpr {
                kind: AstExprKind::FuncCall, expr_type: None, line, col,
                data: AstExprData::FuncCall {
                    func: None, name: "ovic_task_await".to_string(), callee: None,
                    args: vec![taken],
                },
            };
            let cast = AstExpr {
                kind: AstExprKind::Cast, expr_type: None, line, col,
                data: AstExprData::Cast {
                    target_type: AstType::new(TypePrim::Long),
                    expr: Box::new(await_call),
                },
            };
            *e = AstExpr {
                kind: AstExprKind::Comma, expr_type: None, line, col,
                data: AstExprData::Comma(vec![state_assign, cast]),
            };
        }
        AstExprData::MsgSend { receiver, args, .. } => {
            lower_awaits_m2_expr(receiver, state, ctx);
            for a in args.iter_mut() { lower_awaits_m2_expr(a, state, ctx); }
        }
        AstExprData::FuncCall { callee, args, .. } => {
            if let Some(c) = callee { lower_awaits_m2_expr(c, state, ctx); }
            for a in args.iter_mut() { lower_awaits_m2_expr(a, state, ctx); }
        }
        AstExprData::Unary { operand, .. } => lower_awaits_m2_expr(operand, state, ctx),
        AstExprData::Binary { left, right, .. } => {
            lower_awaits_m2_expr(left, state, ctx);
            lower_awaits_m2_expr(right, state, ctx);
        }
        AstExprData::Assign { target, value } => {
            lower_awaits_m2_expr(target, state, ctx);
            lower_awaits_m2_expr(value, state, ctx);
        }
        AstExprData::Cast { expr, .. } => lower_awaits_m2_expr(expr, state, ctx),
        AstExprData::Ternary { cond, then, else_ } => {
            lower_awaits_m2_expr(cond, state, ctx);
            lower_awaits_m2_expr(then, state, ctx);
            lower_awaits_m2_expr(else_, state, ctx);
        }
        AstExprData::Comma(exprs) => { for x in exprs.iter_mut() { lower_awaits_m2_expr(x, state, ctx); } }
        AstExprData::Subscript { object, key } => {
            lower_awaits_m2_expr(object, state, ctx);
            lower_awaits_m2_expr(key, state, ctx);
        }
        AstExprData::InitList(items) | AstExprData::ArrayLit(items) => {
            for x in items.iter_mut() { lower_awaits_m2_expr(x, state, ctx); }
        }
        _ => {}
    }
}

/// Rewrite references inside the entry body: params → `__ovic_f->param`,
/// `self` → `__ovic_task->self_obj`. The entry only receives the task, so all
/// param/self access must go through the frame / task.
fn rewrite_entry_refs_stmt(s: &mut AstStmt, params: &[(String, AstType)]) {
    match &mut s.data {
        AstStmtData::Expr(e) => rewrite_entry_refs_expr(e, params),
        AstStmtData::Compound(stmts) => {
            for st in stmts { rewrite_entry_refs_stmt(st, params); }
        }
        AstStmtData::Decl(d) => rewrite_entry_refs_decl(d, params),
        AstStmtData::If { cond, then, else_ } => {
            rewrite_entry_refs_expr(cond, params);
            rewrite_entry_refs_stmt(then, params);
            if let Some(e) = else_ { rewrite_entry_refs_stmt(e, params); }
        }
        AstStmtData::While { cond, body } | AstStmtData::Do { body, cond } => {
            rewrite_entry_refs_expr(cond, params);
            rewrite_entry_refs_stmt(body, params);
        }
        AstStmtData::For { init, cond, incr, body } => {
            if let Some(i) = init { rewrite_entry_refs_stmt(i, params); }
            if let Some(c) = cond { rewrite_entry_refs_expr(c, params); }
            if let Some(u) = incr { rewrite_entry_refs_expr(u, params); }
            rewrite_entry_refs_stmt(body, params);
        }
        AstStmtData::Return(Some(e)) => rewrite_entry_refs_expr(e, params),
        _ => {}
    }
}

fn rewrite_entry_refs_decl(d: &mut AstDecl, params: &[(String, AstType)]) {
    if let AstDeclData::Variable { init, next, .. } = &mut d.data {
        if let Some(i) = init { rewrite_entry_refs_expr(i, params); }
        if let Some(n) = next { rewrite_entry_refs_decl(n, params); }
    }
}

fn rewrite_entry_refs_expr(e: &mut AstExpr, params: &[(String, AstType)]) {
    let line = e.line;
    let col = e.col;
    match &mut e.data {
        AstExprData::VarRef { name, .. } => {
            if name == "self" {
                // __ovic_task->self_obj
                *e = member_expr("__ovic_task", "self_obj", line, col);
            } else if params.iter().any(|(n, _)| n == name) {
                // __ovic_f->param
                *e = member_expr("__ovic_f", name, line, col);
            }
        }
        AstExprData::MsgSend { receiver, args, .. } => {
            rewrite_entry_refs_expr(receiver, params);
            for a in args.iter_mut() { rewrite_entry_refs_expr(a, params); }
        }
        AstExprData::FuncCall { callee, args, .. } => {
            if let Some(c) = callee { rewrite_entry_refs_expr(c, params); }
            for a in args.iter_mut() { rewrite_entry_refs_expr(a, params); }
        }
        AstExprData::Unary { operand, .. } => rewrite_entry_refs_expr(operand, params),
        AstExprData::Binary { left, right, .. } => {
            rewrite_entry_refs_expr(left, params);
            rewrite_entry_refs_expr(right, params);
        }
        AstExprData::Assign { target, value } => {
            rewrite_entry_refs_expr(target, params);
            rewrite_entry_refs_expr(value, params);
        }
        AstExprData::Cast { expr, .. } => rewrite_entry_refs_expr(expr, params),
        AstExprData::Ternary { cond, then, else_ } => {
            rewrite_entry_refs_expr(cond, params);
            rewrite_entry_refs_expr(then, params);
            rewrite_entry_refs_expr(else_, params);
        }
        AstExprData::Comma(exprs) => {
            for x in exprs.iter_mut() { rewrite_entry_refs_expr(x, params); }
        }
        AstExprData::Subscript { object, key } => {
            rewrite_entry_refs_expr(object, params);
            rewrite_entry_refs_expr(key, params);
        }
        AstExprData::InitList(items) | AstExprData::ArrayLit(items) => {
            for x in items.iter_mut() { rewrite_entry_refs_expr(x, params); }
        }
        _ => {}
    }
}

/// Rewrite `return e;` inside the entry body to the completion protocol:
/// `t->result = (void *)(unsigned long)(e); t->state = -1; return 1;`
/// (void return → just `t->state = -1; return 1;`).
fn rewrite_returns_to_result(stmts: &mut Vec<AstStmt>) {
    for s in stmts.iter_mut() {
        rewrite_returns_stmt(s);
    }
}

fn rewrite_returns_stmt(s: &mut AstStmt) {
    let line = s.line;
    let col = s.col;
    match &mut s.data {
        AstStmtData::Return(ret) => {
            let taken = ret.take();
            let mut repl: Vec<AstStmt> = Vec::new();
            if let Some(e) = taken {
                // t->result = (void *)(unsigned long)(e);
                repl.push(AstStmt {
                    kind: AstStmtKind::Expr, line, col,
                    data: AstStmtData::Expr(AstExpr {
                        kind: AstExprKind::Assign, expr_type: None, line, col,
                        data: AstExprData::Assign {
                            target: Box::new(member_expr("__ovic_task", "result", line, col)),
                            value: Box::new(AstExpr {
                                kind: AstExprKind::Cast, expr_type: None, line, col,
                                data: AstExprData::Cast {
                                    target_type: named_type("void", true),
                                    expr: Box::new(AstExpr {
                                        kind: AstExprKind::Cast, expr_type: None, line, col,
                                        data: AstExprData::Cast {
                                            target_type: prim_type(TypePrim::Unsigned),
                                            expr: Box::new(*e),
                                        },
                                    }),
                                },
                            }),
                        },
                    }),
                });
            }
            repl.push(AstStmt {
                kind: AstStmtKind::Expr, line, col,
                data: AstStmtData::Expr(AstExpr {
                    kind: AstExprKind::Assign, expr_type: None, line, col,
                    data: AstExprData::Assign {
                        target: Box::new(member_expr("__ovic_task", "state", line, col)),
                        value: Box::new(int_expr(-1, line, col)),
                    },
                }),
            });
            repl.push(AstStmt {
                kind: AstStmtKind::Return, line, col,
                data: AstStmtData::Return(Some(Box::new(int_expr(1, line, col)))),
            });
            // Replace this single Return with a Compound of the repl statements.
            *s = AstStmt {
                kind: AstStmtKind::Compound, line, col,
                data: AstStmtData::Compound(repl),
            };
        }
        AstStmtData::Compound(stmts) => rewrite_returns_to_result(stmts),
        AstStmtData::If { then, else_, .. } => {
            rewrite_returns_stmt(then);
            if let Some(e) = else_ { rewrite_returns_stmt(e); }
        }
        AstStmtData::While { body, .. } | AstStmtData::Do { body, .. }
        | AstStmtData::Autoreleasepool(body) | AstStmtData::NoArc(body) => {
            rewrite_returns_stmt(body);
        }
        AstStmtData::For { init, body, .. } => {
            if let Some(i) = init { rewrite_returns_stmt(i); }
            rewrite_returns_stmt(body);
        }
        _ => {}
    }
}

// Expression builders shared by the M2 rewriter.

fn named_type(name: &str, is_ptr: bool) -> AstType {
    let mut inner = AstType::new(TypePrim::Named);
    inner.name = Some(name.to_string());
    if is_ptr {
        let mut t = AstType::new(TypePrim::Named);
        t.is_pointer = true;
        t.subtype = Some(Box::new(inner));
        t
    } else {
        inner
    }
}

fn prim_type(p: TypePrim) -> AstType { AstType::new(p) }

fn int_expr(v: i64, line: usize, col: usize) -> AstExpr {
    AstExpr { kind: AstExprKind::Int, expr_type: None, line, col, data: AstExprData::Int(v) }
}

fn ident_expr(name: &str, line: usize, col: usize) -> AstExpr {
    AstExpr {
        kind: AstExprKind::VarRef, expr_type: None, line, col,
        data: AstExprData::VarRef { sym: None, name: name.to_string() },
    }
}

fn member_expr(obj: &str, field: &str, line: usize, col: usize) -> AstExpr {
    // t->state / t->result — obj is an identifier expression.
    AstExpr {
        kind: AstExprKind::IvarRef, expr_type: None, line, col,
        data: AstExprData::IvarRef {
            obj: Box::new(ident_expr(obj, line, col)),
            ivar: Some(field.to_string()), cls: None,
        },
    }
}

fn call_expr(name: &str, args: Vec<AstExpr>, line: usize, col: usize) -> AstExpr {
    AstExpr {
        kind: AstExprKind::FuncCall, expr_type: None, line, col,
        data: AstExprData::FuncCall { func: None, name: name.to_string(), callee: None, args },
    }
}

fn call_stmt(name: &str, args: Vec<AstExpr>, line: usize, col: usize) -> AstStmt {
    AstStmt { kind: AstStmtKind::Expr, line, col, data: AstStmtData::Expr(call_expr(name, args, line, col)) }
}

fn ovic_task_ptr_type() -> AstType {
    let mut inner = AstType::new(TypePrim::Named);
    inner.name = Some("NPTask".to_string());
    let mut t = AstType::new(TypePrim::Named);
    t.is_pointer = true;
    t.subtype = Some(Box::new(inner));
    t
}

/// Build a CstParam for the entry function's `(NPTask *t)`.
fn cst_param(type_name: &str, name: &str) -> ovic_cst::CstParam {
    let mut t = ovic_cst::CstType::new(TypePrim::Named);
    t.name = Some(type_name.to_string());
    let mut ptr = ovic_cst::CstType::new(TypePrim::Named);
    ptr.is_pointer = true;
    ptr.name = Some(type_name.to_string());
    ptr.subtype = Some(Box::new(t));
    ovic_cst::CstParam {
        name: Some(name.to_string()),
        par_type: Some(Box::new(ptr)),
        external_name: None,
        next: None,
        attributes: Vec::new(),
    }
}

/// Apply the M2 desugar over the whole unit.
pub fn desugar_unit_m2(unit: &mut ovic_ast::AstUnit, hosted: bool) {
    // Pre-collect async method selectors (colon-stripped) so await lowering
    // can tell task-yielding calls from sync ones (expr_type is not
    // annotated yet — desugar runs before the checker).
    let mut async_sels: std::collections::HashSet<String> = std::collections::HashSet::new();
    for decl in unit.decls.iter() {
        if let AstDeclData::Class { methods, .. } = &decl.data {
            for m in methods {
                if method_is_async(m) {
                    if let Some(sel) = &m.name {
                        async_sels.insert(sel.replace(':', ""));
                    }
                }
            }
        }
    }
    let mut ctx = AsyncCtx { async_sels, task_vars: std::collections::HashSet::new() };
    let mut top_decls: Vec<AstDecl> = Vec::new();
    for decl in unit.decls.iter_mut() {
        if let AstDeclData::Class { methods, .. } = &mut decl.data {
            for m in methods.iter_mut() {
                let is_async = method_is_async(m);
                if !is_async { continue; }
                if let AstDeclData::Method { body: Some(ref mut b), return_type, params, .. } = &mut m.data {
                    // Extract param list (name, type) from CstParam chain.
                    let mut plist: Vec<(String, AstType)> = Vec::new();
                    let mut p = params.as_ref().map(|b| &**b);
                    while let Some(param) = p {
                        if let (Some(n), Some(t)) = (&param.name, &param.par_type) {
                            plist.push((n.clone(), AstType::from_cst_type(t)));
                        }
                        p = param.next.as_ref().map(|n| &**n);
                    }
                    let sym = m.name.clone().unwrap_or_default();
                    let rt = return_type.as_deref().cloned();
                    desugar_method_m2(&sym, &plist, b, &mut top_decls, rt.as_ref(), &mut ctx);
                }
            }
        }
    }
    // Frame structs and entry functions must appear BEFORE the class
    // implementations that reference them: prepend to the unit.
    let mut new_decls = top_decls;
    new_decls.extend(unit.decls.drain(..));
    unit.decls = new_decls;
    // `[task start]` appears in CALLER bodies too (e.g. main), outside async
    // methods — rewrite it unit-wide (doc/async_nptask_plan.md, stage D).
    // Likewise `@await e` in NON-async bodies (main / plain functions): the
    // blocking-join entry form lowers to ovic_task_await(e) here; async
    // method bodies were already rewritten above.
    for decl in &mut unit.decls {
        lower_task_starts_decl(decl, &mut ctx, hosted);
        lower_toplevel_awaits_decl(decl, &ctx, hosted);
    }
}

/// `@await e` in a non-async (synchronous) body → `ovic_task_await(e)` —
/// the blocking-join entry form. Must NOT touch async method bodies (their
/// awaits were already lowered with state-machine bookkeeping above; the
/// double rewrite would corrupt the Comma chains).
fn lower_toplevel_awaits_expr(e: &mut AstExpr) {
    if let AstExprData::Await(inner) = &mut e.data {
        let line = e.line;
        let col = e.col;
        let taken = std::mem::replace(&mut **inner, int_expr(0, line, col));
        // (long)ovic_task_await(e) — the runtime returns void * (the cached
        // result slot); a scalar result flows through the long cast like the
        // legacy eager driver did. Object/pointer results keep their bits.
        let await_call = AstExpr {
            kind: AstExprKind::FuncCall, expr_type: None, line, col,
            data: AstExprData::FuncCall {
                func: None, name: "ovic_task_await".to_string(), callee: None,
                args: vec![taken],
            },
        };
        *e = AstExpr {
            kind: AstExprKind::Cast, expr_type: None, line, col,
            data: AstExprData::Cast {
                target_type: AstType::new(TypePrim::Long),
                expr: Box::new(await_call),
            },
        };
        return;
    }
    match &mut e.data {
        AstExprData::MsgSend { receiver, args, .. } => {
            lower_toplevel_awaits_expr(receiver);
            for a in args.iter_mut() { lower_toplevel_awaits_expr(a); }
        }
        AstExprData::FuncCall { callee, args, .. } => {
            if let Some(c) = callee { lower_toplevel_awaits_expr(c); }
            for a in args.iter_mut() { lower_toplevel_awaits_expr(a); }
        }
        AstExprData::Unary { operand, .. } => lower_toplevel_awaits_expr(operand),
        AstExprData::Binary { left, right, .. } => {
            lower_toplevel_awaits_expr(left);
            lower_toplevel_awaits_expr(right);
        }
        AstExprData::Assign { target, value } => {
            lower_toplevel_awaits_expr(target);
            lower_toplevel_awaits_expr(value);
        }
        AstExprData::Comma(parts) => { for p in parts.iter_mut() { lower_toplevel_awaits_expr(p); } }
        AstExprData::Paren(inner) | AstExprData::Cast { expr: inner, .. } => lower_toplevel_awaits_expr(inner),
        _ => {}
    }
}

fn lower_toplevel_awaits_stmt(s: &mut AstStmt, ctx: &AsyncCtx, hosted: bool) {
    match &mut s.data {
        AstStmtData::Expr(e) => {
            lower_toplevel_awaits_expr(e);
            if hosted { drive_entry_call(e, ctx); }
        }
        AstStmtData::Return(Some(e)) => lower_toplevel_awaits_expr(e),
        AstStmtData::If { cond, then, else_ } => {
            lower_toplevel_awaits_expr(cond);
            lower_toplevel_awaits_stmt(then, ctx, hosted);
            if let Some(e) = else_ { lower_toplevel_awaits_stmt(e, ctx, hosted); }
        }
        AstStmtData::While { cond, body } | AstStmtData::Do { body, cond } => {
            lower_toplevel_awaits_expr(cond);
            lower_toplevel_awaits_stmt(body, ctx, hosted);
        }
        AstStmtData::For { init, cond, incr, body } => {
            if let Some(i) = init { lower_toplevel_awaits_stmt(i, ctx, hosted); }
            if let Some(c) = cond { lower_toplevel_awaits_expr(c); }
            if let Some(u) = incr { lower_toplevel_awaits_expr(u); }
            lower_toplevel_awaits_stmt(body, ctx, hosted);
        }
        AstStmtData::Compound(stmts) => { for st in stmts.iter_mut() { lower_toplevel_awaits_stmt(st, ctx, hosted); } }
        AstStmtData::Decl(d) => lower_toplevel_awaits_decl(d, ctx, hosted),
        _ => {}
    }
}

/// Hosted drive at the statement position (doc/async_nptask_plan.md §静态分析 3).
/// A bare call to an async method at STATEMENT position (its result discarded)
/// is the async chain's "spawn and forget" entry — lower it to
/// `ovic_task_await(call)`. `ovic_task_await` starts an unstarted task and
/// blocks until it drains (runtime.c), so `[f runAll];` runs to completion
/// without an explicit pump.
///
/// A CAPTURED call stays lazy (form A, §调用点语义): `NPTask<T> *t = [f runX];`
/// yields a first-class handle the caller may `@await` or `[t start]` (the
/// latter also drives when written as a hosted statement).
/// Freestanding (`hosted == false`) never auto-drives — bare-metal `main` owns
/// its own pump loop (doc/async_nptask_plan.md §runtime).
fn drive_entry_call(e: &mut AstExpr, ctx: &AsyncCtx) {
    if !expr_yields_task(e, ctx) { return; }
    let line = e.line;
    let col = e.col;
    let taken = std::mem::replace(e, int_expr(0, line, col));
    *e = call_expr("ovic_task_await", vec![taken], line, col);
}

fn lower_toplevel_awaits_decl(d: &mut AstDecl, ctx: &AsyncCtx, hosted: bool) {
    match &mut d.data {
        AstDeclData::Variable { init, next, .. } => {
            if let Some(i) = init { lower_toplevel_awaits_expr(i); }
            if let Some(n) = next { lower_toplevel_awaits_decl(n, ctx, hosted); }
        }
        AstDeclData::Function { body: Some(b), .. } => lower_toplevel_awaits_stmt(b, ctx, hosted),
        AstDeclData::Class { methods, .. } => {
            for m in methods.iter_mut() {
                // Skip async methods: their awaits are state-machine
                // bookkeeping now; a second rewrite would nest await calls.
                let async_m = method_is_async(m);
                if let AstDeclData::Method { body: Some(b), .. } = &mut m.data {
                    if !async_m { lower_toplevel_awaits_stmt(b, ctx, hosted); }
                }
            }
        }
        _ => {}
    }
}
