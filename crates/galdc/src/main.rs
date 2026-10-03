use std::env;
use std::fs;
use std::io::Write;
use std::path::Path;
use std::process::{Command, Stdio};
use clap::{Command as ClapCommand, Arg};
use clap_complete::{Shell, generate};
use galdc::pipeline::{Pipeline, DEFAULT_EH_CHECKED};
use attrs;

/// Locate the installed galdc bundle root (the directory containing `include/`).
///
/// Resolution order (first candidate containing `include/gald/runtime.h` wins):
///   1. `$GALD_HOME` — explicit override (also works when installed anywhere)
///   2. the executable's own directory (bundle layout: `bin/galdc` + `include/`)
///   3. up to three parent directories (dev layout: `target/<profile>/galdc`)
///   4. the current directory (last-resort fallback)
///
/// This replaces the old hard-coded "three parents up" guess, which broke once
/// the install layout (e.g. on Windows) differed from `target/release/galdc`.
fn resolve_bundle_root() -> std::path::PathBuf {
    let mut candidates: Vec<std::path::PathBuf> = Vec::new();
    if let Ok(home) = env::var("GALD_HOME") {
        if !home.trim().is_empty() {
            candidates.push(std::path::PathBuf::from(home));
        }
    }
    if let Ok(exe) = env::current_exe() {
        let exe = std::fs::canonicalize(&exe).unwrap_or(exe);
        if let Some(dir) = exe.parent() {
            // Prefer the bundle root (`PREFIX`, where `bin/galdc` lives →
            // `PREFIX/include`) and the dev root (`target/<profile>/galdc` →
            // `project/include`) BEFORE the executable's own directory. The
            // latter may contain a *stale* `include/` copied by build.rs, which
            // must not shadow the source tree.
            let mut cur = dir.to_path_buf();
            for _ in 0..3 {
                match cur.parent() {
                    Some(p) => { cur = p.to_path_buf(); candidates.push(cur.clone()); }
                    None => break,
                }
            }
            candidates.push(dir.to_path_buf());
        }
    }
    candidates.push(std::path::PathBuf::from("."));
    for c in &candidates {
        if c.join("include").join("gald").join("runtime.h").exists() {
            return c.clone();
        }
    }
    candidates.into_iter().next().unwrap_or_else(|| std::path::PathBuf::from("."))
}

/// Pick the C compiler for the chosen codegen backend, as a command word list
/// (so multi-word compilers like `zig cc` work).
///
/// - `$GALD_CC` overrides everything (may contain spaces, e.g. `zig cc`).
/// - `-backend gcc` → `gcc`.
/// - Otherwise: on Windows the default is `zig cc` (a single self-contained
///   toolchain that bundles libc for `windows-gnu`); on other platforms the
///   default is `clang`.
fn select_c_compiler(backend: attrs::Backend) -> Vec<String> {
    if let Ok(cc) = env::var("GALD_CC") {
        let words: Vec<String> = cc.split_whitespace().map(|s| s.to_string()).collect();
        if !words.is_empty() {
            return words;
        }
    }
    match backend {
        attrs::Backend::Gcc => vec!["gcc".to_string()],
        _ => {
            if cfg!(windows) {
                vec!["zig".to_string(), "cc".to_string()]
            } else {
                vec!["clang".to_string()]
            }
        }
    }
}

/// clap Command describing the galdc CLI — used to generate shell completions.
///
/// NOTE: galdc's real CLI uses *single-dash* long flags (`-rewrite-gald`,
/// `-fno-gald-arc`, `-arch`), which clap would normally normalize to
/// `--rewrite-gald` etc. We declare the clap options, then post-process the
/// generated script in `gen_completions` to emit the single-dash forms.
fn clap_command() -> ClapCommand {
    ClapCommand::new("galdc")
        .version(env!("GALD_VERSION"))
        .about("Gald language compiler")
        .disable_help_flag(true)
        .disable_version_flag(true)
        .disable_help_subcommand(true)
        .arg(Arg::new("rewrite-gald").long("rewrite-gald")
            .help("transpile to C only (no link)"))
        .arg(Arg::new("verbose").short('v').long("verbose")
            .help("show verbose transpilation info"))
        .arg(Arg::new("version").long("version")
            .help("print version and exit"))
        .arg(Arg::new("arc").long("fgald-arc")
            .help("enable ARC (default)"))
        .arg(Arg::new("no-arc").long("fno-gald-arc")
            .help("disable ARC (MRC)"))
        .arg(Arg::new("no-checker").long("fno-checker")
            .help("skip type checking"))
        .arg(Arg::new("strong-metadata").long("fstrong-metadata")
            .help("emit class metadata as strong symbols (for building a precompiled library)"))
        .arg(Arg::new("eh").long("eh").value_name("CHECKED|SJLJ")
            .help("exception backend: checked (Swift-scheme, no cross-frame leak) or sjlj (setjmp/longjmp, default; 'legacy' is an alias)"))
        .arg(Arg::new("ffreestanding").long("ffreestanding")
            .help("bare-metal/freestanding output"))
        .arg(Arg::new("nostdinc").long("nostdinc")
            .help("pass -nostdinc to the C compiler (orthogonal; -ffreestanding does NOT imply it)"))
        .arg(Arg::new("no-comments").long("no-comments")
            .help("omit readability comments in generated C (default: on)"))
        .arg(Arg::new("trace-refcount").long("trace-refcount")
            .help("print a static reference-count trace (no codegen)"))
        .arg(Arg::new("trace-max-iters").long("trace-max-iters").value_name("N")
            .help("loop iterations simulated in the refcount trace (default 2)"))
        .arg(Arg::new("trace-no-color").long("trace-no-color")
            .help("disable colors in the refcount trace"))
        .arg(Arg::new("backend").long("backend").value_name("CLANG|PORTABLE|GCC")
            .help("C compiler backend (clang is the default)"))
        .arg(Arg::new("output").short('o').value_name("PATH")
            .help("output path (binary or .c)"))
        .arg(Arg::new("include").short('I').value_name("DIR")
            .help("add include dir").action(clap::ArgAction::Append))
        .arg(Arg::new("lib").short('L').value_name("DIR")
            .help("add lib dir").action(clap::ArgAction::Append))
        .arg(Arg::new("asm").short('S').long("asm").value_name("FILE")
            .help("link a real assembly file (repeatable)").action(clap::ArgAction::Append))
        .arg(Arg::new("arch").long("arch").value_name("TARGET")
            .help("target arch (e.g. -arch x86_64)"))
        .arg(Arg::new("gen-completions").long("gen-completions").value_name("SHELL")
            .help("generate shell completion script (bash|zsh|fish|powershell|elvish)"))
        .arg(Arg::new("input").value_name("INPUT")
            .num_args(1..)
            .help("input .gm file(s): first = main TU; each extra .gm is an \
                   additional TU compiled and linked in (compile mode); .o/.a \
                   are linked as-is"))
        .subcommand(ClapCommand::new("run")
            .about("compile + run, then delete binary")
            .disable_help_flag(true)
            .arg(Arg::new("input").value_name("INPUT").required(true))
            .arg(Arg::new("args").value_name("ARGS").num_args(0..).last(true)))
}

