use std::path::Path;
use std::fs;
use gald_parser::parser::Parser;
use gald_binder::Binder;
use gald_elaborator::Elaborator;
use gald_codegen::{emit_unit_with_headers, emit_bridge_header};
use gald_preprocessor::Preprocessor;
use gald_symbol::SymbolTable;
use gald_ast::ast::*;
use gald_cfg::cfg_build;
use gald_arc::{arc_local_analyze, arc_global_analyze, arc_analyze_loops, arc_insert_actions, arc_optimize_pairs};
use gald_checker::Checker;
use gald_trace::{trace_refcounts, TraceOptions};
use attrs::{Backend, disposition, AttrDisposition};

/// Default exception backend.
///
/// `true` = `-eh checked`: the explicit flag + guard lowering in `crates/eh`
/// (each `@throw` arms `__gald_eh_flag` and returns; every call site that may
/// throw is guarded; the function tail propagates). ARC settles every frame on
/// the way out, so a cross-function throw releases intermediate frames'
/// owned locals — the sjlj backend's documented limitation.
///
/// `-eh legacy` (alias `-eh sjlj`) selects the old setjmp/longjmp backend and
/// remains a complete, faithful rollback.
pub const DEFAULT_EH_CHECKED: bool = true;

pub struct Pipeline {
    pub has_error: bool,
    pub error_msg: String,
    pub search_dirs: Vec<String>,
    pub no_arc: bool,
    pub no_checker: bool,
    pub no_libc: bool,
    pub nostdinc: bool,
    pub verbose: bool,
    pub trace_refcount: bool,
    pub trace_max_iters: usize,
    pub trace_color: bool,
    pub backend: Backend,
    pub werror: bool,
    pub bridge_header: Option<String>,
    pub no_comments: bool,
    /// `-eh checked` (DEFAULT since the flip): explicit flag + guard exception
    /// lowering (crates/eh), unwind-safe ARC. `-eh legacy` / `-eh sjlj`
    /// selects the old setjmp/longjmp backend for rollback.
    pub eh_checked: bool,
    /// `--slots <manifest>`: append-only vtable slot manifest for stable
    /// cross-TU layout (None = historical sorted layout).
    pub slots_manifest: Option<String>,
    /// C compiler + leading args used for the link step (e.g. `["zig", "cc"]`).
    /// Also used by the C type-name probe; empty means "unknown", which skips
    /// the probe.
    pub c_cc: Vec<String>,
    /// Target arch forwarded to the probe, mirroring the compile step's
    /// `-arch` (header search paths on Apple SDKs depend on it).
    pub c_arch: Option<String>,
    /// `-fno-ctype-probe`: never invoke the C preprocessor to recover the C
    /// type-name table. The parser then falls back to its builtin list and
    /// shape heuristics — casts to C-header typedefs outside that list are
    /// rejected again, and `x * y;` on two variables is misread as a
    /// declaration. Useful when no C compiler is available at transpile time.
    pub no_ctype_probe: bool,
}

impl Pipeline {
    pub fn new() -> Self {
        Pipeline {
            has_error: false,
            error_msg: String::new(),
            search_dirs: vec!["include".into(), ".".into(), "include/Foundation".into()],
            no_arc: false,
            no_checker: false,
            no_libc: false,
            nostdinc: false,
            verbose: false,
            trace_refcount: false,
            trace_max_iters: 2,
            trace_color: true,
            backend: Backend::Clang,
            werror: false,
            bridge_header: None,
            no_comments: false,
            eh_checked: DEFAULT_EH_CHECKED,
            slots_manifest: None,
            c_cc: Vec::new(),
            c_arch: None,
            no_ctype_probe: false,
        }
    }

