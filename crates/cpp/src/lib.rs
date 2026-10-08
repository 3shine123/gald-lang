//! Nepa-side macro expansion.
//!
//! Nepa is a C superset: plain C `#define`s are passed through verbatim to the
//! C compiler, which expands them as usual. But a macro whose body contains
//! nepa-specific syntax (`[receiver msg]`, `@keyword`, `^{...}`) cannot be
//! expanded by any C compiler — the body is not C. For those macros nepac
//! expands them itself, at the source level, before lexing.
//!
//! The expansion algorithm follows the C standard's specification of macro
//! replacement (ISO/IEC 9899:2011 §6.10.3), implemented independently against
//! nepa's own token model:
//!
//! - object-like macros: name → replacement list, then rescan (§6.10.3.1)
//! - function-like macros: arguments collected on balanced parens, each
//!   argument fully macro-expanded before substitution (§6.10.3.1),
//!   except an argument that is the operand of `#` or `##` (§6.10.3.2/3)
//! - `#param` stringifies the *unexpanded* argument (§6.10.3.2)
//! - `a ## b` pastes the surrounding tokens; operands of `##` are not
//!   expanded before pasting (§6.10.3.3)
//! - `__VA_ARGS__` receives the trailing variadic arguments joined with
//!   commas (§6.10.3.4)
//! - a macro name is not re-expanded inside its own expansion — the "hide
//!   set" freeze that terminates recursion (§6.10.3.1p2 / the standard's
//!   blue-paint rule). We approximate with a per-expansion active set, which
//!   agrees with the standard for all non-pathological nesting.
//!
//! Deliberate limitations (reported as clear errors, never silent):
//! - a macro invocation must be complete on one logical line
//! - no `#if`-context macro evaluation (conditionals only test definedness)
//! - `_Pragma` inside a macro *body*: §6.10.9 defines the pragma at the
//!   *invocation* site, relative to the tokens around the call — a position a
//!   source-level expander has no way to name — so expanding such a macro is
//!   an error instead of a silently misplaced pragma. A `_Pragma(...)` written
//!   directly in the source never reaches this crate: it travels as a verbatim
//!   pass-through line and lands in the generated C where it was written.

use std::collections::{HashMap, HashSet};

/// A parsed `#define`.
#[derive(Debug, Clone)]
pub struct MacroDef {
    pub name: String,
    /// `None` = object-like; `Some(params)` = function-like.
    pub params: Option<Vec<String>>,
    /// Function-like declared with `, ...` — trailing args land in `__VA_ARGS__`.
    pub variadic: bool,
    /// Replacement list (single logical line: continuations pre-joined).
    pub body: String,
}

/// True when the macro body contains nepa-specific syntax that a C compiler
/// could not expand: an `@` keyword, a message send, or a block literal.
///
/// Heuristic (conservative in the harmless direction): an ObjC message send
/// has `[` in token-initial position (preceded by whitespace, start of line,
/// or an opening punctuation), whereas C indexing glues `[` directly onto an
/// identifier/`)`/`]`. If a pure-C macro is misclassified anyway, expansion is
/// still harmless — the body has no nepa syntax, so splicing it back yields
/// the same text; the only loss is that the definition no longer reaches the
/// C prelude.
pub fn body_has_nepa_syntax(body: &str) -> bool {
    let bytes = body.as_bytes();
    let mut i = 0;
    let mut prev_significant: Option<u8> = None; // last non-space char
    while i < bytes.len() {
        match bytes[i] {
            b'@' if i + 1 < bytes.len() && !matches!(bytes[i + 1], b' ' | b'\t' | b'(' | b')' | b',' | b';') => {
                // @keyword, @"boxed literal", @42 — anything ObjC-ish. A lone
                // @ followed by punctuation/whitespace is not nepa syntax.
                return true;
            }
            b'[' => {
                let token_initial = match prev_significant {
                    None => true,
                    Some(c) => !(c.is_ascii_alphanumeric() || c == b'_' || c == b')' || c == b']'),
                };
                if token_initial { return true; }
                prev_significant = Some(b'[');
            }
            b'^' if i + 1 < bytes.len() && bytes[i + 1] == b'{' => return true,
            b'"' => { i = skip_string(bytes, i, b'"'); prev_significant = Some(b'"'); continue; }
            b'\'' => { i = skip_string(bytes, i, b'\''); prev_significant = Some(b'\''); continue; }
            b'/' if i + 1 < bytes.len() && bytes[i + 1] == b'/' => return false, // rest is a comment
            b'/' if i + 1 < bytes.len() && bytes[i + 1] == b'*' => {
                i += 2;
                while i + 1 < bytes.len() && !(bytes[i] == b'*' && bytes[i + 1] == b'/') { i += 1; }
                i = (i + 2).min(bytes.len());
                continue;
            }
            c if c == b' ' || c == b'\t' => {}
            c => prev_significant = Some(c),
        }
        i += 1;
    }
    false
}

