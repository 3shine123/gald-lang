// Dead code is a build failure, not a warning: a function nobody calls is
// either a bug or a leftover, and both should surface at compile time rather
// than rot unnoticed. Mark intentional exceptions with #[allow(dead_code)]
// and a comment saying who will use it.
#![deny(dead_code)]
pub mod ctype_probe;
pub mod pipeline;
pub use pipeline::*;