/// Map clap's double-dash normalization back to galdc's single-dash flags.
const DOUBLE_TO_SINGLE: &[(&str, &str)] = &[
    ("--rewrite-gald", "-rewrite-gald"),
    ("--fgald-arc", "-fgald-arc"),
    ("--fno-gald-arc", "-fno-gald-arc"),
    ("--fno-checker", "-fno-checker"),
    ("--fstrong-metadata", "-fstrong-metadata"),
    ("--eh", "-eh"),
    ("--ffreestanding", "-ffreestanding"),
    ("--nostdinc", "-nostdinc"),
    ("--no-comments", "-no-comments"),
    ("--trace-refcount", "-trace-refcount"),
    ("--trace-max-iters", "-trace-max-iters"),
    ("--trace-no-color", "-trace-no-color"),
    ("--backend", "-backend"),
    ("--arch", "-arch"),
    ("--asm", "-asm"),
    ("--output", "-o"),
];

/// -gen-completions <shell>: emit a completion script for the given shell.
fn gen_completions(shell: &str) {
    let shell = match shell {
        "bash" => Shell::Bash,
        "zsh" => Shell::Zsh,
        "fish" => Shell::Fish,
        "powershell" | "pwsh" => Shell::PowerShell,
        "elvish" => Shell::Elvish,
        other => {
            eprintln!("error: unsupported shell '{}' (try bash, zsh, fish, powershell, elvish)", other);
            std::process::exit(1);
        }
    };
    let mut cmd = clap_command();
    let mut buf: Vec<u8> = Vec::new();
    generate(shell, &mut cmd, "galdc", &mut buf);
    let text = String::from_utf8_lossy(&buf).to_string();
    let mut out = text;
    for (from, to) in DOUBLE_TO_SINGLE {
        out = out.replace(from, to);
    }
    // Deduplicate consecutive identical lines (e.g. the `-o` spec left after
    // `--output` → `-o`, which clap emits once for the short and once for long).
    let mut dedup: Vec<&str> = Vec::new();
    for line in out.lines() {
        if dedup.last().map_or(true, |last| *last != line) {
            dedup.push(line);
        }
    }
    println!("{}", dedup.join("\n"));
}

fn find_libgald(custom_libs: &[String]) -> Option<String> {
    if let Ok(exe_path) = std::env::current_exe() {
        if let Some(exe_dir) = exe_path.parent() {
            let lib_path = exe_dir.join("libgald.a");
            if lib_path.exists() {
                return Some(exe_dir.to_string_lossy().to_string());
            }
        }
    }
    let mut paths = vec![
        "builddir".to_string(),
        "/opt/gald/lib".to_string(),
        "/usr/local/lib/gald".to_string(),
    ];
    for p in custom_libs { paths.push(p.clone()); }
    for p in &paths {
        let libpath = format!("{}/libgald.a", p);
        if std::path::Path::new(&libpath).exists() {
            return Some(p.clone());
        }
    }
    None
}