/// True when a macro *body* contains the `_Pragma` operator (§6.10.9) as a
/// whole token — not as a substring of a longer identifier, and not inside a
/// string/char literal or a comment.
///
/// `_Pragma` defines its pragma at the invocation site, relative to whatever
/// tokens surround the macro call, so the expansion cannot be placed by a
/// source-level expander. Detecting it here turns a silent misplacement into a
/// clear error (see `expand_line`).
fn body_has_pragma(body: &str) -> bool {
    let bytes = body.as_bytes();
    let mut i = 0;
    while i < bytes.len() {
        match bytes[i] {
            b'"' | b'\'' => {
                i = skip_string(bytes, i, bytes[i]).min(bytes.len());
            }
            b'/' if i + 1 < bytes.len() && bytes[i + 1] == b'/' => return false, // rest is a comment
            b'/' if i + 1 < bytes.len() && bytes[i + 1] == b'*' => {
                i += 2;
                while i + 1 < bytes.len() && !(bytes[i] == b'*' && bytes[i + 1] == b'/') { i += 1; }
                i = (i + 2).min(bytes.len());
            }
            c if is_ident_byte(c, true) => {
                let start = i;
                while i < bytes.len() && is_ident_byte(bytes[i], false) { i += 1; }
                if &body[start..i] == "_Pragma" { return true; }
            }
            _ => i += 1,
        }
    }
    false
}

/// Skip a string/char literal starting at `i` (the quote), returning the index
/// just past the closing quote (or end of input if unterminated).
fn skip_string(bytes: &[u8], mut i: usize, quote: u8) -> usize {
    i += 1;
    while i < bytes.len() {
        if bytes[i] == b'\\' { i += 2; continue; }
        if bytes[i] == quote { return i + 1; }
        i += 1;
    }
    i
}

/// Remove `//` and `/* */` comments from a define line, honoring string and
/// char literals. C's translation phase 3 replaces comments with a space
/// before directives are parsed — a trailing `// note` in a `#define` must
/// not become part of the replacement list (it would swallow the rest of
/// every expanded line).
fn strip_comments(text: &str) -> String {
    let bytes = text.as_bytes();
    let mut out = String::with_capacity(text.len());
    let mut i = 0;
    while i < bytes.len() {
        let c = bytes[i];
        if c == b'"' || c == b'\'' {
            let end = skip_string(bytes, i, c);
            let end = end.min(bytes.len());
            out.push_str(&text[i..end]);
            i = end;
            continue;
        }
        if c == b'/' && i + 1 < bytes.len() && bytes[i + 1] == b'/' {
            break; // rest of the (logical) line is a comment
        }
        if c == b'/' && i + 1 < bytes.len() && bytes[i + 1] == b'*' {
            i += 2;
            while i + 1 < bytes.len() && !(bytes[i] == b'*' && bytes[i + 1] == b'/') { i += 1; }
            i = (i + 2).min(bytes.len());
            out.push(' '); // phase 3: one space in place of the comment
            continue;
        }
        out.push(c as char);
        i += 1;
    }
    out
}

/// Parse a `#define` line (continuations already joined into `rest`).
/// Returns None if the line is not a well-formed define.
pub fn parse_define(rest: &str) -> Option<MacroDef> {
    let rest = strip_comments(rest).trim().to_string();
    let bytes = rest.as_bytes();
    // Macro name: identifier immediately after #define.
    let mut i = 0;
    while i < bytes.len() && (bytes[i] as char).is_whitespace() { i += 1; }
    let name_start = i;
    while i < bytes.len() && ((bytes[i] as char).is_alphanumeric() || bytes[i] == b'_') {
        i += 1;
    }
    if i == name_start { return None; }
    let name = rest[name_start..i].to_string();

    // Function-like iff '(' immediately follows the name (no space) — C11 6.10.3p3.
    if i < bytes.len() && bytes[i] == b'(' {
        let close = find_matching_paren(bytes, i)?;
        let param_txt = &rest[i + 1..close];
        let body = rest[close + 1..].trim().to_string();
        let mut params = Vec::new();
        let mut variadic = false;
        for p in param_txt.split(',') {
            let p = p.trim();
            if p == "..." { variadic = true; continue; }
            if p.is_empty() && param_txt.trim().is_empty() { continue; }
            if !p.bytes().all(|c| (c as char).is_alphanumeric() || c == b'_') {
                return None; // not a plain identifier list — leave to C
            }
            params.push(p.to_string());
        }
        return Some(MacroDef { name, params: Some(params), variadic, body });
    }

    let body = rest[i..].trim().to_string();
    Some(MacroDef { name, params: None, variadic: false, body })
}

fn find_matching_paren(bytes: &[u8], open: usize) -> Option<usize> {
    let mut depth = 0usize;
    let mut i = open;
    while i < bytes.len() {
        match bytes[i] {
            b'(' => depth += 1,
            b')' => { depth -= 1; if depth == 0 { return Some(i); } }
            b'"' => { i = skip_string(bytes, i, b'"'); continue; }
            b'\'' => { i = skip_string(bytes, i, b'\''); continue; }
            _ => {}
        }
        i += 1;
    }
    None
}

/// Expand every nepa macro invocation in `src`.
/// Errors describe the problem with a 1-based line number in `src`.
pub fn expand(src: &str, table: &HashMap<String, MacroDef>) -> Result<String, String> {
    Ok(expand_mapped(src, table)?.0)
}

