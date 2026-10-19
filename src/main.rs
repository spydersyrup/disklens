pub mod models;
pub mod utils;

use std::env;
use std::ffi::{c_char, c_int, CString};

extern "C" {
    fn dd_native_app_main(argc: c_int, argv: *mut *mut c_char) -> c_int;
}

fn main() {
    let args: Vec<String> = env::args().collect();
    let c_strings: Vec<CString> = args
        .iter()
        .map(|s| CString::new(s.as_str()).unwrap_or_else(|_| CString::new("").unwrap()))
        .collect();
    let mut c_ptrs: Vec<*mut c_char> = c_strings
        .iter()
        .map(|cs| cs.as_ptr() as *mut c_char)
        .collect();
    c_ptrs.push(std::ptr::null_mut());

    let rc = unsafe { dd_native_app_main((c_ptrs.len() - 1) as c_int, c_ptrs.as_mut_ptr()) };
    std::process::exit(rc);
}

#[cfg(test)]
mod tests {
    use crate::models::FileCategory;
    use crate::utils::{
        abbreviate_label, format_bytes, format_duration, format_percent, sha256_file_bounded,
        sha256_hex, truncate_middle,
    };
    use std::path::Path;

    #[test]
    fn test_formatters_and_categories() {
        assert_eq!(format_bytes(999), "999 B");
        assert_eq!(format_bytes(1000), "1.00 KB");
        assert_eq!(format_bytes(12_345_678), "12.3 MB");
        assert_eq!(format_percent(0.0005), "<0.1%");
        assert_eq!(format_duration(0.05), "0.05s");
        assert_eq!(abbreviate_label("DerivedData"), "DD");
        assert_eq!(abbreviate_label("Application Support"), "AS");
        assert_eq!(truncate_middle("abcdefghijkl", 8), "ab...jkl");
        assert_eq!(
            FileCategory::classify_bytes(b"archive.tar.gz", false),
            FileCategory::Archive
        );
        assert_eq!(
            FileCategory::classify_bytes(b"cache.sqlite-wal", false),
            FileCategory::System
        );
        assert_eq!(format_bytes(-12_345_678), "-12.3 MB");
        let digest =
            sha256_file_bounded(Path::new("Cargo.toml"), 65536, true).expect("hash Cargo.toml");
        assert_eq!(sha256_hex(&digest).len(), 64);
        let digest_full =
            sha256_file_bounded(Path::new("Cargo.toml"), 0, true).expect("hash Cargo.toml full");
        assert_eq!(sha256_hex(&digest_full).len(), 64);
    }
}
