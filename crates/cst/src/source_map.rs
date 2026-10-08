//! Source location model — the single source of truth for file identity and
//! position mapping (doc/source_locations_debug_lsp_plan.md, 阶段 1).
//!
//! Every file read or imported gets a stable [`SourceId`]. The flattened
//! post-preprocessing buffer (what the lexer/parser see) maps *line ranges*
//! back to (file, line range) instead of the old per-line string table, and
//! regions with no real source (macro expansion output, generated text) are
//! marked [`SpanOrigin::Synthetic`].
//!
//! The legacy API ([`SourceMap::locate`] etc.) is kept for the diagnostic path;
//! consumers migrate to it incrementally. New nodes must not store bare
//! (line, col) without a file identity.

/// Identity of one source file within a compilation.
/// Indices into [`SourceRegistry::files`].
pub type SourceId = u32;

/// A registered source file: normalized path, display path, and line-start
/// byte offsets (for line <-> byte-offset conversion).
#[derive(Debug, Clone)]
pub struct SourceFile {
    /// Path as used for dedup / import resolution (what the line table stored).
    pub path: String,
    /// Byte offset of the start of each line (entry 0 is always 0).
    pub line_starts: Vec<usize>,
    /// Total byte length of the source text.
    pub len: usize,
}

impl SourceFile {
    /// Convert a 1-based line number to a byte offset (clamped to the file).
    pub fn line_to_offset(&self, line: u32) -> Option<usize> {
        if line == 0 {
            return None;
        }
        self.line_starts.get(line as usize - 1).copied()
    }

    /// Convert a byte offset to a 1-based line number.
    pub fn offset_to_line(&self, offset: usize) -> u32 {
        match self.line_starts.binary_search(&offset) {
            Ok(i) => i as u32 + 1,
            Err(i) => i as u32, // offset is inside line i (1-based)
        }
    }

    /// Convert a byte offset to a 1-based column (UTF-8 bytes).
    /// Requires `text` (the file content); kept separate so the registry stays
    /// lightweight when only line mapping is needed.
    pub fn offset_to_col(text: &str, offset: usize) -> u32 {
        if offset >= text.len() {
            return 1;
        }
        let line_start = text[..offset]
            .rfind('\n')
            .map(|i| i + 1)
            .unwrap_or(0);
        // Column in bytes +1, matching the existing parser convention.
        (offset - line_start + 1) as u32
    }
}

/// Registry of all source files in one compilation. `SourceId 0` is reserved
/// for the main file so a default `SourceSpan` never aliases a real import.
#[derive(Debug, Clone, Default)]
pub struct SourceRegistry {
    pub files: Vec<SourceFile>,
}

impl SourceRegistry {
    pub fn new() -> Self {
        SourceRegistry { files: Vec::new() }
    }

    /// Register a file (dedup by path); returns its SourceId.
    /// `text` is used to build the line-start table.
    pub fn add(&mut self, path: &str, text: &str) -> SourceId {
        if let Some(id) = self.id_of(path) {
            return id;
        }
        let line_starts: Vec<usize> = std::iter::once(0)
            .chain(
                text.bytes()
                    .enumerate()
                    .filter(|&(_, b)| b == b'\n')
                    .map(|(i, _)| i + 1),
            )
            .collect();
        let id = self.files.len() as SourceId;
        self.files.push(SourceFile {
            path: path.to_string(),
            line_starts,
            len: text.len(),
        });
        id
    }

    pub fn id_of(&self, path: &str) -> Option<SourceId> {
        self.files.iter().position(|f| f.path == path).map(|i| i as SourceId)
    }

    pub fn get(&self, id: SourceId) -> Option<&SourceFile> {
        self.files.get(id as usize)
    }

    pub fn path_of(&self, id: SourceId) -> &str {
        self.files.get(id as usize).map(|f| f.path.as_str()).unwrap_or("")
    }
}

/// Where an emitted line's content came from.
#[derive(Debug, Clone, Copy, PartialEq, Eq, Default)]
pub enum SpanOrigin {
    /// Text exists verbatim in a source file.
    #[default]
    Source,
    /// Generated text with no single source line (macro expansion output past
    /// the invocation site, future codegen helpers). Carries no file mapping.
    Synthetic,
}