/// Expand every nepa macro invocation in `src`, letting a function-like call
/// span source lines, and report where each output line came from: `lines[i]`
/// is the 1-based source line that output line `i` is attributed to.
///
/// A call split across lines is expanded as a whole, and every line it produces
/// is attributed to the line the call *starts* on — its invocation site. That
/// keeps a diagnostic inside the expansion pointing at the line the author
/// actually wrote, and keeps the caller's line map in step: the map gets one
/// entry per **output** line, not per input line, so source lines collapsed by
/// the expansion (an argument spread over three lines becomes one) cannot shift
/// every following position.
pub fn expand_mapped(src: &str, table: &HashMap<String, MacroDef>)
    -> Result<(String, Vec<u32>), String>
{
    let src_lines: Vec<&str> = src.lines().collect();
    if table.is_empty() {
        return Ok((src.to_string(), (1..=src_lines.len() as u32).collect()));
    }
    let mut out = String::with_capacity(src.len());
    let mut map: Vec<u32> = Vec::new();
    let mut i = 0usize;
    while i < src_lines.len() {
        let start = i + 1; // 1-based line the invocation starts on
        let mut buf = src_lines[i].to_string();
        // Streaming scan: an invocation still open at the end of the line pulls
        // in the next line and rescans from the start of the call, until it
        // closes or the input runs out.
        let expanded = loop {
            match expand_line(&buf, table, &mut HashSet::new()) {
                Ok(Scan::Text(s)) => break s,
                Ok(Scan::Unclosed) => {
                    i += 1;
                    if i >= src_lines.len() {
                        return Err(format!("line {}: macro invocation is not closed before the \
                            end of the input — the call must be closed with ')'", start));
                    }
                    buf.push('\n');
                    buf.push_str(src_lines[i]);
                }
                Err(e) => return Err(format!("line {}: {}", start, e)),
            }
        };
        for l in expanded.lines() {
            out.push_str(l);
            out.push('\n');
            map.push(start as u32);
        }
        if expanded.is_empty() {
            // An invocation that expands to nothing still consumes its source
            // line: emit the (empty) line so `map` stays aligned with `out`.
            out.push('\n');
            map.push(start as u32);
        }
        i += 1;
    }
    // Preserve a missing trailing newline exactly as the input had it (the map
    // is popped with it, so it keeps exactly one entry per emitted line).
    if !src.ends_with('\n') && out.ends_with('\n') {
        out.pop();
        map.pop();
    }
    Ok((out, map))
}

/// ASCII identifier bytes only: macro names are C identifiers, so any
/// non-ASCII byte (UTF-8 continuation/lead bytes of comments or string text)
/// must be passed through verbatim — never treated as identifier material.
/// (Slicing on byte indices is safe again because scanning stops at ASCII
/// boundaries.)
fn is_ident_byte(c: u8, first: bool) -> bool {
    c.is_ascii_alphabetic() || c == b'_' || (!first && c.is_ascii_digit())
}

/// Outcome of scanning a chunk of source for macro invocations.
enum Scan {
    /// The chunk was expanded in full.
    Text(String),
    /// A function-like invocation was still open when the chunk ended, so the
    /// scan was inconclusive: more input is needed and the chunk must be
    /// rescanned as a whole — see `expand_mapped`, the only caller that can act
    /// on this. Everything else propagates it (a call the input never closes
    /// ends as an error at the top).
    Unclosed,
}