    /// The C type names this TU's `#include`d headers declare, plus whether the
    /// table is authoritative (produced by the real C preprocessor over the
    /// same headers, macros, search paths and freestanding flags the compile
    /// step uses). `(vec![], false)` means "unknown": the parser then keeps its
    /// builtin list and shape fallbacks, so this can never reject code it
    /// cannot prove wrong.
    fn c_type_names(&self, pre: &Preprocessor, macros: &[&str], filename: &str) -> (Vec<String>, bool) {
        if self.no_ctype_probe || self.c_cc.is_empty() || pre.c_headers.is_empty() {
            return (Vec::new(), false);
        }
        // The source file's own directory comes first: `#include "x.h"` next to
        // the `.gm` is the natural spelling, and the C compiler resolves it
        // relative to the including file (which for the emitted C is the
        // invocation directory — `search_dirs` already carries `.`).
        let mut dirs: Vec<String> = Vec::new();
        if let Some(parent) = Path::new(filename).parent() {
            let p = parent.to_string_lossy().to_string();
            if !p.is_empty() {
                dirs.push(p);
            }
        }
        dirs.extend(self.search_dirs.iter().cloned());
        match crate::ctype_probe::probe_c_type_names(
            &self.c_cc,
            &pre.c_headers,
            &dirs,
            macros,
            self.c_arch.as_deref(),
            self.no_libc,
            self.nostdinc,
        ) {
            Some(names) => {
                if self.verbose {
                    eprintln!(
                        "[galdc] C type table: {} names from {} passthrough header(s)",
                        names.len(),
                        pre.c_headers.len()
                    );
                }
                (names, true)
            }
            None => {
                if self.verbose {
                    eprintln!("[galdc] C type probe unavailable — using the builtin type list");
                }
                (Vec::new(), false)
            }
        }
    }

    pub fn transpile_file(&mut self, input_path: &str, output_path: &str) -> Result<(), String> {
        let source = fs::read_to_string(input_path)
            .map_err(|e| format!("cannot read {}: {}", input_path, e))?;

        let c_code = self.transpile(&source, input_path)?;

        if let Some(parent) = Path::new(output_path).parent() {
            fs::create_dir_all(parent).map_err(|e| format!("cannot create output dir: {}", e))?;
        }
        fs::write(output_path, &c_code)
            .map_err(|e| format!("cannot write {}: {}", output_path, e))?;

        Ok(())
    }