/// A half-open `[start, end)` byte range in a registered source file.
/// Rows/columns are derived for display; bytes are the precise identity.
#[derive(Debug, Clone, Copy, PartialEq, Eq)]
pub struct SourceSpan {
    pub file: SourceId,
    /// 1-based start line.
    pub start_line: u32,
    /// 1-based end line (inclusive in line terms for diagnostics display).
    pub end_line: u32,
    pub origin: SpanOrigin,
}

impl SourceSpan {
    pub fn is_synthetic(&self) -> bool {
        self.origin == SpanOrigin::Synthetic
    }
}

/// One region of the flattened (post-preprocessing) buffer, mapped back to
/// its source file and 1-based line range.
#[derive(Debug, Clone, Copy)]
pub struct LineRegion {
    /// 1-based start line in the flattened buffer.
    pub out_start: u32,
    /// 1-based end line in the flattened buffer (inclusive).
    pub out_end: u32,
    pub file: SourceId,
    /// 1-based start line in the source file.
    pub src_start: u32,
    pub origin: SpanOrigin,
}

/// Maps flattened-buffer lines to source files via [`LineRegion`]s.
///
/// Built by the preprocessor; consumed by the diagnostic path
/// (`translate_lines`) and, later, codegen `#line` emission (阶段 2).
#[derive(Debug, Clone, Default)]
pub struct SourceMap {
    regions: Vec<LineRegion>,
    /// Files touched by this mapping (parallel data, indexed by SourceId).
    registry: SourceRegistry,
}

impl SourceMap {
    /// Build from a region list + registry.
    pub fn from_regions(regions: Vec<LineRegion>, registry: SourceRegistry) -> Self {
        SourceMap { regions, registry }
    }

    pub fn regions(&self) -> &[LineRegion] {
        &self.regions
    }

    pub fn registry(&self) -> &SourceRegistry {
        &self.registry
    }

    pub fn is_empty(&self) -> bool {
        self.regions.is_empty()
    }

    /// Number of flattened-buffer lines covered (used by legacy `len`).
    pub fn out_lines(&self) -> u32 {
        self.regions.last().map(|r| r.out_end).unwrap_or(0)
    }

    /// Locate a flattened-buffer line: returns (display file, 1-based line).
    /// Unmapped lines resolve to ("", line) — the legacy behavior for
    /// lines the preprocessor did not tag.
    pub fn locate(&self, inlined_line: usize) -> (String, u32) {
        let l = inlined_line as u32;
        for r in &self.regions {
            if l >= r.out_start && l <= r.out_end {
                let real = r.src_start + (l - r.out_start);
                return (self.registry.path_of(r.file).to_string(), real);
            }
        }
        (String::new(), l)
    }

    /// Structured variant of [`locate`] for new consumers.
    pub fn locate_span(&self, inlined_line: usize) -> SourceSpan {
        let l = inlined_line as u32;
        for r in &self.regions {
            if l >= r.out_start && l <= r.out_end {
                return SourceSpan {
                    file: r.file,
                    start_line: r.src_start + (l - r.out_start),
                    end_line: r.src_start + (l - r.out_start),
                    origin: r.origin,
                };
            }
        }
        SourceSpan {
            file: 0,
            start_line: l,
            end_line: l,
            origin: SpanOrigin::Synthetic,
        }
    }
}

// ---------------------------------------------------------------------------
// Legacy line-table API (kept until all consumers migrate; plan 阶段 1 keeps
// 兼容访问器 to avoid a one-shot big-bang change).
// ---------------------------------------------------------------------------