/// Scan one line, expanding macros. `active` is the hide set: macro names in
/// it are left unexpanded (self-recursion freeze).
fn expand_line(line: &str, table: &HashMap<String, MacroDef>, active: &mut HashSet<String>)
    -> Result<Scan, String>
{
    let bytes = line.as_bytes();
    let mut out = String::with_capacity(line.len());
    let mut i = 0;
    while i < bytes.len() {
        let c = bytes[i];
        if c == b'"' {
            let end = skip_string(bytes, i, b'"');
            out.push_str(&line[i..end.min(bytes.len())]);
            i = end.min(bytes.len());
            continue;
        }
        if c == b'\'' {
            let end = skip_string(bytes, i, b'\'');
            out.push_str(&line[i..end.min(bytes.len())]);
            i = end.min(bytes.len());
            continue;
        }
        if c == b'/' && i + 1 < bytes.len() && bytes[i + 1] == b'/' {
            out.push_str(&line[i..]);
            break;
        }
        if is_ident_byte(c, true) {
            let start = i;
            while i < bytes.len() && is_ident_byte(bytes[i], false) { i += 1; }
            let name = &line[start..i];
            match table.get(name) {
                None => out.push_str(name),
                Some(_) if active.contains(name) => out.push_str(name), // frozen
                Some(def) => {
                    if body_has_pragma(&def.body) {
                        return Err(format!(
                            "macro '{}' has '_Pragma' in its body, which nepa cannot expand — \
                             '_Pragma' (ISO/IEC 9899:2011 §6.10.9) takes effect at the invocation \
                             site during preprocessing, and a source-level expander has no position \
                             to place it; write the _Pragma(...) directly on its own source line instead",
                            name));
                    }
                    match def.params {
                        None => {
                            // Object-like: expand body, then rescan it (the body
                            // may itself reference other macros).
                            active.insert(name.to_string());
                            let body = expand_line(&def.body, table, active)?;
                            active.remove(name);
                            match body {
                                Scan::Text(s) => out.push_str(&s),
                                Scan::Unclosed => return Ok(Scan::Unclosed),
                            }
                        }
                        Some(_) => {
                            // Function-like: only a call (name followed by '('
                            // after optional whitespace) expands; a bare name
                            // (e.g. a function pointer reference) stays.
                            let mut j = i;
                            while j < bytes.len() && (bytes[j] == b' ' || bytes[j] == b'\t') { j += 1; }
                            if j >= bytes.len() || bytes[j] != b'(' {
                                out.push_str(name);
                                continue;
                            }
                            let (args, has_close, end) = collect_args(bytes, j)?;
                            if !has_close {
                                // The chunk ended inside the argument list. The
                                // caller (expand_mapped) appends the next source
                                // line and rescans, so a call may span lines;
                                // if the input ends first, that becomes a clear
                                // "not closed before the end of the input".
                                return Ok(Scan::Unclosed);
                            }
                            i = end;
                            // §6.10.3.1: arguments are expanded before
                            // substitution — and before `name` joins the hide
                            // set, so `F(F(1))` expands its inner call; only the
                            // rescan of the substituted body freezes `name`.
                            let mut expanded_args: Vec<String> =
                                Vec::with_capacity(args.len());
                            for a in &args {
                                match expand_line(a, table, active)? {
                                    Scan::Text(s) => expanded_args.push(s),
                                    Scan::Unclosed => return Ok(Scan::Unclosed),
                                }
                            }
                            active.insert(name.to_string());
                            let body =
                                expand_function_like(def, &args, &expanded_args, table, active)?;
                            active.remove(name);
                            match body {
                                Scan::Text(s) => out.push_str(&s),
                                Scan::Unclosed => return Ok(Scan::Unclosed),
                            }
                        }
                    }
                }
            }
            continue;
        }
        out.push(c as char);
        i += 1;
    }
    Ok(Scan::Text(out))
}

/// Collect the comma-separated arguments of a macro call starting at `open`
/// (the '('). Returns (args, closed_on_line, index past the closing ')').
/// Respects nesting, string/char literals. Empty parens yield zero args —
/// `expand_function_like` maps that to a single empty argument when the
/// definition takes exactly one parameter (§6.10.3p4: `S()` is `S("")`).
fn collect_args(bytes: &[u8], open: usize) -> Result<(Vec<String>, bool, usize), String> {
    let mut args: Vec<String> = Vec::new();
    let mut cur = String::new();
    let mut depth = 0usize;
    let mut i = open;
    while i < bytes.len() {
        let c = bytes[i];
        match c {
            b'"' => { let e = skip_string(bytes, i, b'"'); cur.push_str(&bytes[i..e.min(bytes.len())].iter().map(|&b| b as char).collect::<String>()); i = e.min(bytes.len()); continue; }
            b'\'' => { let e = skip_string(bytes, i, b'\''); cur.push_str(&bytes[i..e.min(bytes.len())].iter().map(|&b| b as char).collect::<String>()); i = e.min(bytes.len()); continue; }
            b'(' => { depth += 1; if depth > 1 { cur.push('('); } }
            b')' => {
                depth -= 1;
                if depth == 0 {
                    if !(cur.is_empty() && args.is_empty()) { args.push(cur.trim().to_string()); }
                    return Ok((args, true, i + 1));
                }
                cur.push(')');
            }
            b',' if depth == 1 => { args.push(cur.trim().to_string()); cur.clear(); }
            _ => cur.push(c as char),
        }
        i += 1;
    }
    Ok((args, false, bytes.len()))
}