    pub fn transpile(&mut self, source: &str, filename: &str) -> Result<String, String> {
        // `__GALD__` is always defined: gald headers can guard objc-style
        // syntax behind `#ifdef __GALD__` so a plain C compiler sees only the
        // C-compatible subset when the header is used directly (without galdc).
        let extra_macros: &[&str] = match self.backend {
            attrs::Backend::Clang => &["__clang__", "__GNUC__", "__GALD__"],
            attrs::Backend::Gcc => &["__GNUC__", "__GALD__"],
            attrs::Backend::Portable => &["__GNUC__", "__GALD__"],
        };
        let pre = Preprocessor::process(source, filename, &self.search_dirs, extra_macros)?;

        // `#include`d C headers are passed through verbatim, so the parser never
        // sees the typedefs they declare. Recover the table the C compiler will
        // actually use (see `ctype_probe`) so the parser can resolve casts and
        // `x * y;` by the symbol table, exactly as C does, instead of guessing
        // from token shape. Failure is not an error: the table is then simply
        // not authoritative, and the parser keeps its historical fallbacks.
        let (c_type_names, c_types_complete) = self.c_type_names(&pre, extra_macros, filename);

        // Step 1: Parse the resolved gald source
        if self.verbose { eprintln!("[galdc] parsing..."); }
        let mut parser = Parser::with_c_type_names(&pre.resolved_gald, &c_type_names, c_types_complete);
        // The parser reads one inlined buffer, so it has no `#include` boundary
        // of its own. The line→file map is the only way it can scope an
        // `NF_ASSUME_NONNULL` region to the file that opened it — without this
        // an open region marks every pointer in every imported header nonnull.
        parser.set_source_map(pre.source_map.clone());
        let mut cst = parser.parse_translation_unit()
            .ok_or_else(|| format!("Parse failed:\n{}", prefix_lines("[parser]",
                &translate_lines(parser.last_error(), &pre.source_map))))?;
        cst.filename = filename.to_string();

        if parser.has_error() {
            return Err(format!("Parse failed:\n{}", prefix_lines("[parser]",
                &translate_lines(parser.last_error(), &pre.source_map))));
        }

        // Step 2: Bind names
        if self.verbose { eprintln!("[galdc] binding names..."); }
        let symtab = SymbolTable::new();
        let mut binder = Binder::new(symtab);
        if binder.bind(&mut cst) != 0 {
            return Err(format!("Binding failed:\n{}", prefix_lines("[binder]",
                &translate_lines(binder.last_error(), &pre.source_map))));
        }

        // Step 3: Elaborate CST → AST
        if self.verbose { eprintln!("[galdc] elaborating..."); }
        let symtab_for_checker = binder.symtab.clone();
        let mut elaborator = Elaborator::new(Some(binder.symtab));
        elaborator.verbose = self.verbose;
        if elaborator.run(&cst) != 0 {
            return Err(format!("Elaboration failed:\n{}", prefix_lines("[elaborator]", elaborator.last_error())));
        }
        let mut ast = elaborator.take_ast()
            .ok_or_else(|| "Elaboration produced no AST".to_string())?;

        // Step 3.9: Checked-exception desugar (-eh checked) — MUST run before
        // ARC so the injected early `return`s are ordinary control flow that
        // ARC already releases for (running it after would reintroduce the
        // sjlj cross-function leak this backend exists to fix).
        if self.eh_checked {
            if self.verbose { eprintln!("[galdc] eh desugar (checked)..."); }
            let eh_diags = gald_eh::check_unit(&ast);
            if !eh_diags.errors.is_empty() {
                self.has_error = true;
                self.error_msg = format!("EH check failed:\n{}", prefix_lines("[eh]", &translate_lines(&eh_diags.errors.join("\n"), &pre.source_map)));
                return Err(self.error_msg.clone());
            }
            gald_eh::desugar_unit(&mut ast);
        }

        // Step 3.9: @defer splicing — AFTER eh desugar (checked-mode throws
        // are already plain returns) and BEFORE ARC, so ARC's scope-end
        // releases land after the user's defer statements (deferred code runs
        // while objects are still alive). See AGENTS.md `@defer` section.
        {
            if self.verbose { eprintln!("[galdc] defer desugar..."); }
            let defer_diags = gald_defer::desugar_unit(&mut ast);
            if !defer_diags.errors.is_empty() {
                self.has_error = true;
                self.error_msg = format!("Defer check failed:\n{}", prefix_lines("[defer]", &translate_lines(&defer_diags.errors.join("\n"), &pre.source_map)));
                return Err(self.error_msg.clone());
            }
        }

        // Step 3.92: legacy (sjlj) `@finally` early-return splice.
        //
        // The checked backend rewrites `@try` before this point and splices the
        // finally body in front of every `return` while doing so. The sjlj
        // backend hands `@try` to codegen unchanged, so without this pass a
        // `return` inside a `@try` silently skipped its `@finally` (ObjC runs
        // it). Runs before ARC so the injected cleanup stays after the finally
        // copy — the finally sees its locals alive.
        if !self.eh_checked {
            if self.verbose { eprintln!("[galdc] legacy @finally splice..."); }
            gald_eh::splice_finally_exits(&mut ast);
        }

        // Step 3.95: Pattern-switch lowering — AFTER defer splicing (defer
        // sees the original switch; lowered goto/labels are ordinary stmts it
        // must never splice into) and BEFORE ARC (ARC/checker/codegen only
        // ever see plain C statements: If/Decl/Goto/Label — zero new arms
        // downstream). See AGENTS.md pattern-switch section.
        {
            if self.verbose { eprintln!("[galdc] pattern-switch lowering..."); }
            gald_pattern::desugar_unit(&mut ast);
        }

        // Step 4: ARC analysis (skipped when -fno-gald-arc is set)
        if self.verbose { eprintln!("[galdc] ARC analysis..."); }
        if !self.no_arc {
            for decl in &mut ast.decls {
                match &mut decl.data {
                    AstDeclData::Method { body: Some(ref mut b), .. } => {
                        if let Some(ref msym) = decl.name {
                            let cfg = cfg_build(b);
                            let mut arc_result = arc_local_analyze(b, &cfg, msym);
                            arc_global_analyze(&cfg, &mut arc_result, msym);
                            arc_analyze_loops(&cfg, &mut arc_result, msym);
                            arc_insert_actions(b, &arc_result);
                            arc_optimize_pairs(b);
                            if !arc_result.errors.is_empty() {
                                return Err(format!("ARC analysis failed:\n{}",
                                    prefix_lines("[arc]", &arc_result.errors.join("\n"))));
                            }
                            for w in &arc_result.leak_warnings {
                                eprintln!("\x1b[1;35m[arc] warning:\x1b[0m {}:{}:{}: {}", filename, decl.line, decl.col, w);
                            }
                        }
                    }
                    AstDeclData::Function { body: Some(ref mut b), .. } => {
                        let name = decl.name.as_deref().unwrap_or("function");
                        let cfg = cfg_build(b);
                        let mut arc_result = arc_local_analyze(b, &cfg, name);
                        arc_global_analyze(&cfg, &mut arc_result, name);
                        arc_analyze_loops(&cfg, &mut arc_result, name);
                        arc_insert_actions(b, &arc_result);
                        arc_optimize_pairs(b);
                        if !arc_result.errors.is_empty() {
                            return Err(format!("ARC analysis failed:\n{}",
                                prefix_lines("[arc]", &arc_result.errors.join("\n"))));
                        }
                        for w in &arc_result.leak_warnings {
                            eprintln!("\x1b[1;35m[arc] warning:\x1b[0m {}:{}:{}: {}", filename, decl.line, decl.col, w);
                        }
                    }
                    AstDeclData::Class { methods, .. } => {
                        for m in methods {
                            if let AstDeclData::Method { body: Some(ref mut b), .. } = &mut m.data {
                                let name = m.name.as_deref().unwrap_or("method");
                                let cfg = cfg_build(b);
                                let mut arc_result = arc_local_analyze(b, &cfg, name);
                                arc_global_analyze(&cfg, &mut arc_result, name);
                                arc_analyze_loops(&cfg, &mut arc_result, name);
                                arc_insert_actions(b, &arc_result);
                                arc_optimize_pairs(b);
                                if !arc_result.errors.is_empty() {
                                    return Err(format!("ARC analysis failed:\n{}",
                                        prefix_lines("[arc]", &arc_result.errors.join("\n"))));
                                }
                                for w in &arc_result.leak_warnings {
                                    eprintln!("\x1b[1;35m[arc] warning:\x1b[0m {}:{}:{}: {}", filename, m.line, m.col, w);
                                }
                            }
                        }
                    }
                    _ => {}
                }
            }
        }

        // Step 4.5: Reference-count trace (skips codegen entirely)
        if self.trace_refcount {
            if self.verbose { eprintln!("[galdc] tracing refcounts..."); }
            let opts = TraceOptions {
                max_iters: self.trace_max_iters,
                color: self.trace_color,
            };
            return Ok(trace_refcounts(&ast, &opts));
        }

        // Step 4.8: Async pre-pass — validate await placement, then desugar
        // every async method body into the task-driven form (route map item
        // #4). check_unit runs on the ORIGINAL AST; desugar_unit rewrites it
        // in place before ARC/checker see the method bodies.
        if self.verbose { eprintln!("[galdc] async analysis..."); }
        let async_diags = gald_async::check_unit(&ast);
        if !async_diags.errors.is_empty() {
            self.has_error = true;
            self.error_msg = format!("Async check failed:\n{}", prefix_lines("[async]", &translate_lines(&async_diags.errors.join("\n"), &pre.source_map)));
            return Err(self.error_msg.clone());
        }
        // Recoverable async warnings (e.g. `@await` without an `NFAsync<T>`
        // marker) — printed purple like ARC warnings; `-Werror` escalates.
        // Lines are inline-buffer positions: translate via SourceMap.
        if !async_diags.warnings.is_empty() {
            if self.werror {
                self.has_error = true;
                self.error_msg = format!("Async check failed (-Werror):\n{}", prefix_lines("[async]", &translate_lines(&async_diags.warnings.join("\n"), &pre.source_map)));
                return Err(self.error_msg.clone());
            }
            for w in &async_diags.warnings {
                // translate_lines already yields `file:line:col: msg`.
                eprintln!("\x1b[1;35m[async] warning:\x1b[0m {}", translate_lines(w, &pre.source_map));
            }
        }
        gald_async::desugar_unit_m2(&mut ast);

        // Step 5: Check types (skipped when -fno-checker is set)
        if self.verbose { eprintln!("[galdc] checking types..."); }
        let mut struct_eq_tags: Vec<String> = Vec::new();
        if !self.no_checker {
            let mut checker = Checker::new(Some(symtab_for_checker));
            checker.no_arc = self.no_arc;
            checker.source_map = Some(pre.source_map.clone());
            // `-eh checked` rewrites `@throw` before the checker runs, so the
            // bare `@throws` "must really throw" rule cannot see the
            // statements it is meant to reconcile.
            checker.eh_checked = self.eh_checked;
            if checker.check(&mut ast) != 0 {
                return Err(format!("Type checking failed:\n{}", prefix_lines("[checker]", checker.last_error())));
            }
            // Print warnings (non-fatal diagnostics, like C/ObjC `-W...`).
            // With `-Werror`, suppress the purple warning and promote to a red error.
            if self.werror && !checker.warnings().is_empty() {
                return Err(format!("Type checking failed (-Werror):\n{}",
                    prefix_lines("[checker]", &checker.warnings().join("\n"))));
            }
            for w in checker.warnings() {
                eprintln!("\x1b[1;35m[checker] warning:\x1b[0m {}", w);
            }
            struct_eq_tags = checker.struct_eq_tags.clone();
        }

        // Step 5.5: Validate __attribute__ against backend
        if self.verbose { eprintln!("[galdc] validating attributes..."); }
        self.validate_attrs(&ast);
        if self.has_error {
            return Err(self.error_msg.clone());
        }

        // Step 6: Generate C code
        if self.verbose { eprintln!("[galdc] generating C code..."); }
        // Slots manifest: read the assigned method order (if the file exists)
        // BEFORE codegen, and write the post-compile assignment back after.
        let slots: Option<Vec<String>> = self.slots_manifest.as_ref().and_then(|path| {
            fs::read_to_string(path).ok().map(|content| {
                content.lines().map(|l| l.trim().to_string()).filter(|l| !l.is_empty()).collect()
            })
        });
        let mut cg = gald_codegen::ast_to_cg_unit_with_slots(&ast, self.backend, slots.as_deref());
        // Struct tags whose `==`/`!=` the checker rewrote to value-comparison
        // calls; codegen emits one field-wise `gald_struct_eq_<tag>` per tag.
        cg.struct_eq_tags = struct_eq_tags;
        cg.no_arc = self.no_arc;
        let c_code = emit_unit_with_headers(&cg, &pre.c_headers, &self.search_dirs, self.no_libc, self.backend, !self.no_comments, self.eh_checked);

        // Step 6.4: Write back the slots manifest (append-only): the compiled
        // assignment (previously-assigned slots kept + new methods appended)
        // becomes the manifest, so a later TU compiled with the same manifest
        // file shares this exact method→slot layout.
        if let Some(ref path) = self.slots_manifest {
            let manifest = cg.global_instance_method_names.join("\n");
            if let Err(e) = fs::write(path, manifest + "\n") {
                self.has_error = true;
                return Err(format!("cannot write slots manifest {}: {}", path, e));
            }
        }

        // Step 6.5: Generate bridge header (if requested)
        if let Some(ref path) = self.bridge_header {
            if self.verbose { eprintln!("[galdc] writing bridge header: {}", path); }
            let bridge = emit_bridge_header(&cg);
            fs::write(path, &bridge)
                .map_err(|e| format!("cannot write bridge header {}: {}", path, e))?;
        }

    Ok(c_code)
    }

