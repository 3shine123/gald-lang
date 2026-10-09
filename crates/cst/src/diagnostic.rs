//! Structured diagnostics shared by every front-end stage.
//!
//! Stages record `Diagnostic` values instead of pre-formatted strings; the
//! renderer (nepac side) turns them into either clang-style annotated source
//! output or plain `file:line:col: message` lines. Rendering lives elsewhere
//! so the recorder never needs source text or colors.

/// Severity of a [`Diagnostic`]. Order matters: errors dominate warnings when
/// callers ask "did anything fail".
#[derive(Debug, Clone, Copy, PartialEq, Eq, PartialOrd, Ord)]
pub enum Severity {
    Warning,
    Error,
    Note,
}

impl Severity {
    /// Lowercase word used in rendered headers (`error:`, `warning:`).
    pub fn as_str(self) -> &'static str {
        match self {
            Severity::Error => "error",
            Severity::Warning => "warning",
            Severity::Note => "note",
        }
    }
}

/// One diagnostic: where it points and what it says.
///
/// `col` is the 1-based byte column of the primary caret. `end_col`, when
/// present, is the exclusive end of the annotated range and is rendered as
/// `~~~~^~~~~` (tildes up to the caret, matching clang). Diagnostics recorded
/// before spans were threaded through pass `end_col: None`, which renders a
/// single `^`.
#[derive(Debug, Clone, PartialEq, Eq)]
pub struct Diagnostic {
    pub severity: Severity,
    /// File as reported by the SourceMap (`locate`). Empty for synthesized
    /// positions with no backing file.
    pub file: String,
    /// 1-based source line (already remapped through the SourceMap).
    pub line: usize,
    /// 1-based source column (already remapped through the SourceMap).
    pub col: usize,
    /// Exclusive end column of the annotated span on the same line, if known.
    pub end_col: Option<usize>,
    pub message: String,
}

impl Diagnostic {
    pub fn error(file: impl Into<String>, line: usize, col: usize, message: impl Into<String>) -> Self {
        Diagnostic {
            severity: Severity::Error,
            file: file.into(),
            line,
            col,
            end_col: None,
            message: message.into(),
        }
    }

    pub fn warning(file: impl Into<String>, line: usize, col: usize, message: impl Into<String>) -> Self {
        Diagnostic {
            severity: Severity::Warning,
            file: file.into(),
            line,
            col,
            end_col: None,
            message: message.into(),
        }
    }

    pub fn note(file: impl Into<String>, line: usize, col: usize, message: impl Into<String>) -> Self {
        Diagnostic {
            severity: Severity::Note,
            file: file.into(),
            line,
            col,
            end_col: None,
            message: message.into(),
        }
    }

    /// Builder-style span setter for call sites that know the width.
    pub fn with_end_col(mut self, end_col: usize) -> Self {
        self.end_col = Some(end_col);
        self
    }

    /// Plain `file:line:col: message` form — the historical format scripts
    /// and golden files may grep for. Keeps the `[checker]`-style prefixes
    /// out of the structured layer. A zero line marks a position-less
    /// (synthesized) diagnostic: only the message is rendered.
    pub fn to_plain(&self) -> String {
        if self.line == 0 {
            return self.message.clone();
        }
        if self.file.is_empty() {
            format!("{}:{}: {}", self.line, self.col, self.message)
        } else {
            format!("{}:{}:{}: {}", self.file, self.line, self.col, self.message)
        }
    }
}

/// Render a list of diagnostics back to the historical plain lines. Used for
/// the fallback output format and by unit tests.
pub fn render_plain(diags: &[Diagnostic]) -> String {
    diags
        .iter()
        .map(Diagnostic::to_plain)
        .collect::<Vec<_>>()
        .join("\n")
}

/// Look up the full text of a source file by the name a diagnostic carries.
/// Returns `None` when the file cannot be read (rendering then falls back to
/// the plain one-line form for that diagnostic).
pub type SourceLookup<'a> = dyn Fn(&str) -> Option<String> + 'a;

/// Render diagnostics clang-style, with the offending source line and a
/// `~~~~^~~~~` annotation under it:
///
/// ```text
/// error: null passed to a callee that requires a non-null argument
///   --> foo.np:27:14
///    |
/// 27 |     [g greet:nil];
///    |            ~~~ ^
/// ```
///
/// Diagnostics whose file is empty, or whose file the lookup cannot provide,
/// fall back to the plain `file:line:col:` line so nothing is ever dropped.
pub fn render_annotated(diags: &[Diagnostic], lookup: &SourceLookup) -> String {
    render_annotated_colored(diags, lookup, false)
}

/// Same layout as [`render_annotated`], with ANSI colors when `color` is set:
/// severity word bold red/yellow, caret annotation in the severity color.
/// Callers gate this on stderr being a TTY.
pub fn render_annotated_colored(diags: &[Diagnostic], lookup: &SourceLookup, color: bool) -> String {
    diags
        .iter()
        .map(|d| render_one(d, lookup, color))
        .collect::<Vec<_>>()
        .join("\n")
}

fn severity_color(d: &Diagnostic, color: bool) -> (&'static str, &'static str) {
    if !color {
        ("", "")
    } else {
        match d.severity {
            Severity::Error => ("\x1b[1;31m", "\x1b[0m"),
            Severity::Warning => ("\x1b[1;33m", "\x1b[0m"),
            Severity::Note => ("\x1b[1;36m", "\x1b[0m"),
        }
    }
}