/// Substitute arguments into a function-like macro body (§6.10.3.1).
/// `args` is the raw argument text — what `#` and `##` operate on
/// (§6.10.3.2/.3) — and `expanded_args` its fully macro-expanded form, done by
/// the caller before `def.name` joined the hide set.
fn expand_function_like(
    def: &MacroDef,
    args: &[String],
    expanded_args: &[String],
    table: &HashMap<String, MacroDef>,
    active: &mut HashSet<String>,
) -> Result<Scan, String> {
    let params = def.params.as_deref().unwrap_or(&[]);
    let n_named = params.len();
    // §6.10.3p4: `M()` supplies one *empty* argument to a one-parameter macro
    // (`#define S(x) #x` / `S()` yields `""`), while for a zero-parameter macro
    // it is simply no arguments at all. `collect_args` reports zero args for the
    // empty parens, so widen it to the single empty argument the definition
    // needs — both spellings stay valid.
    let empty_call = args.is_empty() && n_named == 1 && !def.variadic;
    let normalized = if empty_call {
        Some((vec![String::new()], vec![String::new()]))
    } else {
        None
    };
    let (args, expanded_args): (&[String], &[String]) = match &normalized {
        Some((raw, exp)) => (raw.as_slice(), exp.as_slice()),
        None => (args, expanded_args),
    };
    if def.variadic {
        if args.len() < n_named {
            return Err(format!("macro '{}' expects at least {} argument(s), got {}",
                def.name, n_named, args.len()));
        }
    } else if args.len() != n_named {
        return Err(format!("macro '{}' expects {} argument(s), got {}",
            def.name, n_named, args.len()));
    }

    let va_raw: Vec<String> = if def.variadic { args[n_named..].to_vec() } else { Vec::new() };
    let va_expanded: Vec<String> = if def.variadic { expanded_args[n_named..].to_vec() } else { Vec::new() };

    let raw_of = |idx: usize| -> String {
        if idx < n_named { args[idx].clone() }
        else { va_raw.join(", ") } // __VA_ARGS__: raw trailing args
    };
    let exp_of = |idx: usize| -> String {
        if idx < n_named { expanded_args[idx].clone() }
        else { va_expanded.join(", ") }
    };
    let lookup = |pname: &str| -> Option<usize> {
        if pname == "__VA_ARGS__" && def.variadic { return Some(n_named); }
        params.iter().position(|p| p == pname)
    };

    // Token-scan the body, honoring # and ##.
    let body = def.body.as_str();
    let bytes = body.as_bytes();
    let mut out = String::new();
    let mut i = 0;
    // pending_paste: text waiting to be pasted (## right operand merges without space).
    let mut prev_ends_paste = false;
    // §6.10.3.3: `##` may only merge when BOTH operands produced text — an empty
    // argument is a placemarker, so `a ## b` with `a` empty stays two tokens
    // instead of gluing `b` onto whatever precedes it.
    let mut paste_glue = true;
    // GNU `,##__VA_ARGS__`: the literal comma is dropped when the varargs are empty.
    let mut paste_comma = false;
    // Set when the token just substituted is the left operand of an upcoming ##.
    let mut paste_left_empty = false;
    while i < bytes.len() {
        let c = bytes[i];
        // A pending paste consumes the surrounding whitespace: `a ## b`
        // concatenates with no space between the operands (§6.10.3.3).
        if prev_ends_paste && (c == b' ' || c == b'\t') {
            i += 1;
            continue;
        }
        if c == b'"' || c == b'\'' {
            let end = skip_string(bytes, i, c);
            push_token(&mut out, &body[i..end.min(bytes.len())], prev_ends_paste);
            prev_ends_paste = false;
            i = end.min(bytes.len());
            continue;
        }
        if c == b'#' && i + 1 < bytes.len() && bytes[i + 1] == b'#' {
            // ## paste: drop the space between out's tail and the next token.
            // (Spaces were emitted verbatim before we saw the ##; reclaim the
            // trailing run so `a ## b` truly concatenates, §6.10.3.3.)
            while out.ends_with(' ') { out.pop(); }
            // An empty left operand is a placemarker, so the right one must not
            // be glued (§6.10.3.3). A literal comma on the left is remembered so
            // GNU's `,##__VA_ARGS__` can drop it when the varargs are empty.
            paste_glue = !paste_left_empty;
            paste_comma = out.ends_with(',');
            prev_ends_paste = true;
            i += 2;
            continue;
        }
        if c == b'#' {
            // #param stringify (raw argument, §6.10.3.2).
            let mut j = i + 1;
            while j < bytes.len() && (bytes[j] == b' ' || bytes[j] == b'\t') { j += 1; }
            let s = j;
            while j < bytes.len() && is_ident_byte(bytes[j], j == s) { j += 1; }
            let pname = &body[s..j];
            match lookup(pname) {
                Some(idx) => {
                    push_token(&mut out, &stringify(&raw_of(idx)), prev_ends_paste && paste_glue);
                    prev_ends_paste = false;
                    paste_glue = true;
                    paste_comma = false;
                    i = j;
                    continue;
                }
                None => { out.push('#'); i += 1; continue; }
            }
        }
        if is_ident_byte(c, true) {
            let start = i;
            while i < bytes.len() && is_ident_byte(bytes[i], false) { i += 1; }
            let word = &body[start..i];
            // Left operand of ##: peek past whitespace for a following ##.
            let mut k = i;
            while k < bytes.len() && (bytes[k] == b' ' || bytes[k] == b'\t') { k += 1; }
            let is_left_paste = k + 1 < bytes.len() && bytes[k] == b'#' && bytes[k + 1] == b'#';
            match lookup(word) {
                Some(idx) => {
                    // Operands of ## use the RAW argument (§6.10.3.3):
                    // not expanded before pasting.
                    let text = if prev_ends_paste || is_left_paste { raw_of(idx) } else { exp_of(idx) };
                    paste_left_empty = is_left_paste && text.is_empty();
                    push_token(&mut out, &text, prev_ends_paste && paste_glue);
                    // `,##__VA_ARGS__` with no varargs: drop the comma entirely.
                    if paste_comma && text.is_empty() {
                        while out.ends_with(' ') { out.pop(); }
                        if out.ends_with(',') { out.pop(); }
                    }
                }
                None => {
                    paste_left_empty = false;
                    push_token(&mut out, word, prev_ends_paste && paste_glue);
                }
            }
            prev_ends_paste = false;
            paste_glue = true;
            paste_comma = false;
            continue;
        }
        out.push(c as char);
        i += 1;
    }
    // Rescan the substituted body for further macros (§6.10.3.1).
    expand_line(&out, table, active)
}

