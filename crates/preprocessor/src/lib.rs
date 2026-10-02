// Dead code is a build failure, not a warning: a function nobody calls is
// either a bug or a leftover, and both should surface at compile time rather
// than rot unnoticed. Mark intentional exceptions with #[allow(dead_code)]
// and a comment saying who will use it.
#![deny(dead_code)]
pub mod preprocessor;
pub use preprocessor::*;

#[cfg(test)]
mod tests {
    use super::*;
    use std::path::Path;

    #[test]
    fn test_hello_np() {
        let manifest = Path::new(env!("CARGO_MANIFEST_DIR"));
        let workspace = manifest.parent().unwrap().parent().unwrap();
        let hello = workspace.join("tests/golden/01_basics/hello.gm");
        let source = std::fs::read_to_string(&hello).unwrap();
        let include = workspace.join("include").to_str().unwrap().to_string();
        let foundation = workspace.join("include/Foundation").to_str().unwrap().to_string();
        let pre = Preprocessor::process(&source, hello.to_str().unwrap(), &[include, ".".into(), foundation], &["__clang__"]).unwrap();
        println!("=== NUPA OUT ===");
        println!("{}", pre.resolved_gald);
        println!("=== C HEADERS ===");
        for h in &pre.c_headers {
            println!("{}", h);
        }
    }
}