fn compile_to_binary(cc: &[String], c_code: &str, bin_path: &str, include_dirs: &[String], lib_dirs: &[String], libs: &[String], asm_files: &[String], extra_c_files: &[String], extra_objects: &[String], frameworks: &[String], arch: Option<&str>, verbose: bool, no_libc: bool, nostdinc: bool, shared: bool) {
    // Compiler may be multi-word (e.g. `zig cc`): program + leading args.
    let program = cc.first().map(|s| s.as_str()).unwrap_or("clang");
    let cc_extra: Vec<String> = cc.get(1..).unwrap_or(&[]).to_vec();
    if verbose { eprintln!("[galdc] compiling with {}...", cc.join(" ")); }
    let self_dir = resolve_bundle_root();
    let include_root = self_dir.join("include");
    let foundation_include = self_dir.join("include").join("Foundation");
    let runtime_c = self_dir.join("include").join("gald").join("runtime.c");
    if verbose {
        eprintln!("[galdc]   self_dir={:?}", self_dir);
        eprintln!("[galdc]   runtime_c={:?}", runtime_c);
    }
    let mut clang_args = vec![
        "-x".to_string(), "c".to_string(),
        "-".to_string(),
        "-x".to_string(), "none".to_string(),
    ];
    if shared {
        // `-shared` is a gcc/clang shared flag (Linux → .so). Darwin's clang
        // accepts it too, but `-dynamiclib` is the platform convention for a
        // Mach-O .dylib. Pick per-OS so both toolchains stay in their idiom.
        if std::env::consts::OS == "macos" {
            clang_args.push("-dynamiclib".to_string());
        } else {
            clang_args.push("-shared".to_string());
        }
    }
    if no_libc {
        // Bare-metal freestanding flags
        clang_args.push("-ffreestanding".to_string());
        clang_args.push("-fno-builtin".to_string());
        clang_args.push("-fno-stack-protector".to_string());
        clang_args.push("-fno-pic".to_string());
        clang_args.push("-fno-pie".to_string());
        clang_args.push("-fno-asynchronous-unwind-tables".to_string());
        clang_args.push("-nostdlib".to_string());
        if let Some(a) = arch {
            if a.contains("86") || a.contains("x86_64") {
                clang_args.push("-mno-sse".to_string());
                clang_args.push("-mno-mmx".to_string());
                clang_args.push("-mno-red-zone".to_string());
            }
        }
    }
    if nostdinc {
        // Orthogonal include-path control: strip the system include search
        // path only (no other bare-metal flags). Headers must come from the
        // user's -I dirs — note -nostdinc also removes the compiler's builtin
        // freestanding set, so stdint.h/stddef.h/stdbool.h must be supplied.
        clang_args.push("-nostdinc".to_string());
    }
    // Cross-target ARCH selection (e.g. `-arch x86_64` to build via Rosetta).
    if let Some(a) = arch {
        clang_args.push("-arch".to_string());
        clang_args.push(a.to_string());
    }
    // Additional translation units (multi-input mode): plain file inputs on
    // the driver line — the C compiler compiles each TU and links everything
    // in the same invocation (same shape as the bundled runtime.c below).
    for c in extra_c_files {
        clang_args.push(c.clone());
    }
    // Precompiled objects/archives (positional extra inputs): linked as-is.
    for obj in extra_objects {
        clang_args.push(obj.clone());
    }
    // Real assembly (.s) / object (.o) files: assembled/linked alongside.
    for asm_file in asm_files {
        clang_args.push(asm_file.clone());
    }
    // Apple frameworks to link (e.g. `-framework Cocoa`): lets Gald programs
    // link against ObjC bridges (or the runtime) via a thin C API.
    for fw in frameworks {
        clang_args.push("-framework".to_string());
        clang_args.push(fw.clone());
    }
    clang_args.push("-I".to_string());
    clang_args.push(include_root.to_string_lossy().to_string());
    clang_args.push("-I".to_string());
    clang_args.push(foundation_include.to_string_lossy().to_string());
    clang_args.push("-o".to_string());
    clang_args.push(bin_path.to_string());
    for d in include_dirs {
        clang_args.push("-I".to_string());
        clang_args.push(d.clone());
    }
    if no_libc {
        // Freestanding: user provides their own runtime (GALD_CLASS_$_gald_root,
        // exception state, etc.).  Do NOT link the bundled runtime.c (which
        // depends on libc malloc, __thread, etc.).
        if verbose { eprintln!("[galdc]   bare-metal mode: skipping runtime.c"); }
    } else if shared {
        // Shared library: the runtime lives in the host executable — a module
        // must not carry its own copy (duplicate symbols / two metas). It talks
        // to the host through the ModAPI function-pointer table instead.
        if verbose { eprintln!("[galdc]   shared-library mode: skipping runtime.c"); }
    } else {
        // Link a prebuilt static libgald if one is available; otherwise compile
        // the bundled runtime.c source. Never both (duplicate symbols).
        if let Some(lib_path) = find_libgald(lib_dirs) {
            clang_args.push("-L".to_string());
            clang_args.push(lib_path);
            clang_args.push("-lgald".to_string());
        } else {
            clang_args.push(runtime_c.to_string_lossy().to_string());
        }
    }
    // Platform link conveniences (host mode only). Detected from the GENERATED
    // code, so the portable (gcc) backend — which expands blocks away — is
    // automatically exempt. Extra TUs count too: a block literal in any TU
    // needs the blocks runtime at link time.
    let extras_have_blocks = extra_c_files.iter()
        .filter_map(|p| fs::read_to_string(p).ok())
        .any(|s| s.contains("(^"));
    if !no_libc && (c_code.contains("(^") || extras_have_blocks) {
        // Blocks: Apple's clang enables them by default, upstream clang (Linux)
        // does not — and needs the separate BlocksRuntime at link time
        // (apt install libblocksruntime-dev).
        clang_args.push("-fblocks".to_string());
        if std::env::consts::OS == "linux" && !shared {
            clang_args.push("-lBlocksRuntime".to_string());
        }
    }
    if !no_libc && !shared && std::env::consts::OS == "linux" {
        // macOS folds libm into libSystem (sin/cos link implicitly); Linux
        // needs an explicit -lm. The linker drops it when unused (--as-needed).
        clang_args.push("-lm".to_string());
    }
    // User link flags: -L dirs then -l libs, right before the implicit-libs
    // tail — linker resolution order matters (libs must follow the objects,
    // which are streamed via stdin at the end of clang_args).
    for d in lib_dirs {
        clang_args.push("-L".to_string());
        clang_args.push(d.clone());
    }
    for l in libs {
        clang_args.push(format!("-l{}", l));
    }
    clang_args.push("-w".to_string());

    let mut child = match Command::new(program)
        .args(&cc_extra)
        .args(&clang_args)
        .stdin(Stdio::piped())        .spawn()
    {
        Ok(c) => c,
        Err(e) => {
            eprintln!("\x1b[1;31merror:\x1b[0m failed to execute clang: {}", e);
            std::process::exit(1);
        }
    };

    if let Some(mut stdin) = child.stdin.take() {
        if let Err(e) = stdin.write_all(c_code.as_bytes()) {
            eprintln!("\x1b[1;31merror:\x1b[0m failed to write to clang stdin: {}", e);
            std::process::exit(1);
        }
        if verbose { eprintln!("[galdc]   wrote {} bytes to clang", c_code.len()); }
    }
    if verbose { eprintln!("[galdc]   waiting for clang..."); }

    let status = match child.wait() {
        Ok(s) => s,
        Err(e) => {
            eprintln!("\x1b[1;31merror:\x1b[0m clang failed: {}", e);
            std::process::exit(1);
        }
    };

    if verbose { eprintln!("[galdc]   clang finished with status: {}", status); }

    if !status.success() {
        eprintln!("Compilation failed");
        std::process::exit(1);
    }
}