/// Append `tok`, separating from the previous token with a single space unless
/// a `##` paste is pending (paste concatenates directly) or no separator is
/// needed (start of output / punctuation that can't merge).
fn push_token(out: &mut String, tok: &str, paste: bool) {
    if tok.is_empty() { return; }
    let needs_space = !out.is_empty() && !paste && !can_glue(out, tok);
    if needs_space { out.push(' '); }
    out.push_str(tok);
}

/// True when appending `next` directly to `out` would not merge two distinct
/// preprocessor tokens into one (identifier chars, or `#`/`##` prefix).
fn can_glue(out: &str, next: &str) -> bool {
    let last = out.chars().last().unwrap_or(' ');
    let first = next.chars().next().unwrap_or(' ');
    let ident_last = last.is_alphanumeric() || last == '_';
    let ident_first = first.is_alphanumeric() || first == '_';
    !(ident_last && ident_first)
}

/// `#` stringification: escape `\` and `"` inside the raw argument text and
/// wrap in quotes; surrounding whitespace collapses to single spaces
/// (§6.10.3.2: each sequence of white space becomes one space).
fn stringify(raw: &str) -> String {
    let mut s = String::with_capacity(raw.len() + 2);
    s.push('"');
    let mut prev_space = true; // trims leading spaces
    for ch in raw.chars() {
        if ch == ' ' || ch == '\t' {
            if !prev_space { s.push(' '); prev_space = true; }
            continue;
        }
        prev_space = false;
        if ch == '\\' || ch == '"' { s.push('\\'); }
        s.push(ch);
    }
    while s.ends_with(' ') { s.pop(); }
    s.push('"');
    s
}

#[cfg(test)]
mod tests {
    use super::*;

    fn def(src: &str) -> MacroDef {
        // parse_define receives the text AFTER "#define" (preprocessor strips
        // the directive keyword before calling), so mirror that here.
        let rest = src.strip_prefix("#define").unwrap_or(src);
        parse_define(rest).unwrap()
    }

    fn tbl(defs: &[&str]) -> HashMap<String, MacroDef> {
        defs.iter().map(|d| { let m = def(d); (m.name.clone(), m) }).collect()
    }

    #[test]
    fn object_like_message_send() {
        let t = tbl(&["#define SEND(v) [v tag]"]);
        assert_eq!(expand("int x = SEND(p);", &t).unwrap(), "int x = [p tag];");
    }

    #[test]
    fn object_like_plain_passthrough_not_expanded_here() {
        // A body without nepa syntax is still fine to expand — but preprocessor
        // only routes nepa-syntax bodies here.
        let t = tbl(&["#define TWICE(x) ((x) + (x))"]);
        assert_eq!(expand("int y = TWICE(3);", &t).unwrap(), "int y = ((3) + (3));");
    }

    #[test]
    fn argument_expanded_before_substitution() {
        let t = tbl(&["#define TWICE(x) ((x) + (x))", "#define SEND(v) [v tag]"]);
        assert_eq!(expand("int z = TWICE(SEND(q));", &t).unwrap(), "int z = (([q tag]) + ([q tag]));");
    }

    #[test]
    fn self_recursion_frozen() {
        let t = tbl(&["#define FOO FOO + 1"]);
        assert_eq!(expand("int a = FOO;", &t).unwrap(), "int a = FOO + 1;");
    }

    #[test]
    fn mutual_recursion_terminates() {
        let t = tbl(&["#define A B", "#define B A"]);
        let r = expand("int a = A;", &t).unwrap();
        // A -> B -> A(frozen): one cycle then stop.
        assert_eq!(r, "int a = A;");
    }

    #[test]
    fn string_literal_not_scanned() {
        let t = tbl(&["#define SEND(v) [v tag]"]);
        assert_eq!(expand("kputs(\"SEND(p)\");", &t).unwrap(), "kputs(\"SEND(p)\");");
    }

    #[test]
    fn stringify_uses_raw_arg() {
        let t = tbl(&["#define STR(x) #x"]);
        assert_eq!(expand("const char *s = STR(SEND(p));", &t).unwrap(),
            "const char *s = \"SEND(p)\";");
    }

    #[test]
    fn paste_builds_identifier() {
        let t = tbl(&["#define MK(a, b) a ## _ ## b"]);
        assert_eq!(expand("int mk = MK(foo, bar);", &t).unwrap(), "int mk = foo_bar;");
    }

    #[test]
    fn paste_operand_not_expanded() {
        // Operands of ## are pasted RAW (§6.10.3.3): VAL must not become 42
        // before pasting, or the result would be `42_x` instead of `VAL_x`.
        // (The substituted body is then rescanned per §6.10.3.1 — but VAL_x
        // is not itself a macro, so it survives.)
        let t = tbl(&["#define VAL 42", "#define MK(a, b) a ## b"]);
        assert_eq!(expand("int mk = MK(VAL, _x);", &t).unwrap(), "int mk = VAL_x;");
    }

