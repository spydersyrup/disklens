use std::env;
use std::path::PathBuf;
use std::process::Command;

fn main() {
    let out_dir = PathBuf::from(env::var("OUT_DIR").expect("OUT_DIR not set"));
    let target = env::var("TARGET").unwrap_or_default();
    let host = env::var("HOST").unwrap_or_default();
    let is_windows = target.contains("windows");
    let is_windows_host = host.contains("windows");

    let cc = env::var("CC").unwrap_or_else(|_| {
        if is_windows {
            if is_windows_host {
                if Command::new("gcc").arg("--version").output().is_ok() {
                    "gcc".to_string()
                } else if Command::new("x86_64-w64-mingw32-gcc").arg("--version").output().is_ok() {
                    "x86_64-w64-mingw32-gcc".to_string()
                } else {
                    "gcc".to_string()
                }
            } else {
                "x86_64-w64-mingw32-gcc".to_string()
            }
        } else {
            "gcc".to_string()
        }
    });

    let ar = env::var("AR").unwrap_or_else(|_| {
        if is_windows {
            if is_windows_host {
                if Command::new("ar").arg("--version").output().is_ok() {
                    "ar".to_string()
                } else if Command::new("x86_64-w64-mingw32-ar").arg("--version").output().is_ok() {
                    "x86_64-w64-mingw32-ar".to_string()
                } else {
                    "ar".to_string()
                }
            } else {
                "x86_64-w64-mingw32-ar".to_string()
            }
        } else {
            "ar".to_string()
        }
    });

    let sources = [
        "Sources/DiskDude/DiskDudeApp.c",
        "Sources/DiskDude/Theme/Theme.c",
        "Sources/DiskDude/Utils/Utils.c",
        "Sources/DiskDude/Utils/Canvas.c",
        "Sources/DiskDude/Models/FileNode.c",
        "Sources/DiskDude/Scanning/DiskScanner.c",
        "Sources/DiskDude/Views/Views.c",
    ];

    let mut c_args: Vec<String> = vec![
        "-std=c11".to_string(),
        "-O2".to_string(),
        "-Wall".to_string(),
        "-Wextra".to_string(),
        "-Wno-unused-parameter".to_string(),
        "-Wno-format-truncation".to_string(),
        "-DDISKDUDE_RUST_HOST=1".to_string(),
    ];
    if let Ok(extra_cflags) = env::var("CFLAGS") {
        for flag in extra_cflags.split_whitespace() {
            c_args.push(flag.to_string());
        }
    }
    if is_windows && target.contains("msvc") {
        c_args.push("-D__USE_MINGW_ANSI_STDIO=0".to_string());
        c_args.push("-mno-stack-arg-probe".to_string());
        c_args.push("-fno-builtin-sin".to_string());
        c_args.push("-fno-builtin-cos".to_string());
    }

    let mut objects = Vec::new();
    for (idx, src) in sources.iter().enumerate() {
        println!("cargo:rerun-if-changed={}", src);
        let obj = out_dir.join(format!("dd_obj_{}.o", idx));
        let mut cmd = Command::new(&cc);
        cmd.args(&c_args)
            .args(["-c", src, "-o"])
            .arg(&obj);
        let status = cmd.status().expect("Failed to invoke C compiler for platform layer");
        assert!(status.success(), "Failed to compile {}", src);
        objects.push(obj);
    }

    let lib_path = out_dir.join("libdiskdude_platform.a");
    let mut ar_cmd = Command::new(&ar);
    ar_cmd.arg("rcs").arg(&lib_path);
    for obj in &objects {
        ar_cmd.arg(obj);
    }
    let ar_status = ar_cmd.status().expect("Failed to invoke ar");
    assert!(ar_status.success(), "Failed to archive libdiskdude_platform.a");

    println!("cargo:rustc-link-search=native={}", out_dir.display());
    println!("cargo:rustc-link-lib=static=diskdude_platform");

    if is_windows {
        for lib in ["gdi32", "user32", "shell32", "ole32", "uuid", "comdlg32", "psapi", "advapi32"] {
            println!("cargo:rustc-link-lib=dylib={}", lib);
        }
        if target.contains("msvc") {
            if let Ok(mingw_lib) = env::var("MINGW_LIB_DIR") {
                println!("cargo:rustc-link-search=native={}", mingw_lib);
                println!("cargo:rustc-link-lib=static=mingwex");
                println!("cargo:rustc-link-lib=static=gcc");
            }
        }
    } else {
        for lib in ["X11", "pthread", "dl", "m"] {
            println!("cargo:rustc-link-lib=dylib={}", lib);
        }
    }
}