    fn validate_attrs(&mut self, ast: &AstUnit) {
        for decl in &ast.decls {
            self.validate_decl_attrs(decl);
        }
    }

    fn validate_decl_attrs(&mut self, decl: &AstDecl) {
        for attr in &decl.attributes {
            let name = attr.split('(').next().unwrap_or(attr).trim();
            match disposition(name, self.backend) {
                AttrDisposition::Error => {
                    self.has_error = true;
                    self.error_msg = format!(
                        "{}:{}: error: attribute '{}' not allowed in --backend={} (try --backend=clang or --backend=gcc)",
                        decl.line, decl.col, name, self.backend
                    );
                    eprintln!("{}", self.error_msg);
                }
                AttrDisposition::Warn => {
                    eprintln!("\x1b[1;35m{}:{}: warning:\x1b[0m unknown attribute '{}' (passing through to C compiler)",
                        decl.line, decl.col, name);
                }
                AttrDisposition::Pass => {}
            }
        }
        // Recurse into nested decls (class methods, ivars, struct fields, etc.)
        match &decl.data {
            AstDeclData::Class { methods, ivars, properties, impl_vars, .. } => {
                for m in methods { self.validate_decl_attrs(m); }
                for iv in ivars { self.validate_decl_attrs(iv); }
                for p in properties { self.validate_decl_attrs(p); }
                for v in impl_vars { self.validate_decl_attrs(v); }
            }
            AstDeclData::Aggregate { fields, .. } => {
                for f in fields { self.validate_decl_attrs(f); }
            }
            AstDeclData::Namespace(decls) => {
                for d in decls { self.validate_decl_attrs(d); }
            }
            _ => {}
        }
    }
}


