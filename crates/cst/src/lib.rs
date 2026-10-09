// Dead code is a build failure, not a warning: a function nobody calls is
// either a bug or a leftover, and both should surface at compile time rather
// than rot unnoticed. Mark intentional exceptions with #[allow(dead_code)]
// and a comment saying who will use it.
#![deny(dead_code)]
pub mod cst;
pub mod diagnostic;
pub mod source_map;
pub mod source_map_file;
pub use cst::*;
pub use diagnostic::*;
pub use source_map::*;
pub use source_map_file::*;