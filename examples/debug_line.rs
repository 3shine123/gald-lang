use std::fs;
fn main() {
    let content = fs::read_to_string("tests/static_generics_template_test.np").unwrap();
    let search_dirs = vec![
        "include".into(),
        ".".into(),
        "include/Foundation".into(),
    ];
    let pre = nopa_preprocessor::Preprocessor::process(&content, "tests/static_generics_template_test.np", &search_dirs).unwrap();
    let lines: Vec<&str> = pre.resolved_nopa.lines().collect();
    eprintln!("Total lines: {}", lines.len());
    for i in 69..(lines.len().min(90)) {
        let marker = if i+1 == 77 { " >>>" } else { "    " };
        eprintln!("{} {:>4}: {}", marker, i+1, lines[i]);
    }
}