impl SourceMap {
    /// Legacy constructor: build the region list from the old per-line
    /// `(file, line)` table, merging consecutive same-file runs.
    pub fn new(lines: Vec<(String, u32)>) -> Self {
        let mut registry = SourceRegistry::new();
        // Register every distinct file; dedup keeps legacy behavior where the
        // same path shown repeatedly maps to one identity.
        for (path, _) in &lines {
            if registry.id_of(path).is_none() {
                registry.add(path, "");
            }
        }
        let mut regions: Vec<LineRegion> = Vec::new();
        for (i, (path, line)) in lines.iter().enumerate() {
            let file = registry.id_of(path).expect("registered above") as SourceId;
            let out_start = i as u32 + 1;
            match regions.last_mut() {
                Some(r)
                    if r.file == file
                        && r.origin == SpanOrigin::Source
                        && r.out_end + 1 == out_start
                        && r.src_start + (r.out_end - r.out_start) + 1 == *line =>
                {
                    r.out_end = out_start;
                }
                _ => regions.push(LineRegion {
                    out_start,
                    out_end: out_start,
                    file,
                    src_start: *line,
                    origin: SpanOrigin::Source,
                }),
            }
        }
        SourceMap { regions, registry }
    }

    /// Build from the old per-line `(file, line)` table but reuse an existing
    /// registry (the preprocessor registers files with real text; the legacy
    /// path above re-registers with empty text). Files present in `lines` but
    /// missing from the registry are added with empty text.
    pub fn from_line_table(lines: Vec<(String, u32)>, registry: &mut SourceRegistry) -> Self {
        for (path, _) in &lines {
            if registry.id_of(path).is_none() {
                registry.add(path, "");
            }
        }
        let mut regions: Vec<LineRegion> = Vec::new();
        for (i, (path, line)) in lines.iter().enumerate() {
            let file = registry
                .id_of(path)
                .expect("registered above") as SourceId;
            let out_start = i as u32 + 1;
            match regions.last_mut() {
                Some(r)
                    if r.file == file
                        && r.origin == SpanOrigin::Source
                        && r.out_end + 1 == out_start
                        && r.src_start + (r.out_end - r.out_start) + 1 == *line =>
                {
                    r.out_end = out_start;
                }
                _ => regions.push(LineRegion {
                    out_start,
                    out_end: out_start,
                    file,
                    src_start: *line,
                    origin: SpanOrigin::Source,
                }),
            }
        }
        let owned = std::mem::take(registry);
        SourceMap { regions, registry: owned }
    }

    /// Total number of mapped flattened-buffer lines.
    pub fn len(&self) -> usize {
        self.out_lines() as usize
    }
}

#[cfg(test)]
mod tests {
    use super::*;

    #[test]
    fn legacy_line_table_locate() {
        let sm = SourceMap::new(vec![
            ("main.np".into(), 1),
            ("main.np".into(), 2),
            ("foo.nh".into(), 10),
        ]);
        assert_eq!(sm.locate(1), ("main.np".to_string(), 1));
        assert_eq!(sm.locate(2), ("main.np".to_string(), 2));
        assert_eq!(sm.locate(3), ("foo.nh".to_string(), 10));
        assert_eq!(sm.locate(4), (String::new(), 4));
    }

    #[test]
    fn regions_merge_same_file_runs() {
        let sm = SourceMap::new(vec![
            ("a.np".into(), 1),
            ("a.np".into(), 2),
            ("a.np".into(), 3),
        ]);
        assert_eq!(sm.regions().len(), 1);
        assert_eq!(sm.regions()[0].out_start, 1);
        assert_eq!(sm.regions()[0].out_end, 3);
    }

    #[test]
    fn registry_line_offsets() {
        let mut reg = SourceRegistry::new();
        let id = reg.add("x.np", "one\ntwo\nthree");
        let f = reg.get(id).unwrap();
        assert_eq!(f.line_to_offset(2), Some(4));
        assert_eq!(f.offset_to_line(6), 2); // inside "two"
        assert_eq!(SourceFile::offset_to_col("one\ntwo\nthree", 6), 3);
    }

    #[test]
    fn locate_span_reports_file_identity() {
        let mut reg = SourceRegistry::new();
        let a = reg.add("a.np", "line1\nline2\n");
        let regions = vec![LineRegion {
            out_start: 1,
            out_end: 2,
            file: a,
            src_start: 5,
            origin: SpanOrigin::Source,
        }];
        let sm = SourceMap::from_regions(regions, reg);
        let s = sm.locate_span(2);
        assert_eq!(s.file, a);
        assert_eq!(s.start_line, 6);
        assert!(!s.is_synthetic());
    }
}
