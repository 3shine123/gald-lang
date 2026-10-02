use std::path::{Path, PathBuf};
use std::fs;

fn copy_dir_all(src: &Path, dst: &Path) -> std::io::Result<()> {
    fs::create_dir_all(dst)?;
    for entry in fs::read_dir(src)? {
        let entry = entry?;
        let ty = entry.file_type()?;
        let to = dst.join(entry.file_name());
        if ty.is_dir() {
            copy_dir_all(&entry.path(), &to)?;
        } else {
            fs::copy(entry.path(), &to)?;
        }
    }
    Ok(())
}

/// Emit `cargo:rerun-if-changed` for every file under `dir` (recursively), so
/// that editing ANY header re-runs this build script and refreshes the copied
/// `target/<profile>/include/` tree. A directory-level directive alone does not
/// catch content edits to existing files.
fn emit_rerun_for_dir(dir: &Path) {
    if let Ok(entries) = fs::read_dir(dir) {
        for entry in entries.flatten() {
            let p = entry.path();
            if p.is_dir() {
                emit_rerun_for_dir(&p);
            } else {
                println!("cargo:rerun-if-changed={}", p.display());
            }
        }
    }
}

fn main() {
    // Read version from VERSION file
    let version_path = if std::path::Path::new("../../VERSION").exists() {
        "../../VERSION".to_string()
    } else {
        "../VERSION".to_string()
    };
    println!("cargo:rerun-if-changed={}", version_path);
    let version = std::fs::read_to_string(&version_path)
        .unwrap_or_else(|_| "0.0.0".to_string())
        .trim()
        .to_string();
    println!("cargo:rustc-env=NUPA_VERSION={}", version);

    let src = if std::path::Path::new("../../include/nupa/runtime.c").exists() {
        "../../include/nupa/runtime.c"
    } else {
        "../include/nupa/runtime.c"
    };
    println!("cargo:rerun-if-changed={}", src);

    cc::Build::new()
        .file(src)
        .include("../../include")
        .include("../../include/Foundation")
        .compile("nupa");

    // Copy libnupa.a to the architecture-specific output directory (same as nupac binary).
    let built = std::path::Path::new(&std::env::var("OUT_DIR").unwrap()).join("libnupa.a");
    if !built.exists() {
        return;
    }
    let profile = std::env::var("PROFILE").unwrap_or_else(|_| "debug".to_string());
    let target = std::env::var("TARGET").unwrap_or_default();
    let host = std::env::var("HOST").unwrap_or_default();
    // 宿主构建 → target/<profile>/ ；交叉编译 → target/<triple>/<profile>/
    let target_dir = if target.is_empty() || target == host {
        std::path::Path::new("../../target").join(&profile)
    } else {
        std::path::Path::new("../../target").join(&target).join(&profile)
    };
    let _ = std::fs::create_dir_all(&target_dir);
    let _ = std::fs::copy(&built, target_dir.join("libnupa.a"));

    // 拷贝 install.sh + install.ps1 + 头文件到构建输出目录（与 nupac 同层，方便本地安装）
    let install_src = if std::path::Path::new("../../install.sh").exists() {
        PathBuf::from("../../install.sh")
    } else {
        PathBuf::from("../install.sh")
    };
    let install_ps1_src = if std::path::Path::new("../../install.ps1").exists() {
        PathBuf::from("../../install.ps1")
    } else {
        PathBuf::from("../install.ps1")
    };
    let include_src = if std::path::Path::new("../../include").is_dir() {
        PathBuf::from("../../include")
    } else {
        PathBuf::from("../include")
    };
    let completions_src = if std::path::Path::new("../../completions").is_dir() {
        PathBuf::from("../../completions")
    } else {
        PathBuf::from("../completions")
    };
    if install_src.exists() && include_src.exists() {
        let _ = fs::create_dir_all(&target_dir);
        let _ = fs::copy(&install_src, target_dir.join("install.sh"));
        if install_ps1_src.exists() {
            let _ = fs::copy(&install_ps1_src, target_dir.join("install.ps1"));
        }
        let _ = copy_dir_all(&include_src, &target_dir.join("include"));
        if completions_src.is_dir() {
            let _ = copy_dir_all(&completions_src, &target_dir.join("completions"));
        }
        #[cfg(unix)]
        {
            use std::os::unix::fs::PermissionsExt;
            let _ = fs::set_permissions(target_dir.join("install.sh"), fs::Permissions::from_mode(0o755));
        }
    }
    println!("cargo:rerun-if-changed={}", install_src.to_string_lossy());
    println!("cargo:rerun-if-changed={}", install_ps1_src.to_string_lossy());
    emit_rerun_for_dir(&include_src);
    if completions_src.is_dir() {
        emit_rerun_for_dir(&completions_src);
    }
    println!("cargo:rerun-if-changed=../../include/nupa/runtime.h");
    println!("cargo:rerun-if-changed=../../include/nupa/runtime_freestanding.c");
    println!("cargo:rerun-if-changed=../../completions/_nupac");
}