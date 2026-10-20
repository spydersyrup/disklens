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
    use std::fs::File;
    use std::io::Write;

    #[test]
    fn test_formatters_and_categories() {
        assert_eq!(format_bytes(0), "0 B");
        assert_eq!(format_bytes(999), "999 B");
        assert_eq!(format_bytes(1000), "1.00 KB");
        assert_eq!(format_bytes(12_345_678), "12.3 MB");
        assert_eq!(format_bytes(-12_345_678), "-12.3 MB");
        // Check i64::MIN does not overflow or panic
        let min_s = format_bytes(i64::MIN);
        assert!(min_s.starts_with("-"), "format_bytes(i64::MIN) must be negative");

        assert_eq!(format_percent(0.0), "0%");
        assert_eq!(format_percent(0.0005), "<0.1%");
        assert_eq!(format_percent(0.5), "50.0%");
        assert_eq!(format_percent(0.9999), "100%");
        assert_eq!(format_percent(1.0), "100%");

        assert_eq!(format_duration(0.05), "0.05s");
        assert_eq!(format_duration(1.5), "1.5s");

        assert_eq!(abbreviate_label("DerivedData"), "DD");
        assert_eq!(abbreviate_label("Application Support"), "AS");
        assert_eq!(truncate_middle("abcdefghijkl", 8), "ab...jkl");
        assert_eq!(truncate_middle("short", 8), "short");

        assert_eq!(
            FileCategory::classify_bytes(b"video.mp4", false),
            FileCategory::Video
        );
        assert_eq!(
            FileCategory::classify_bytes(b"song.mp3", false),
            FileCategory::Audio
        );
        assert_eq!(
            FileCategory::classify_bytes(b"picture.png", false),
            FileCategory::Image
        );
        assert_eq!(
            FileCategory::classify_bytes(b"doc.pdf", false),
            FileCategory::Document
        );
        assert_eq!(
            FileCategory::classify_bytes(b"main.rs", false),
            FileCategory::Developer
        );
        assert_eq!(
            FileCategory::classify_bytes(b"archive.tar.gz", false),
            FileCategory::Archive
        );
        assert_eq!(
            FileCategory::classify_bytes(b"cache.sqlite-wal", false),
            FileCategory::System
        );
        assert_eq!(
            FileCategory::classify_bytes(b"anyfile", true),
            FileCategory::Apps
        );
    }

    #[test]
    fn test_duplicate_hashing_same_size_different_content() {
        let dir = std::env::temp_dir().join(format!("disklens_test_{}", std::process::id()));
        let _ = std::fs::create_dir_all(&dir);

        let path_a = dir.join("file_a.dat");
        let path_b = dir.join("file_b.dat");

        // 128 KB files: both share the exact same first 64 KB, but differ at offset 70,000
        let file_len = 128 * 1024;
        let mut data_a = vec![0x42u8; file_len];
        let mut data_b = vec![0x42u8; file_len];
        // Mutate beyond 64KB
        data_a[70_000] = 0xAA;
        data_b[70_000] = 0xBB;

        {
            let mut fa = File::create(&path_a).expect("create file_a");
            fa.write_all(&data_a).expect("write file_a");
            let mut fb = File::create(&path_b).expect("create file_b");
            fb.write_all(&data_b).expect("write file_b");
        }

        // Bounded hash (max_bytes = 65536) only inspects first 64KB -> hashes MUST match
        let bound_a = sha256_file_bounded(&path_a, 65536, false).expect("hash bound a");
        let bound_b = sha256_file_bounded(&path_b, 65536, false).expect("hash bound b");
        assert_eq!(
            bound_a, bound_b,
            "Bounded hashing of first 64KB should match when prefixes are identical"
        );

        // Full-file hash (max_bytes = 0 or negative) reads full file -> hashes MUST NOT match!
        let full_a = sha256_file_bounded(&path_a, 0, false).expect("hash full a");
        let full_b = sha256_file_bounded(&path_b, 0, false).expect("hash full b");
        assert_ne!(
            full_a, full_b,
            "CRITICAL: Full hashing (max_bytes=0) must distinguish files with different bodies!"
        );

        // Negative max_bytes also acts as unlimited
        let full_a_neg = sha256_file_bounded(&path_a, -1, false).expect("hash full a neg");
        assert_eq!(full_a, full_a_neg, "max_bytes < 0 must behave identically to max_bytes = 0");

        let _ = std::fs::remove_file(path_a);
        let _ = std::fs::remove_file(path_b);
        let _ = std::fs::remove_dir(dir);
    }

    #[test]
    fn test_duplicate_hashing_identical_samples_different_middle() {
        let dir = std::env::temp_dir().join(format!("disklens_sample_test_{}", std::process::id()));
        let _ = std::fs::create_dir_all(&dir);

        let path_a = dir.join("sample_a.dat");
        let path_b = dir.join("sample_b.dat");

        // 300 KB files (> max_bytes * 2 when max_bytes = 65536)
        // sha256_file_bounded with max_bytes = 65536 samples:
        // offset 0 (chunk = 32768), offset 150_000 (chunk = 32768), and tail offset (300000 - 32768)
        let file_len = 300 * 1024;
        let mut data_a = vec![0x11u8; file_len];
        let mut data_b = vec![0x11u8; file_len];

        // Put differing content in an UNSAMPLED region (e.g. offset 50_000, which is after head 32KB and before mid 150KB)
        data_a[50_000] = 0xFE;
        data_b[50_000] = 0xCA;

        {
            let mut fa = File::create(&path_a).expect("create sample_a");
            fa.write_all(&data_a).expect("write sample_a");
            let mut fb = File::create(&path_b).expect("create sample_b");
            fb.write_all(&data_b).expect("write sample_b");
        }

        // Bounded sample hash matches because sampled regions (0..32k, 150k..182k, 267k..300k) are identical
        let bound_a = sha256_file_bounded(&path_a, 65536, false).expect("hash sample bound a");
        let bound_b = sha256_file_bounded(&path_b, 65536, false).expect("hash sample bound b");
        assert_eq!(
            bound_a, bound_b,
            "Sampled chunk hashes must match when sampled regions are identical"
        );

        // Full file hash MUST differ
        let full_a = sha256_file_bounded(&path_a, 0, false).expect("hash sample full a");
        let full_b = sha256_file_bounded(&path_b, 0, false).expect("hash sample full b");
        assert_ne!(
            full_a, full_b,
            "Full hashing MUST detect differences in unsampled regions!"
        );

        let _ = std::fs::remove_file(path_a);
        let _ = std::fs::remove_file(path_b);
        let _ = std::fs::remove_dir(dir);
    }

    #[test]
    fn test_hashing_empty_file() {
        let dir = std::env::temp_dir().join(format!("disklens_empty_test_{}", std::process::id()));
        let _ = std::fs::create_dir_all(&dir);
        let path = dir.join("empty.dat");
        File::create(&path).expect("create empty file");

        let hash_zero = sha256_file_bounded(&path, 0, false).expect("hash empty 0");
        let hash_bound = sha256_file_bounded(&path, 65536, false).expect("hash empty bound");
        assert_eq!(hash_zero, hash_bound);
        assert_eq!(sha256_hex(&hash_zero).len(), 64);

        let _ = std::fs::remove_file(path);
        let _ = std::fs::remove_dir(dir);
    }

    #[test]
    fn test_atomic_file_write_and_overwrite() {
        use crate::utils::write_file_atomic;
        let dir = std::env::temp_dir().join(format!("disklens_atomic_test_{}", std::process::id()));
        let _ = std::fs::create_dir_all(&dir);
        let target = dir.join("atomic_target.txt");

        // First write creates file
        assert!(write_file_atomic(&target, b"initial payload"));
        assert_eq!(std::fs::read_to_string(&target).unwrap(), "initial payload");

        // Second write safely replaces content
        assert!(write_file_atomic(&target, b"updated payload"));
        assert_eq!(std::fs::read_to_string(&target).unwrap(), "updated payload");

        // Write to nonexistent directory fails safely without panic
        let bad_target = dir.join("nonexistent_sub").join("target.txt");
        assert!(!write_file_atomic(&bad_target, b"data"));

        let _ = std::fs::remove_file(&target);
        let _ = std::fs::remove_dir(dir);
    }

    #[test]
    fn test_dd_exec_sync_safety() {
        use crate::utils::dd_exec_sync;
        use std::ffi::CString;

        // Null pointer safety
        unsafe {
            assert!(!dd_exec_sync(std::ptr::null(), std::ptr::null(), std::ptr::null(), std::ptr::null()));
        }

        // Nonexistent executable returns false safely without panic
        let bad_cmd = CString::new("this_cmd_absolutely_does_not_exist_xyz123").unwrap();
        unsafe {
            assert!(!dd_exec_sync(bad_cmd.as_ptr(), std::ptr::null(), std::ptr::null(), std::ptr::null()));
        }
    }
}
