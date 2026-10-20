use std::env;
use std::path::PathBuf;
use std::process::Command;

fn main() {
    let out_dir = PathBuf::from(env::var("OUT_DIR").expect("OUT_DIR not set"));
    let target = env::var("TARGET").unwrap_or_default();
    let is_windows = target.contains("windows");

    let cc = env::var("CC").unwrap_or_else(|_| {
        if is_windows {
            if Command::new("x86_64-w64-mingw32-gcc").arg("--version").output().is_ok() {
                "x86_64-w64-mingw32-gcc".to_string()
            } else if Command::new("gcc").arg("--version").output().is_ok() {
                "gcc".to_string()
            } else if PathBuf::from(r"C:\Qt\Tools\mingw1310_64\bin\gcc.exe").exists() {
                r"C:\Qt\Tools\mingw1310_64\bin\gcc.exe".to_string()
            } else {
                "x86_64-w64-mingw32-gcc".to_string()
            }
        } else {
            "gcc".to_string()
        }
    });

    let ar = env::var("AR").unwrap_or_else(|_| {
        if is_windows {
            if Command::new("x86_64-w64-mingw32-ar").arg("--version").output().is_ok() {
                "x86_64-w64-mingw32-ar".to_string()
            } else if Command::new("ar").arg("--version").output().is_ok() {
                "ar".to_string()
            } else if PathBuf::from(r"C:\Qt\Tools\mingw1310_64\bin\ar.exe").exists() {
                r"C:\Qt\Tools\mingw1310_64\bin\ar.exe".to_string()
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

    let mut c_args = vec![
        "-std=c11",
        "-O2",
        "-Wall",
        "-Wextra",
        "-Wno-unused-parameter",
        "-Wno-format-truncation",
        "-DDISKDUDE_RUST_HOST=1",
    ];
    if is_windows && target.contains("msvc") {
        c_args.push("-D__USE_MINGW_ANSI_STDIO=0");
        c_args.push("-mno-stack-arg-probe");
        c_args.push("-fno-builtin-sin");
        c_args.push("-fno-builtin-cos");
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
            if PathBuf::from(r"C:\Qt\Tools\mingw1310_64\x86_64-w64-mingw32\lib").exists() {
                println!(r"cargo:rustc-link-search=native=C:\Qt\Tools\mingw1310_64\x86_64-w64-mingw32\lib");
                println!(r"cargo:rustc-link-search=native=C:\Qt\Tools\mingw1310_64\lib\gcc\x86_64-w64-mingw32\13.1.0");
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