/// Prefix every non-empty line of a multi-error message with `[stage]` so a
/// single compile can be read as a categorized report (parser/binder/checker…).
fn prefix_lines(stage: &str, msg: &str) -> String {
    msg.lines()
        .map(|l| if l.trim().is_empty() {
            l.to_string()
        } else {
            format!("{} {}", stage, l)
        })
        .collect::<Vec<_>>()
        .join("\n")
}

/// Rewrites `LINE:COL: message` occurrences in a diagnostic string so the
/// line points at the original source file (via the preprocessor's line map)
/// instead of the flattened inlined buffer. Lines that can't be mapped are
/// left untouched.
fn translate_lines(msg: &str, sm: &gald_cst::source_map::SourceMap) -> String {
    if sm.is_empty() { return msg.to_string(); }
    msg.lines().map(|l| {
        // Parse leading `LINE:COL: ` (or `LINE: `)
        let mut parts = l.splitn(3, ':');
        let line_no: Option<usize> = parts.next().and_then(|p| p.trim().parse().ok());
        let rest: String = match (parts.next(), parts.next()) {
            (Some(col), Some(text)) => {
                let col_trim = col.trim();
                if col_trim.chars().all(|c| c.is_ascii_digit()) && col_trim.starts_with(|c: char| c.is_ascii_digit()) {
                    format!("{}: {}", col_trim, text)
                } else {
                    format!("{}: {}", col, text)
                }
            }
            (Some(col), None) => col.to_string(),
            _ => return l.to_string(),
        };
        match line_no {
            Some(n) if n > 0 => {
                let (file, real) = sm.locate(n);
                if file.is_empty() {
                    l.to_string()
                } else {
                    format!("{}:{}: {}", file, real, rest.trim_start())
                }
            }
            _ => l.to_string(),
        }
    }).collect::<Vec<_>>().join("\n")
}