/// All galdc flags, grouped by dash count.
/// Returns (single_dash_flags, double_dash_flags).
fn galdc_flags() -> (Vec<(&'static str, &'static str)>, Vec<(&'static str, &'static str)>) {
    let single = vec![
        ("-Werror",       "promote warnings to errors"),
        ("-emit-bridge-header", "emit a C bridge header for calling Gald from C"),
        ("-rewrite-gald", "transpile to C only (no link)"),
        ("-fgald-arc",    "enable ARC (default)"),
        ("-fno-gald-arc", "disable ARC (MRC)"),
        ("-fno-checker",  "skip type checking"),
        ("-fstrong-metadata", "emit class metadata as strong symbols (for building a precompiled library)"),
        ("-eh",           "exception backend: checked or sjlj (default sjlj; 'legacy' is an alias)"),
        ("-ffreestanding", "bare-metal/freestanding output"),
        ("-nostdinc",     "pass -nostdinc to the C compiler (headers come from your -I dirs)"),
        ("-no-comments",  "omit readability comments in generated C (default: on)"),
        ("-trace-refcount", "print a static reference-count trace (no codegen)"),
        ("-trace-max-iters", "loop iterations in the refcount trace (default 2)"),
        ("-trace-no-color", "disable colors in the refcount trace"),
        ("-backend",      "C compiler backend (clang, portable, gcc)"),
        ("-v",            "verbose transpilation"),
        ("-o",            "output path (binary or .c)"),
        ("-I",            "add include dir"),
        ("-L",            "add lib dir"),
        ("-l",            "link a library (e.g. -l gmp; -I/-L/-l also accept joined form)"),
        ("-asm",          "link a real assembly file (repeatable)"),
        ("-S",            "link a real assembly file (repeatable)"),
        ("-arch",         "target arch (e.g. -arch x86_64)"),
    ];
    let double = vec![
        ("--verbose", "show verbose transpilation info"),
        ("--version", "print version and exit"),
    ];
    (single, double)
}

/// Interactive flag picker: shows all matching flags and lets the user select one.
/// `prefix` is either "-" or "--". Returns the chosen flag, or None on cancel/error.
fn interactive_flag_picker(prefix: &str) -> Option<&'static str> {
    let (single, double) = galdc_flags();
    let (list, label) = if prefix == "--" {
        (double, "double-dash")
    } else {
        (single, "single-dash")
    };

    if list.is_empty() {
        eprintln!("No {} flags available.", label);
        return None;
    }

    println!("=== Gald {} flags ===", label);
    for (i, (flag, desc)) in list.iter().enumerate() {
        println!("  {:>2}) {}  \x1b[2m{}\x1b[0m", i + 1, flag, desc);
    }
    println!("  {}  (cancel)", list.len() + 1);
    print!("Select (1-{}): ", list.len() + 1);
    let _ = std::io::stdout().flush();

    let mut line = String::new();
    if std::io::stdin().read_line(&mut line).is_err() {
        eprintln!("error reading input");
        return None;
    }
    let trimmed = line.trim();

    if trimmed.is_empty() || trimmed == "0" || trimmed == (list.len() + 1).to_string() {
        println!("Cancelled.");
        return None;
    }

    if let Ok(n) = trimmed.parse::<usize>() {
        if n >= 1 && n <= list.len() {
            let (flag, _) = list[n - 1];
            println!("Selected: {}", flag);
            return Some(flag);
        }
    }

    // Allow typing the flag name directly
    if let Some((flag, _)) = list.iter().find(|(f, _)| *f == trimmed) {
        println!("Selected: {}", flag);
        return Some(flag);
    }

    eprintln!("Invalid selection: {}", trimmed);
    None
}

fn norm_flag(s: &str) -> &str {
    if s.starts_with("--") {
        match s {
            "--rewrite-gald" => "-rewrite-gald",
            "--fgald-arc" => "-fgald-arc",
            "--fno-gald-arc" => "-fno-gald-arc",
            "--fno-checker" => "-fno-checker",
            "--eh" => "-eh",
            "--ffreestanding" => "-ffreestanding",
            "--nostdinc" => "-nostdinc",
            "--no-comments" => "-no-comments",
            "--trace-refcount" => "-trace-refcount",
            "--trace-max-iters" => "-trace-max-iters",
            "--trace-no-color" => "-trace-no-color",
            "--Werror" | "--werror" => "-Werror",
            "--emit-bridge-header" => "-emit-bridge-header",
            "--backend" => "-backend",
            "--arch" => "-arch",
            "--asm" => "-asm",
            "--verbose" => "-v",
            "--version" => "-V",
            _ => s,
        }
    } else {
        s
    }
}

/// Split `-flag=value` / `--flag=value` into a normalized flag and its inline
/// value. Without `=`, returns (normalized_flag, None) so callers fall back to
/// consuming the next argument. Currently applied to `-backend=<mode>`.
fn split_flag_value(arg: &str) -> (&str, Option<&str>) {
    match arg.find('=') {
        Some(eq) => {
            let flag = norm_flag(&arg[..eq]);
            (flag, arg.get(eq + 1..))
        }
        None => (norm_flag(arg), None),
    }
}