    #[test]
    fn nested_same_macro_call_in_argument_expands() {
        // §6.10.3.1: arguments are expanded before the invoked macro joins the
        // hide set, so the inner call expands too — only the rescan of the
        // substituted body freezes the name, which is what keeps the
        // self-recursive `G(x) G(x)` finite.
        let t = tbl(&["#define F(x) ((x) + 1)"]);
        let out = expand("int n = F(F(1));", &t).unwrap();
        let compact: String = out.chars().filter(|c| !c.is_whitespace()).collect();
        // `((x) + 1)` around `((1) + 1)` — the argument keeps its own parens.
        assert_eq!(compact, "intn=((((1)+1))+1);");

        let rec = tbl(&["#define G(x) G(x)"]);
        assert_eq!(expand("G(1);", &rec).unwrap(), "G(1);");
    }

    #[test]
    fn stringify_and_paste_edges_match_iso() {
        // Reference semantics taken from `clang -E -P` on the same macro set
        // (`Log a ## b` instead of `[Log a ## b]`):
        //   MK(, tag)    -> Log tag     (empty operand = placemarker)
        //   MK(tag, )    -> Log tag
        //   MK(, )       -> Log
        //   MK3(x,y,z)   -> Log xyz
        //   STR(a   b)   -> "a b"       (whitespace collapsed)
        //   STR()        -> ""
        //   VA(1, 2)     -> "1, 2"
        let t = tbl(&[
            "#define MK(a, b) [Log a ## b]",
            "#define MK3(a, b, c) [Log a ## b ## c]",
            "#define STR(x) [Log log:#x]",
            "#define VA(...) [Log log:#__VA_ARGS__]",
        ]);
        // §6.10.3.3: an empty operand is a placemarker, so the paste cannot
        // glue the other operand onto the preceding token.
        assert_eq!(expand("r MK(, tag);", &t).unwrap(), "r [Log tag];");
        assert_eq!(expand("r MK(tag, );", &t).unwrap(), "r [Log tag];");
        assert_eq!(expand("r MK(, );", &t).unwrap(), "r [Log];");
        assert_eq!(expand("r MK3(x, y, z);", &t).unwrap(), "r [Log xyz];");
        assert_eq!(expand("r MK(tag, _x);", &t).unwrap(), "r [Log tag_x];");
        // §6.10.3.2: stringify the RAW argument, whitespace collapsed.
        assert_eq!(expand("r STR(a   b);", &t).unwrap(), "r [Log log:\"a b\"];");
        assert_eq!(expand("r STR();", &t).unwrap(), "r [Log log:\"\"];");
        assert_eq!(expand("r STR(\"q\\z\");", &t).unwrap(),
            "r [Log log:\"\\\"q\\\\z\\\"\"];");
        assert_eq!(expand("r VA(1, 2);", &t).unwrap(), "r [Log log:\"1, 2\"];");
    }

    #[test]
    fn two_stage_stringify_sees_the_expanded_spelling() {
        // The usual XSTR/WRAP idiom: the inner call is expanded first and `#`
        // then stringifies the *spelling* of the result — so a placemarker
        // (`MK(, tag)`) shows up as the two separate tokens `Log tag`.
        let t = tbl(&[
            "#define STR(x) #x",
            "#define XSTR(x) STR(x)",
            "#define MK(pre, tag) Log pre ## tag",
        ]);
        assert_eq!(expand("r XSTR(MK(Log, tag));", &t).unwrap(), "r \"Log Logtag\";");
        assert_eq!(expand("r XSTR(MK(, tag));", &t).unwrap(), "r \"Log tag\";");
    }

    #[test]
    fn gnu_comma_swallow_for_empty_varargs() {
        // GNU `,##__VA_ARGS__` (the shape every logging macro in the wild uses):
        // the comma must vanish when the varargs are empty, and survive when
        // they are present. Without this, `LOGF("a")` would expand to a call
        // with a trailing comma and fail to compile.
        let t = tbl(&["#define LOGF(fmt, ...) [Log log:fmt, ##__VA_ARGS__]"]);
        assert_eq!(expand("r LOGF(\"a\");", &t).unwrap(), "r [Log log:\"a\"];");
        let two = expand("r LOGF(\"a\", 1, 2);", &t).unwrap();
        let compact: String = two.chars().filter(|c| !c.is_whitespace()).collect();
        assert_eq!(compact, "r[Loglog:\"a\",1,2];");
    }

    #[test]
    fn variadic_args() {
        let t = tbl(&["#define LOG(fmt, ...) [Log log:fmt, __VA_ARGS__]"]);
        assert_eq!(expand("LOG(@\"x\", 1, 2);", &t).unwrap(), "[Log log:@\"x\", 1, 2];");
    }

    #[test]
    fn variadic_empty() {
        let t = tbl(&["#define LOG(fmt, ...) [Log log:fmt, __VA_ARGS__]"]);
        assert_eq!(expand("LOG(@\"x\");", &t).unwrap(), "[Log log:@\"x\", ];");
    }