fn render_one(d: &Diagnostic, lookup: &SourceLookup, color: bool) -> String {
    let source = if d.file.is_empty() {
        None
    } else {
        lookup(&d.file)
    };
    let Some(text) = source else {
        return d.to_plain();
    };
    let Some(line_text) = text.lines().nth(d.line.checked_sub(1).unwrap_or(usize::MAX)) else {
        return d.to_plain();
    };

    let (s_on, s_off) = severity_color(d, color);
    let ln = d.line.to_string();
    let gutter = " ".repeat(ln.len());
    let mut out = String::new();
    out.push_str(&format!(
        "{}{}{}: {}\n{}--> {}:{}:{}\n{} |\n{} | {}\n",
        s_on,
        d.severity.as_str(),
        s_off,
        d.message,
        gutter,
        d.file,
        d.line,
        d.col,
        gutter,
        ln,
        line_text
    ));
    out.push_str(&format!("{} | ", gutter));
    // Bytes before the caret render as spaces; multi-byte source content can
    // desync byte columns from display columns, but Nepa source is effectively
    // ASCII on code lines — revisit with display width if that changes.
    let col = d.col.max(1);
    out.push_str(&" ".repeat((col - 1).min(line_text.len())));
    match d.end_col {
        Some(end) if end > col + 1 => {
            // clang shape: caret at the primary column, tildes through the
            // rest of the span.
            let width = (end - col).min(line_text.len().saturating_sub(col - 1)).max(1);
            out.push_str(&format!("{}^{}{}", s_on, "~".repeat(width - 1), s_off));
        }
        _ => {
            // No recorded width: underline the token under the caret when the
            // caret sits at its start (previous char is not identifier-ish) —
            // `nil`, identifiers, keywords. Mid-token carets keep a plain ^.
            let b = line_text.as_bytes();
            let idx = (col - 1).min(b.len());
            let is_word = |c: u8| c.is_ascii_alphanumeric() || c == b'_';
            let mut width = 1usize;
            if idx < b.len() && (is_word(b[idx]) || b[idx] == b'@')
                && (idx == 0 || !is_word(b[idx - 1]))
            {
                let mut j = idx + 1;
                if b[idx] == b'@' && j < b.len() && b[j] == b'(' {
                    // `@(...)` boxed expression: extend through the balanced
                    // closing paren so the annotation covers the whole expression.
                    let mut depth = 0usize;
                    while j < b.len() {
                        match b[j] {
                            b'(' => depth += 1,
                            b')' => {
                                depth -= 1;
                                if depth == 0 {
                                    j += 1;
                                    break;
                                }
                            }
                            _ => {}
                        }
                        j += 1;
                    }
                    width = j - idx;
                } else {
                    while j < b.len() && is_word(b[j]) { j += 1; }
                    width = j - idx;
                }
            }
            if width > 1 {
                out.push_str(&format!("{}^{}{}", s_on, "~".repeat(width - 1), s_off));
            } else {
                out.push_str(&format!("{}^{}", s_on, s_off));
            }
        }
    }
    out
}

#[cfg(test)]
mod tests {
    use super::*;

    #[test]
    fn plain_format_matches_historical_layout() {
        let d = Diagnostic::error("foo.np", 27, 14, "boom");
        assert_eq!(d.to_plain(), "foo.np:27:14: boom");
        let d = Diagnostic::warning(String::new(), 3, 5, "synthetic");
        assert_eq!(d.to_plain(), "3:5: synthetic");
    }

    #[test]
    fn plain_lines_join_in_order() {
        let ds = vec![
            Diagnostic::error("a.np", 1, 1, "one"),
            Diagnostic::warning("a.np", 2, 1, "two"),
        ];
        assert_eq!(render_plain(&ds), "a.np:1:1: one\na.np:2:1: two");
    }

    fn lookup_ok(file: &str) -> Option<String> {
        if file == "foo.np" {
            Some("int main(void) {\n    [g greet:nil];\n    return 0;\n}\n".into())
        } else {
            None
        }
    }

    #[test]
    fn annotated_renders_caret_with_gutter() {
        let d = Diagnostic::error("foo.np", 2, 9, "boom");
        let out = render_annotated(&[d], &lookup_ok);
        assert_eq!(
            out,
            "error: boom\n --> foo.np:2:9\n  |\n2 |     [g greet:nil];\n  |         ^"
        );
    }

    #[test]
    fn annotated_renders_clang_tilde_span() {
        // span columns 13..18 → caret at 13, tildes through 17
        let d = Diagnostic::error("foo.np", 2, 13, "bad arg").with_end_col(18);
        let out = render_annotated(&[d], &lookup_ok);
        let line = out.lines().last().unwrap();
        assert_eq!(line, "  |             ^~~~~");
    }

    #[test]
    fn annotated_falls_back_to_plain_when_file_unreadable() {
        let d = Diagnostic::error("missing.np", 4, 7, "nope");
        let out = render_annotated(&[d], &lookup_ok);
        assert_eq!(out, "missing.np:4:7: nope");
    }

    #[test]
    fn annotated_falls_back_when_line_out_of_range() {
        let d = Diagnostic::error("foo.np", 99, 3, "beyond");
        let out = render_annotated(&[d], &lookup_ok);
        assert_eq!(out, "foo.np:99:3: beyond");
    }
}
