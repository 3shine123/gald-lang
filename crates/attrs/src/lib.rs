// Dead code is a build failure, not a warning: a function nobody calls is
// either a bug or a leftover, and both should surface at compile time rather
// than rot unnoticed. Mark intentional exceptions with #[allow(dead_code)]
// and a comment saying who will use it.
#![deny(dead_code)]
//! Attribute capability matrix used to gate which `__attribute__((...))`
//! spellings ovicc allows, driven by the `--backend` option.
//!
//! Classification rules (see `table.rs` for the data source):
//!   - `portable` (default): only `AttrClass::Common`
//!   - `clang`:              `Common` ∪ `ClangOnly`
//!   - `gcc`:                `Common` ∪ `GccOnly`
//!
//! Attributes not present in the table are "unknown": the backend gate
//! treats them as allowed-with-warning rather than hard errors, so legitimate
//! attributes added to a compiler without our table being updated are not
//! misrejected.

mod table;

pub use table::{CLANG_ONLY_ATTRS, COMMON_ATTRS, GCC_ONLY_ATTRS};

/// Which compiler(s) accept a given attribute name.
#[derive(Debug, Clone, Copy, PartialEq, Eq)]
pub enum AttrClass {
    /// Accepted by both gcc and clang.
    Common,
    /// Accepted only by clang.
    ClangOnly,
    /// Accepted only by gcc.
    GccOnly,
}

/// The C backend the transpiled output must compile against.
#[derive(Debug, Clone, Copy, PartialEq, Eq, Default)]
pub enum Backend {
    /// Output must compile with both gcc and clang (only `Common` attributes).
    #[default]
    Portable,
    /// Output only needs to compile with clang.
    Clang,
    /// Output only needs to compile with gcc.
    Gcc,
}

impl std::fmt::Display for Backend {
    fn fmt(&self, f: &mut std::fmt::Formatter<'_>) -> std::fmt::Result {
        f.write_str(match self {
            Backend::Portable => "portable",
            Backend::Clang => "clang",
            Backend::Gcc => "gcc",
        })
    }
}

impl Backend {
    pub fn parse(s: &str) -> Option<Self> {
        match s {
            "portable" => Some(Backend::Portable),
            "clang" => Some(Backend::Clang),
            "gcc" => Some(Backend::Gcc),
            _ => None,
        }
    }
}

/// Look up the compiler-support class for a single attribute name.
///
/// Returns `None` for attributes not in the table (unknown).
pub fn classify(name: &str) -> Option<AttrClass> {
    if COMMON_ATTRS.binary_search(&name).is_ok() {
        Some(AttrClass::Common)
    } else if CLANG_ONLY_ATTRS.binary_search(&name).is_ok() {
        Some(AttrClass::ClangOnly)
    } else if GCC_ONLY_ATTRS.binary_search(&name).is_ok() {
        Some(AttrClass::GccOnly)
    } else {
        None
    }
}

/// Whether `backend` permits an attribute of the given class.
pub fn allowed_for(class: AttrClass, backend: Backend) -> bool {
    match (class, backend) {
        (AttrClass::Common, _) => true,
        (AttrClass::ClangOnly, Backend::Clang) => true,
        (AttrClass::GccOnly, Backend::Gcc) => true,
        _ => false,
    }
}

/// How a single attribute should be handled under a backend:
/// pass through, hard error (known but not allowed), or warn (unknown).
#[derive(Debug, Clone, Copy, PartialEq, Eq)]
pub enum AttrDisposition {
    Pass,
    Error,
    Warn,
}

/// Decide what to do with an attribute name under the chosen backend.
pub fn disposition(name: &str, backend: Backend) -> AttrDisposition {
    match classify(name) {
        None => AttrDisposition::Warn,
        Some(class) if allowed_for(class, backend) => AttrDisposition::Pass,
        Some(_) => AttrDisposition::Error,
    }
}

#[cfg(test)]
mod tests {
    use super::*;

    #[test]
    fn common_attrs_pass_everywhere() {
        for backend in [Backend::Portable, Backend::Clang, Backend::Gcc] {
            assert_eq!(disposition("packed", backend), AttrDisposition::Pass);
            assert_eq!(disposition("visibility", backend), AttrDisposition::Pass);
            assert_eq!(disposition("destructor", backend), AttrDisposition::Pass);
        }
    }

    #[test]
    fn clang_only_gated() {
        assert_eq!(disposition("availability", Backend::Portable), AttrDisposition::Error);
        assert_eq!(disposition("availability", Backend::Gcc), AttrDisposition::Error);
        assert_eq!(disposition("availability", Backend::Clang), AttrDisposition::Pass);
    }

    #[test]
    fn gcc_only_gated() {
        assert_eq!(disposition("strub", Backend::Portable), AttrDisposition::Error);
        assert_eq!(disposition("strub", Backend::Clang), AttrDisposition::Error);
        assert_eq!(disposition("strub", Backend::Gcc), AttrDisposition::Pass);
    }

    #[test]
    fn unknown_warns_not_errors() {
        for backend in [Backend::Portable, Backend::Clang, Backend::Gcc] {
            assert_eq!(disposition("zzz_no_such_attr", backend), AttrDisposition::Warn);
        }
    }

    #[test]
    fn table_is_sorted() {
        for arr in [COMMON_ATTRS, CLANG_ONLY_ATTRS, GCC_ONLY_ATTRS] {
            let mut v: Vec<&str> = arr.to_vec();
            v.sort_unstable();
            v.dedup();
            assert_eq!(arr.len(), v.len(), "duplicate in sorted attr array");
            assert_eq!(arr, v.as_slice(), "attr array not sorted");
        }
    }
}