    #[test]
    fn bare_function_macro_name_stays() {
        let t = tbl(&["#define SEND(v) [v tag]"]);
        assert_eq!(expand("int (*f)(int) = SEND; // not a call", &t).unwrap(),
            "int (*f)(int) = SEND; // not a call");
    }

    #[test]
    fn multiline_continuation_joined_by_caller() {
        // The preprocessor joins `\` continuations before parse_define; the
        // parser just sees the joined body.
        let t = tbl(&["#define BIG(v) [v tag] + 1"]);
        assert_eq!(expand("int b = BIG(w);", &t).unwrap(), "int b = [w tag] + 1;");
    }

    #[test]
    fn arg_count_mismatch_is_error() {
        let t = tbl(&["#define SEND(v) [v tag]"]);
        let e = expand("SEND(p, q);", &t).unwrap_err();
        assert!(e.contains("expects 1 argument"), "{}", e);
    }

    #[test]
    fn body_has_nepa_detection() {
        assert!(body_has_nepa_syntax("[v tag]"));
        assert!(body_has_nepa_syntax("do { [v tag]; } while (0)"));
        assert!(body_has_nepa_syntax("@throw e"));
        assert!(body_has_nepa_syntax("^{ return 1; }"));
        assert!(!body_has_nepa_syntax("tab[x]"));
        assert!(!body_has_nepa_syntax("((int *)p)[i]"));
        assert!(!body_has_nepa_syntax("a[i] + b[j]"));
    }

    #[test]
    fn invocation_may_span_lines() {
        // §6.10.3: a function-like call is a token sequence — the newline
        // between arguments is whitespace, not a terminator. The scan pulls the
        // next source line in and expands the call as a whole.
        let t = tbl(&["#define SEND(v) [v tag]"]);
        assert_eq!(expand("int x = SEND(\n  p);", &t).unwrap(), "int x = [p tag];");
        let t2 = tbl(&["#define ADD(a, b) ((a) + (b))"]);
        assert_eq!(expand("int s = ADD(1,\n            2);", &t2).unwrap(),
            "int s = ((1) + (2));");
        let t3 = tbl(&["#define ADD3(a, b, c) ((a) + (b) + (c))"]);
        assert_eq!(expand("int s = ADD3(1,\n2,\n3);", &t3).unwrap(),
            "int s = ((1) + (2) + (3));");
    }

    #[test]
    fn nested_call_spanning_lines() {
        let t = tbl(&["#define ID(v) v", "#define SEND(v) [v tag]"]);
        assert_eq!(expand("int x = ID(\n  SEND(\n    q));", &t).unwrap(), "int x = [q tag];");
    }

    #[test]
    fn unclosed_call_reports_the_invocation_line() {
        let t = tbl(&["#define SEND(v) [v tag]"]);
        let e = expand("int a = 1;\nint x = SEND(p;\n", &t).unwrap_err();
        assert!(e.contains("line 2:"), "{}", e);
        assert!(e.contains("not closed"), "{}", e);
    }

    #[test]
    fn expand_mapped_attributes_output_to_invocation_line() {
        // Three source lines collapse into one expanded line: the map has one
        // entry per output line, pointing at the line the call starts on.
        let t = tbl(&["#define PAIR(a, b) ((a) + (b))"]);
        let (text, map) = expand_mapped("int s =\n    PAIR(1,\n         2);\n", &t).unwrap();
        assert_eq!(text, "int s =\n    ((1) + (2));\n");
        assert_eq!(map, vec![1, 2]);
    }

    #[test]
    fn expand_mapped_keeps_untouched_lines_in_place() {
        let t = tbl(&["#define ONE 1"]);
        let (text, map) = expand_mapped("a\nONE\nb\n", &t).unwrap();
        assert_eq!(text, "a\n1\nb\n");
        assert_eq!(map, vec![1, 2, 3]);
    }

    #[test]
    fn expand_mapped_without_table_is_identity() {
        let empty = HashMap::new();
        let (text, map) = expand_mapped("a\n\nb\n", &empty).unwrap();
        assert_eq!(text, "a\n\nb\n");
        assert_eq!(map, vec![1, 2, 3]);
    }

    #[test]
    fn pragma_in_macro_body_is_error() {
        // `_Pragma` takes effect at the *invocation* site (§6.10.9), a position
        // the source-level expander cannot place — so expanding the macro is a
        // clear error instead of dropping the pragma somewhere harmless-looking.
        let t = tbl(&["#define PUSH _Pragma(\"clang diagnostic push\")"]);
        let e = expand("PUSH int x = 1;", &t).unwrap_err();
        assert!(e.contains("PUSH"), "{}", e);
        assert!(e.contains("_Pragma"), "{}", e);
        assert!(e.contains("own source line"), "{}", e);
    }

    #[test]
    fn pragma_in_string_literal_is_not_the_operator() {
        // The operator must be a whole token outside literals/comments; a body
        // that merely spells it inside a string expands normally.
        let t = tbl(&["#define MSG \"_Pragma(x)\"", "#define NAME MY_Pragma"]);
        assert_eq!(expand("kputs(MSG);", &t).unwrap(), "kputs(\"_Pragma(x)\");");
        assert_eq!(expand("int NAME;", &t).unwrap(), "int MY_Pragma;");
    }
}
