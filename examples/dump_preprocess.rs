use nepa_preprocessor::Preprocessor;

fn main() {
    let search_dirs = vec![
        "include".into(),
        ".".into(),
        "include/Foundation".into(),
    ];
    let pre = Preprocessor::process_file("tests/static_generics_template_test.np", &search_dirs).unwrap();
    let lines: Vec<&str> = pre.resolved_nepa.lines().collect();
    eprintln!("Total lines: {}", lines.len());
    for (i, line) in lines.iter().enumerate() {
        if i+1 >= 70 && i+1 <= 85 {
            eprintln!("{:>4}: {}", i+1, line);
        }
    }
}
