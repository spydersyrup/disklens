use std::ffi::{c_char, c_int};

#[repr(u8)]
#[derive(Clone, Copy, Debug, PartialEq, Eq, Hash)]
pub enum FileCategory {
    Video = 0,
    Audio = 1,
    Image = 2,
    Document = 3,
    Developer = 4,
    Archive = 5,
    Apps = 6,
    System = 7,
    Other = 8,
}

impl FileCategory {
    pub fn classify_bytes(name_bytes: &[u8], inside_app_bundle: bool) -> Self {
        if inside_app_bundle {
            return Self::Apps;
        }
        let dot_pos = match name_bytes.iter().rposition(|&b| b == b'.') {
            Some(pos) if pos > 0 && pos + 1 < name_bytes.len() => pos + 1,
            _ => return Self::Other,
        };
        let ext_raw = &name_bytes[dot_pos..];
        if ext_raw.is_empty() || ext_raw.len() > 15 {
            return Self::Other;
        }
        let mut ext_buf = [0u8; 16];
        for (i, &b) in ext_raw.iter().enumerate() {
            ext_buf[i] = b.to_ascii_lowercase();
        }
        let ext = match std::str::from_utf8(&ext_buf[..ext_raw.len()]) {
            Ok(s) => s,
            Err(_) => return Self::Other,
        };

        match ext {
            "mp4" | "mov" | "m4v" | "mkv" | "avi" | "wmv" | "flv" | "webm" | "mpg" | "mpeg"
            | "3gp" | "mts" | "m2ts" | "vob" => Self::Video,

            "mp3" | "wav" | "aac" | "m4a" | "flac" | "ogg" | "aiff" | "aif" | "alac" | "wma"
            | "opus" | "mid" | "midi" | "caf" | "m4b" => Self::Audio,

            "jpg" | "jpeg" | "png" | "gif" | "heic" | "heif" | "tiff" | "tif" | "bmp" | "webp"
            | "svg" | "raw" | "cr2" | "cr3" | "nef" | "arw" | "dng" | "psd" | "ai" | "sketch"
            | "fig" | "ico" | "icns" | "exr" => Self::Image,

            "pdf" | "doc" | "docx" | "xls" | "xlsx" | "ppt" | "pptx" | "key" | "pages"
            | "numbers" | "txt" | "rtf" | "md" | "csv" | "epub" | "odt" | "ods" | "odp"
            | "html" | "htm" | "tex" => Self::Document,

            "swift" | "m" | "mm" | "h" | "c" | "cc" | "cpp" | "hpp" | "js" | "mjs" | "cjs"
            | "ts" | "tsx" | "jsx" | "json" | "py" | "pyc" | "rb" | "go" | "rs" | "java"
            | "kt" | "class" | "jar" | "o" | "a" | "dylib" | "so" | "dll" | "pdb" | "node"
            | "wasm" | "map" | "lock" | "yml" | "yaml" | "toml" | "xml" | "sh" | "zsh" | "css"
            | "scss" | "less" | "gradle" | "pbxproj" | "storyboard" | "xib" | "sql" | "php"
            | "vue" | "svelte" | "dart" | "lua" | "r" | "ipynb" | "tsbuildinfo" | "d" | "cs" => {
                Self::Developer
            }

            "zip" | "gz" | "tgz" | "tar" | "bz2" | "xz" | "7z" | "rar" | "dmg" | "iso" | "pkg"
            | "xip" | "zst" | "lz4" | "cab" | "deb" | "rpm" | "msi" => Self::Archive,

            "app" | "appimage" | "flatpak" | "exe" => Self::Apps,

            "plist" | "log" | "db" | "sqlite" | "sqlite3" | "sqlite-wal" | "sqlite-shm"
            | "db-wal" | "db-shm" | "wal" | "shm" | "cache" | "dat" | "ldb" | "idx" | "pack"
            | "vmdk" | "img" | "sparseimage" | "sparsebundle" | "asar" | "qcow2" | "vhd"
            | "vhdx" => Self::System,

            _ => Self::Other,
        }
    }
}

#[no_mangle]
pub unsafe extern "C" fn dd_classify_filename(
    name: *const c_char,
    len: usize,
    inside_app_bundle: bool,
) -> c_int {
    if name.is_null() || len == 0 {
        return if inside_app_bundle {
            FileCategory::Apps as c_int
        } else {
            FileCategory::Other as c_int
        };
    }
    let bytes = std::slice::from_raw_parts(name as *const u8, len);
    FileCategory::classify_bytes(bytes, inside_app_bundle) as c_int
}