#[cfg(test)]
mod eh_default_tests {
    use super::*;

    /// Regression guard for the default exception backend.
    ///
    /// The flip to `-eh checked` was vetted by the acceptance matrix
    /// (`tests/eh_matrix/run_eh_matrix.sh`: 17 scenarios x 3 modes),
    /// the clang/ObjC differential suite (`tests/eh_diff/`: 7/7) and
    /// `test_all`. Reverting this constant — or letting `Pipeline::new()`
    /// drift away from it — silently changes every build's exception
    /// semantics, so both are asserted here.
    #[test]
    fn default_exception_backend_is_checked() {
        assert!(
            DEFAULT_EH_CHECKED,
            "default EH backend regressed to sjlj; '-eh checked' is the vetted default"
        );
        assert!(
            Pipeline::new().eh_checked,
            "Pipeline::new() must take its eh_checked value from DEFAULT_EH_CHECKED"
        );
    }

    /// `-eh legacy` must still be reachable as a full rollback: the flag is a
    /// plain bool the CLI sets to false, so a false `Pipeline::new()` default
    /// would have made `legacy` a no-op rather than a distinct backend.
    #[test]
    fn legacy_rollback_is_an_explicit_false() {
        let mut p = Pipeline::new();
        p.eh_checked = false;
        assert!(!p.eh_checked, "-eh legacy must produce the sjlj backend");
    }
}