fn main() {
    let args: Vec<String> = env::args().collect();
    let version = env!("GALD_VERSION");

    // Bare "-" or "--" triggers an interactive flag picker.
    if args.len() > 1 && (args[1] == "-" || args[1] == "--") {
        if let Some(flag) = interactive_flag_picker(&args[1]) {
            // Re-execute with the chosen flag in place of the bare "-"/"--".
            let mut new_args: Vec<String> = Vec::with_capacity(args.len());
            new_args.push(args[0].clone());
            new_args.push(flag.to_string());
            new_args.extend_from_slice(&args[2..]);
            let status = std::process::Command::new(&new_args[0])
                .args(&new_args[1..])
                .status();
            match status {
                Ok(s) => std::process::exit(s.code().unwrap_or(1)),
                Err(e) => {
                    eprintln!("error re-executing galdc: {}", e);
                    std::process::exit(1);
                }
            }
        } else {
            std::process::exit(1);
        }
    }

    // Check for --version / -V first (before any other parsing)
    if args.len() > 1 && (args[1] == "--version" || args[1] == "-V") {
        println!("galdc version {}", version);
        return;
    }

    // -gen-completions <shell> / --gen-completions <shell>: emit a completion script.
    if let Some(idx) = args.iter().position(|a| a == "--gen-completions" || a == "-gen-completions") {
        if let Some(shell) = args.get(idx + 1) {
            gen_completions(shell);
        } else {
            eprintln!("error: -gen-completions requires a shell name (bash|zsh|fish|powershell|elvish)");
            std::process::exit(1);
        }
        return;
    }

    // Hidden --autocomplete mode (mirrors clang): prints matching flags for shell Tab completion.
    // The shell script calls `galdc --autocomplete=<whole-command-so-far>` or
    // `galdc --autocomplete "<whole-command-so-far>"`.
    if let Some(ac_arg) = args.iter().find(|a| a.starts_with("--autocomplete")) {
        // Strip the "--autocomplete[...]" prefix to get the raw command line.
        let raw = ac_arg.trim_start_matches("--autocomplete");
        let joined = match raw.strip_prefix('=') {
            Some(rest) => rest.to_string(),
            None => {
                // Could be `--autocomplete <args>`: collect following args.
                let idx = args.iter().position(|a| a == "--autocomplete");
                match idx {
                    Some(i) => args[i + 1..].join(" "),
                    None => String::new(),
                }
            }
        };
        let (single, double) = galdc_flags();
        let all = single.iter().chain(double.iter());
        // The word being completed is the last token — the shell joins words with
        // commas (like clang), so split on commas and whitespace.
        let cur = joined
            .split(|c: char| c == ',' || c.is_whitespace())
            .filter(|s| !s.is_empty())
            .last()
            .unwrap_or("");
        for (flag, desc) in all {
            if flag.starts_with(cur) && !cur.is_empty() {
                println!("{}\t{}", flag, desc);
            }
        }
        return;
    }

    if args.len() < 2 || args[1] == "-h" || args[1] == "--help" {
        println!("Usage: galdc [command] [options] <input.gm> [more.gm|file.o|lib.a ...]");
        println!();
        println!("Commands:");
        println!("  run                 Compile, run, then delete binary");
        println!();
        println!("Modes (default: compile to binary):");
        println!("  -rewrite-gald       Transpile to C only (no link; single input)");
        println!();
        println!("Multi-TU (compile mode): extra positional .gm files become additional");
        println!("translation units compiled and linked into the same binary; .o/.a files");
        println!("are linked as-is. Example: galdc main.gm lib.gm -I include -o app");
        println!("Transpilation Options:");
        println!("  -o <path>                            Output path (binary or .c)");
        println!("  -I <dir>                             Add include directory");
        println!("  -L <dir>                             Add lib directory");
        println!("  -l <lib>                             Link a library (e.g. -l gmp)");
        println!("                                       (-I/-L/-l also accept joined -I<path> form)");
        println!("  -asm <file.s>                        Link a real assembly file (repeatable)");
        println!("  -arch <target>                       Build for target arch (e.g. -arch x86_64)");
        println!("  -v, --verbose                        Show verbose transpilation info");
        println!("  -V, --version                        Print version and exit");
        println!();
        println!("Runtime Options:");
        println!("  -fgald-arc                           Enable ARC (default)");
        println!("  -fno-gald-arc                        Disable ARC (MRC)");
        println!("  -ffreestanding                       Bare-metal/freestanding output");
        println!("                                     (no libc headers, no TLS, no bundled");
        println!("                                     runtime. The C compiler gets");
        println!("                                     -ffreestanding -fno-builtin ... (NOT");
        println!("                                     -nostdinc; see below).");
        println!("  -nostdinc                            Pass -nostdinc to the C compiler:");
        println!("                                     strip system include paths — all");
        println!("                                     headers must come from -I dirs.");
        println!("  -no-comments                         Omit the readability comments in the");
        println!("                                     generated C code (comments are on by");
        println!("                                     default).");
        println!("  -backend <mode>                      C compiler backend (clang, portable, gcc)");
        println!("                                     Default: clang");
        println!("                                     portable: only gcc+clang common attributes");
        println!("                                     gcc:   allow gcc-specific __attribute__");
        println!("  -fstrong-metadata                    Emit class metadata (vtable /");
        println!("                                     meta-vtable instances, getClass,");
        println!("                                     gald_metaInit) as STRONG symbols");
        println!("                                     instead of __attribute__((weak)).");
        println!("                                     Use when building a precompiled");
        println!("                                     library so its real tables outrank a");
        println!("                                     client TU's declaration-only stubs.");
        println!("  -eh <mode>                           Exception backend:");
        println!("                                     checked (default): flag + guard lowering,");
        println!("                                       unwind-safe ARC, no setjmp/longjmp —");
        println!("                                       works on bare metal");
        println!("                                     legacy (alias: sjlj): old setjmp/longjmp");
        println!("                                       backend, kept as a full rollback");
        println!();
        println!("Type Checking Options:");
        println!("  -fno-checker                         Skip type checking");
        println!();
        println!("Refcount Trace (debug aid):");
        println!("  -trace-refcount                      Print a static reference-count trace");
        println!("                                     of each retained object, in source order.");
        println!("  -trace-max-iters <N>                 Loop iterations in the trace (default 2)");
        println!("  -trace-no-color                      Disable colors in the trace");
        println!();
        println!("Additional help:");
        println!("  -h, --help                           Print this help and exit");
        std::process::exit(0);
    }

    let mut i = 1;
    let mut mode = "compile";
    let mut input = None;
    let mut output = None;
    let mut include_dirs = Vec::new();
    let mut lib_dirs = Vec::new();
    let mut libs = Vec::new(); // -l <name> → clang -l<name>
    let mut asm_files = Vec::new();
    // Multi-TU mode: positional .gm inputs after the first. Each is transpiled
    // in its own galdc pass and linked into the same binary.
    let mut extra_inputs: Vec<String> = Vec::new();
    // Precompiled objects/archives passed as extra positionals: linked as-is.
    let mut extra_objects: Vec<String> = Vec::new();
    let mut frameworks = Vec::new();
    let mut no_arc = false;
    let mut no_checker = false;  // ARC mode by default
    let mut no_libc = false;     // bare-metal / freestanding mode
    let mut nostdinc = false;    // strip system include paths (orthogonal flag)
    let mut no_comments = false; // readability comments in generated C (on by default)
    let mut shared = false;      // dynamic library output (-shared/-dynamiclib)
    let mut werror = false;
    let mut verbose = false;
    let mut program_args = Vec::new();
    let mut arch: Option<String> = None;
    let mut backend: Option<String> = None;
    let mut trace_refcount = false;
    let mut trace_max_iters = 2;
    let mut trace_no_color = false;
    let mut bridge_header: Option<String> = None;
    // DEFAULT is the checked backend (pipeline::DEFAULT_EH_CHECKED). `-eh
    // legacy` / `-eh sjlj` select the old setjmp/longjmp backend for rollback.
    let mut eh_checked = DEFAULT_EH_CHECKED;
    let mut slots_manifest: Option<String> = None; // --slots <file> (stable cross-TU vtable layout)
    let mut strong_metadata = false; // -fstrong-metadata (precompiled-library metadata linkage)

    // Check for "run" subcommand: look for `run` that is not preceded by a flag
    // (i.e. not `-o run` or `-I run`)
    let mut run_pos = None;
    for (pos, arg) in args.iter().enumerate() {
        if arg == "run" && pos > 0 {
            if pos > 1 && args[pos - 1].starts_with('-') && !matches!(args[pos - 1].as_str(), "-v" | "--verbose") {
                continue;
            }
            run_pos = Some(pos);
            break;
        }
    }
    if let Some(pos) = run_pos {
        mode = "run";
        i = pos + 1;
        // Re-process flags before "run"
        for j in 1..pos {
            let (nj, inline_val) = split_flag_value(&args[j]);
            if nj == "-v" {
                verbose = true;
            } else if nj == "-fno-gald-arc" {
                no_arc = true;
            } else if nj == "-fgald-arc" {
                no_arc = false;
            } else if nj == "-fno-checker" {
                no_checker = true;
            } else if nj == "-fstrong-metadata" {
                strong_metadata = true;
            } else if nj == "-ffreestanding" {
                no_libc = true;
            } else if nj == "-nostdinc" {
                nostdinc = true;
            } else if nj == "-no-comments" {
                no_comments = true;
            } else if nj == "-Werror" {
                werror = true;
            } else if nj == "-arch" && j + 1 < pos {
                arch = Some(args[j + 1].clone());
            } else if nj == "-backend" && j + 1 < pos {
                backend = Some(args[j + 1].clone());
            } else if nj == "-backend" && inline_val.is_some() {
                backend = inline_val.map(|s| s.to_string());
            } else if nj == "-trace-refcount" {
                trace_refcount = true;
            } else if nj == "-trace-max-iters" && j + 1 < pos {
                trace_max_iters = args[j + 1].parse().unwrap_or(2);
            } else if nj == "-trace-no-color" {
                trace_no_color = true;
            } else if nj == "-emit-bridge-header" && j + 1 < pos {
                bridge_header = Some(args[j + 1].clone());
            } else if nj == "-eh" {
                let v = inline_val.map(|s| s.to_string())
                    .or_else(|| if j + 1 < pos { Some(args[j + 1].clone()) } else { None })
                    .unwrap_or_default();
                match v.as_str() {
                    "checked" => eh_checked = true,
                    // 'legacy' is the sjlj backend's official alias: the escape
                    // hatch for the checked-by-default flip. Explicitly sets the
                    // flag to false (the default is now true).
                    "sjlj" | "legacy" | "" => eh_checked = false,
                    other => {
                        eprintln!("error: -eh expects 'checked', 'sjlj' or 'legacy' (got '{}')", other);
                        std::process::exit(1);
                    }
                }
            } else if nj == "--slots" {
                slots_manifest = inline_val.map(|s| s.to_string())
                    .or_else(|| if j + 1 < pos { Some(args[j + 1].clone()) } else { None });
                if slots_manifest.is_none() {
                    eprintln!("error: --slots expects a manifest file path");
                    std::process::exit(1);
                }
            }
        }
    }

    while i < args.len() {
        let (normalized, inline_val) = split_flag_value(&args[i]);
        if normalized == "-rewrite-gald" {
            mode = "rewrite";
            i += 1;
        } else if normalized == "-fno-gald-arc" {
            no_arc = true;
            i += 1;
        } else if normalized == "-fgald-arc" {
            no_arc = false;
            i += 1;
        } else if normalized == "-fno-checker" {
            no_checker = true;
            i += 1;
        } else if normalized == "-fstrong-metadata" {
            strong_metadata = true;
            i += 1;
        } else if normalized == "-ffreestanding" {
            no_libc = true;
            i += 1;
        } else if normalized == "-nostdinc" {
            nostdinc = true;
            i += 1;
        } else if normalized == "-no-comments" {
            no_comments = true;
            i += 1;
        } else if normalized == "-shared" {
            shared = true;
            i += 1;
        } else if normalized == "-Werror" {
            werror = true;
            i += 1;
        } else if normalized == "-trace-refcount" {
            trace_refcount = true;
            i += 1;
        } else if normalized == "-trace-max-iters" && i + 1 < args.len() {
            trace_max_iters = args[i + 1].parse().unwrap_or(2);
            i += 2;
        } else if normalized == "-trace-no-color" {
            trace_no_color = true;
            i += 1;
        } else if normalized == "-eh" {
            let (val, adv) = if let Some(iv) = inline_val {
                (Some(iv.to_string()), 1)
            } else if i + 1 < args.len() {
                (Some(args[i + 1].clone()), 2)
            } else {
                (None, 1)
            };
            match val.as_deref() {
                Some("checked") => { eh_checked = true; i += adv; }
                // 'legacy' = sjlj alias — the rollback for the checked default.
                Some("sjlj") | Some("legacy") => { eh_checked = false; i += adv; }
                other => {
                    eprintln!("error: -eh expects 'checked', 'sjlj' or 'legacy' (got '{}')",
                        other.unwrap_or("(missing)"));
                    std::process::exit(1);
                }
            }
        } else if normalized == "--slots" {
            let (val, adv) = if let Some(iv) = inline_val {
                (Some(iv.to_string()), 1)
            } else if i + 1 < args.len() {
                (Some(args[i + 1].clone()), 2)
            } else {
                (None, 1)
            };
            match val {
                Some(v) => { slots_manifest = Some(v); i += adv; }
                None => {
                    eprintln!("error: --slots expects a manifest file path");
                    std::process::exit(1);
                }
            }
        } else if normalized == "-arch" && i + 1 < args.len() {
            arch = Some(args[i + 1].clone());
            i += 2;
        } else if normalized == "-backend" {
            let (val, adv) = if let Some(iv) = inline_val {
                (Some(iv.to_string()), 1)
            } else if i + 1 < args.len() {
                (Some(args[i + 1].clone()), 2)
            } else {
                (None, 1)
            };
            match val {
                Some(b) => { backend = Some(b); i += adv; }
                None => {
                    eprintln!("error: -backend requires a mode (clang, portable, or gcc)");
                    std::process::exit(1);
                }
            }
        } else if normalized == "-v" {
            verbose = true;
            i += 1;
        } else if normalized == "-o" && i + 1 < args.len() {
            output = Some(args[i + 1].clone());
            i += 2;
        } else if normalized == "-I" && i + 1 < args.len() {
            include_dirs.push(args[i + 1].clone());
            i += 2;
        } else if normalized.starts_with("-I") && normalized.len() > 2 {
            // Joined form `-I/path` (clang/GCC convention)
            include_dirs.push(normalized[2..].to_string());
            i += 1;
        } else if normalized == "-l" && i + 1 < args.len() {
            libs.push(args[i + 1].clone());
            i += 2;
        } else if normalized.starts_with("-l") && normalized.len() > 2
            && normalized[2..].chars().next().map_or(false, |c| c.is_ascii_alphanumeric())
        {
            // Joined form `-lgmp`. Must come AFTER `-lgmp`-shaped unknown-flag
            // handling would reject it — accepted here so link libraries follow
            // the usual clang spelling. (Plain identifiers only: `-last`-style
            // ambiguity is the user's to resolve with the spaced form.)
            libs.push(normalized[2..].to_string());
            i += 1;
        } else if (normalized == "-asm" || normalized == "-S") && i + 1 < args.len() {
            asm_files.push(args[i + 1].clone());
            i += 2;
        } else if normalized == "-framework" && i + 1 < args.len() {
            frameworks.push(args[i + 1].clone());
            i += 2;
        } else if normalized == "-L" && i + 1 < args.len() {
            lib_dirs.push(args[i + 1].clone());
            i += 2;
        } else if normalized.starts_with("-L") && normalized.len() > 2 {
            // Joined form `-L/path` (clang/GCC convention)
            lib_dirs.push(normalized[2..].to_string());
            i += 1;
        } else if normalized == "-emit-bridge-header" && i + 1 < args.len() {
            bridge_header = Some(args[i + 1].clone());
            i += 2;
        } else if input.is_none() {
            input = Some(args[i].clone());
            i += 1;
        } else if mode == "compile"
            && (args[i].ends_with(".gm") || args[i].ends_with(".o") || args[i].ends_with(".a"))
        {
            // Additional input (multi-input mode). .gm → an extra translation
            // unit (transpiled in its own galdc pass); .o/.a → linked as-is.
            // Compile mode only: in run mode positionals are program arguments,
            // and -rewrite-gald emits one .c per invocation.
            if args[i].ends_with(".gm") {
                extra_inputs.push(args[i].clone());
            } else {
                extra_objects.push(args[i].clone());
            }
            i += 1;
        } else if mode == "rewrite" && args[i].ends_with(".gm") {
            eprintln!("error: multiple inputs are supported in compile mode only — run one -rewrite-gald per file");
            std::process::exit(1);
        } else if mode == "run" {
            program_args.push(args[i].clone());
            i += 1;
        } else {
            eprintln!("Unknown argument: {}", args[i]);
            std::process::exit(1);
        }
    }

    let input_path = input.unwrap_or_else(|| {
        eprintln!("No input file specified");
        std::process::exit(1);
    });

    if trace_refcount && !extra_inputs.is_empty() {
        eprintln!("error: -trace-refcount traces one translation unit; drop the extra inputs");
        std::process::exit(1);
    }

    let source = match fs::read_to_string(&input_path) {
        Ok(s) => s,
        Err(e) => {
            eprintln!("\x1b[1;31merror:\x1b[0m cannot read {}: {}", input_path, e);
            std::process::exit(1);
        }
    };

    let self_dir = resolve_bundle_root();
    let include_root = self_dir.join("include");
    let foundation_include = self_dir.join("include").join("Foundation");

    let mut pipeline = Pipeline::new();
    pipeline.search_dirs.clear();
    pipeline.search_dirs.push(include_root.to_string_lossy().to_string());
    pipeline.search_dirs.push(foundation_include.to_string_lossy().to_string());
    pipeline.search_dirs.push(".".to_string());
    pipeline.search_dirs.extend(include_dirs.clone());
    pipeline.no_arc = no_arc;
    pipeline.no_checker = no_checker;
    pipeline.no_libc = no_libc;
    pipeline.nostdinc = nostdinc;
    pipeline.no_comments = no_comments;
    pipeline.werror = werror;
    pipeline.bridge_header = bridge_header;
    pipeline.verbose = verbose;
    pipeline.trace_refcount = trace_refcount;
    pipeline.trace_max_iters = trace_max_iters;
    pipeline.trace_color = !trace_no_color;
    pipeline.eh_checked = eh_checked;
    pipeline.slots_manifest = slots_manifest.clone();
    pipeline.strong_metadata = strong_metadata;
    if let Some(ref b) = backend {
        match attrs::Backend::parse(b) {
            Some(be) => pipeline.backend = be,
            None => {
                eprintln!("error: unknown backend '{}' (try clang, portable, or gcc)", b);
                std::process::exit(1);
            }
        }
    }

    // C compiler for the link step: default `clang`; `-backend gcc` → `gcc`;
    // `$GALD_CC` overrides. (Windows default is also clang.)
    let cc = select_c_compiler(pipeline.backend);
    // The C type-name probe (see `ctype_probe`) runs the same compiler's
    // preprocessor, so it needs the same program and target arch.
    pipeline.c_cc = cc.clone();
    pipeline.c_arch = arch.clone();
    // Escape hatch, same spirit as `$GALD_CC`: skip the probe entirely (no
    // compiler is spawned, no header is read). The parser then falls back to
    // the builtin type list and its shape heuristics.
    pipeline.no_ctype_probe = std::env::var_os("GALD_NO_CTYPE_PROBE").is_some();

    let c_code = match pipeline.transpile(&source, &input_path) {
        Ok(code) => code,
        Err(e) => {
            eprintln!("\x1b[1;31merror:\x1b[0m {}", e);
            std::process::exit(1);
        }
    };

    if trace_refcount {
        println!("{}", c_code);
        return;
    }

    // Multi-TU mode: transpile each extra .gm in its own galdc subprocess.
    // One process per TU is required: codegen's method-metadata tables are
    // process-global (OnceLock), so a second in-process transpile is not
    // possible. Transpile-affecting flags are forwarded; link-only flags
    // (-L/-l/-asm/-framework) are not needed by the transpile step.
    // -fstrong-metadata is deliberately NOT forwarded: it upgrades every
    // metadata symbol in a TU to strong, so two TUs would both emit strong
    // definitions for shared (non-owned) classes and the link would fail
    // with duplicate symbols. In multi-input mode the first TU's strong
    // symbols already win over the extras' weak ones.
    let mut extra_c_files: Vec<String> = Vec::new();
    if !extra_inputs.is_empty() {
        let exe = std::env::current_exe().unwrap_or_else(|e| {
            eprintln!("error: cannot locate galdc for multi-TU transpile: {}", e);
            std::process::exit(1);
        });
        if strong_metadata {
            println!("note: -fstrong-metadata applies to the first TU only (extras stay weak so shared metadata coalesces)");
        }
        for (n, tu) in extra_inputs.iter().enumerate() {
            let stem = Path::new(tu).file_stem().and_then(|s| s.to_str()).unwrap_or("tu");
            let mut tmp = std::env::temp_dir();
            tmp.push(format!("{}-{}-galdc-{}.c", stem, std::process::id(), n));
            let mut cmd: Vec<String> = vec![
                exe.to_string_lossy().to_string(),
                "-rewrite-gald".into(), tu.clone(),
                "-o".into(), tmp.to_string_lossy().to_string(),
            ];
            for d in &include_dirs {
                cmd.push("-I".into());
                cmd.push(d.clone());
            }
            if no_arc { cmd.push("-fno-gald-arc".into()); }
            if no_checker { cmd.push("-fno-checker".into()); }
            if werror { cmd.push("-Werror".into()); }
            cmd.push("-eh".into());
            cmd.push(if eh_checked { "checked".into() } else { "legacy".into() });
            if let Some(slots) = &slots_manifest {
                cmd.push("--slots".into());
                cmd.push(slots.clone());
            }
            if let Some(b) = &backend {
                cmd.push("-backend".into());
                cmd.push(b.clone());
            }
            if let Some(a) = &arch {
                cmd.push("-arch".into());
                cmd.push(a.clone());
            }
            if no_libc { cmd.push("-ffreestanding".into()); }
            if nostdinc { cmd.push("-nostdinc".into()); }
            if no_comments { cmd.push("-no-comments".into()); }
            if verbose { cmd.push("-v".into()); }
            if verbose {
                println!("[galdc] transpiling extra TU: {}", tu);
            }
            let status = Command::new(&cmd[0]).args(&cmd[1..]).status()
                .unwrap_or_else(|e| {
                    eprintln!("error: failed to spawn galdc for {}: {}", tu, e);
                    std::process::exit(1);
                });
            if !status.success() {
                eprintln!("error: transpiling extra TU {} failed", tu);
                std::process::exit(1);
            }
            extra_c_files.push(tmp.to_string_lossy().to_string());
        }
    }

    match mode {
        "rewrite" => {
            // -rewrite-gald: output C code to file
            let output_path = output.unwrap_or_else(|| {
                if input_path.ends_with(".gm") {
                    input_path[..input_path.len()-3].to_string() + ".c"
                } else {
                    input_path.clone() + ".c"
                }
            });
            if let Some(parent) = Path::new(&output_path).parent() {
                if !parent.as_os_str().is_empty() {
                    let _ = fs::create_dir_all(parent);
                }
            }
            match fs::write(&output_path, &c_code) {
                Ok(_) => {}
                Err(e) => {
                    eprintln!("\x1b[1;31merror:\x1b[0m cannot write {}: {}", output_path, e);
                    std::process::exit(1);
                }
            }
        }
        "run" => {
            // run mode: compile, run, then kill + delete binary
            let input_stem = Path::new(&input_path).file_stem()
                .and_then(|s| s.to_str())
                .unwrap_or("a.out");
            let bin_path = output.unwrap_or_else(|| {
                let mut p = std::env::temp_dir();
                p.push(format!("{}{}", input_stem, std::env::consts::EXE_SUFFIX));
                p.to_string_lossy().to_string()
            });

            compile_to_binary(&cc, &c_code, &bin_path, &include_dirs, &lib_dirs, &libs, &asm_files, &extra_c_files, &extra_objects, &frameworks, arch.as_deref(), verbose, no_libc, nostdinc, false);

            for t in &extra_c_files {
                let _ = fs::remove_file(t);
            }

            let run_status = Command::new(&bin_path)
                .args(&program_args)
                .status()
                .expect("failed to execute binary");

            let _ = fs::remove_file(&bin_path);

            std::process::exit(run_status.code().unwrap_or(1));
        }
        _ => {
            // compile mode: compile to binary, keep it
            let bin_path = output.unwrap_or_else(|| format!("a.out{}", std::env::consts::EXE_SUFFIX));

            if bin_path.ends_with(".c") {
                eprintln!("error: use -rewrite-gald to output C code");
                std::process::exit(1);
            }
            let shared = shared
                || bin_path.ends_with(".dylib")
                || bin_path.ends_with(".so")
                || bin_path.ends_with(".dll");

            compile_to_binary(&cc, &c_code, &bin_path, &include_dirs, &lib_dirs, &libs, &asm_files, &extra_c_files, &extra_objects, &frameworks, arch.as_deref(), verbose, no_libc, nostdinc, shared);

            for t in &extra_c_files {
                let _ = fs::remove_file(t);
            }
        }
    }
}