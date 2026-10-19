use std::env;
use std::path::PathBuf;
use std::process::Command;

fn main() {
    let out_dir = PathBuf::from(env::var("OUT_DIR").expect("OUT_DIR not set"));
    let target = env::var("TARGET").unwrap_or_default();
    let is_windows = target.contains("windows");

    let cc = if is_windows {
        "x86_64-w64-mingw32-gcc"
    } else {
        "gcc"
    };
    let ar = if is_windows {
        "x86_64-w64-mingw32-ar"
    } else {
        "ar"
    };

    let sources = [
        "Sources/DiskDude/DiskDudeApp.c",
        "Sources/DiskDude/Theme/Theme.c",
        "Sources/DiskDude/Utils/Utils.c",
        "Sources/DiskDude/Utils/Canvas.c",
        "Sources/DiskDude/Models/FileNode.c",
        "Sources/DiskDude/Scanning/DiskScanner.c",
        "Sources/DiskDude/Views/Views.c",
    ];

    let mut objects = Vec::new();
    for (idx, src) in sources.iter().enumerate() {
        println!("cargo:rerun-if-changed={}", src);
        let obj = out_dir.join(format!("dd_obj_{}.o", idx));
        let status = Command::new(cc)
            .args([
                "-std=c11",
                "-O2",
                "-Wall",
                "-Wextra",
                "-Wno-unused-parameter",
                "-Wno-format-truncation",
                "-DDISKDUDE_RUST_HOST=1",
                "-c",
                src,
                "-o",
            ])
            .arg(&obj)
            .status()
            .expect("Failed to invoke C compiler for platform layer");
        assert!(status.success(), "Failed to compile {}", src);
        objects.push(obj);
    }

    let lib_path = out_dir.join("libdiskdude_platform.a");
    let mut ar_cmd = Command::new(ar);
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
    } else {
        for lib in ["X11", "pthread", "dl", "m"] {
            println!("cargo:rustc-link-lib=dylib={}", lib);
        }
    }
}
