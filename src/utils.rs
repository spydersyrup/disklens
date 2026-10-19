use std::env;
use std::ffi::{c_char, CStr, OsStr};
use std::fs::{self, File, OpenOptions};
use std::io::{Read, Seek, SeekFrom, Write};
use std::path::{Path, PathBuf};
use std::process::{Command, Stdio};
use std::time::{SystemTime, UNIX_EPOCH};

pub fn now_seconds() -> f64 {
    SystemTime::now()
        .duration_since(UNIX_EPOCH)
        .map(|d| d.as_secs_f64())
        .unwrap_or(0.0)
}

pub fn format_bytes(bytes: i64) -> String {
    let abs = bytes.abs() as f64;
    if abs < 1000.0 {
        return format!("{} B", bytes);
    }
    let units = ["B", "KB", "MB", "GB", "TB", "PB"];
    let mut val = abs;
    let mut u = 0usize;
    while val >= 999.5 && u < 5 {
        val /= 1000.0;
        u += 1;
    }
    if val >= 100.0 {
        format!("{:.0} {}", val, units[u])
    } else if val >= 10.0 {
        format!("{:.1} {}", val, units[u])
    } else {
        format!("{:.2} {}", val, units[u])
    }
}

pub fn format_percent(fraction: f64) -> String {
    let pct = (fraction * 100.0).clamp(0.0, 100.0);
    if pct <= 0.0 {
        "0%".to_string()
    } else if pct < 0.1 {
        "<0.1%".to_string()
    } else if pct >= 99.95 {
        "100%".to_string()
    } else {
        format!("{:.1}%", pct)
    }
}

pub fn format_relative_age(timestamp: f64, now: f64) -> String {
    if timestamp <= 0.0 {
        return "Unknown".to_string();
    }
    let diff = (now - timestamp).max(0.0);
    if diff < 60.0 {
        "Just now".to_string()
    } else if diff < 3600.0 {
        let m = (diff / 60.0) as i32;
        format!("{} min{} ago", m, if m == 1 { "" } else { "s" })
    } else if diff < 86400.0 {
        let h = (diff / 3600.0) as i32;
        format!("{} hour{} ago", h, if h == 1 { "" } else { "s" })
    } else if diff < 86400.0 * 30.0 {
        let d = (diff / 86400.0) as i32;
        format!("{} day{} ago", d, if d == 1 { "" } else { "s" })
    } else if diff < 86400.0 * 365.0 {
        let mo = ((diff / (86400.0 * 30.0)) as i32).max(1);
        format!("{} month{} ago", mo, if mo == 1 { "" } else { "s" })
    } else {
        let y = ((diff / (86400.0 * 365.0)) as i32).max(1);
        format!("{} year{} ago", y, if y == 1 { "" } else { "s" })
    }
}

pub fn format_duration(seconds: f64) -> String {
    let s = seconds.max(0.0);
    if s < 0.1 {
        format!("{:.2}s", s)
    } else {
        format!("{:.1}s", s)
    }
}

pub fn abbreviate_label(name: &str) -> String {
    let trimmed = name.trim_start_matches(['.', '_', '-']);
    let src = if trimmed.is_empty() { name } else { trimmed };
    let bytes = src.as_bytes();
    let mut out = String::with_capacity(3);
    let mut prev_sep = true;

    for (i, &b) in bytes.iter().enumerate() {
        let ch = b as char;
        if matches!(ch, ' ' | '_' | '-' | '.') {
            prev_sep = true;
        } else if prev_sep
            || (i > 0 && ch.is_ascii_uppercase() && (bytes[i - 1] as char).is_ascii_lowercase())
        {
            out.push(ch.to_ascii_uppercase());
            prev_sep = false;
            if out.len() >= 3 {
                break;
            }
        }
    }
    if out.len() == 1 && bytes.len() > 1 && (bytes[1] as char).is_ascii_alphanumeric() {
        out.push((bytes[1] as char).to_ascii_uppercase());
    }
    out
}

pub fn truncate_middle(s: &str, max_chars: usize) -> String {
    let chars: Vec<char> = s.chars().collect();
    if chars.len() <= max_chars || max_chars < 8 {
        return s.to_string();
    }
    let head = (max_chars - 3) / 2;
    let tail = max_chars - 3 - head;
    let prefix: String = chars[..head].iter().collect();
    let suffix: String = chars[chars.len() - tail..].iter().collect();
    format!("{}...{}", prefix, suffix)
}

const K256: [u32; 64] = [
    0x428a2f98, 0x71374491, 0xb5c0fbcf, 0xe9b5dba5, 0x3956c25b, 0x59f111f1, 0x923f82a4, 0xab1c5ed5,
    0xd807aa98, 0x12835b01, 0x243185be, 0x550c7dc3, 0x72be5d74, 0x80deb1fe, 0x9bdc06a7, 0xc19bf174,
    0xe49b69c1, 0xefbe4786, 0x0fc19dc6, 0x240ca1cc, 0x2de92c6f, 0x4a7484aa, 0x5cb0a9dc, 0x76f988da,
    0x983e5152, 0xa831c66d, 0xb00327c8, 0xbf597fc7, 0xc6e00bf3, 0xd5a79147, 0x06ca6351, 0x14292967,
    0x27b70a85, 0x2e1b2138, 0x4d2c6dfc, 0x53380d13, 0x650a7354, 0x766a0abb, 0x81c2c92e, 0x92722c85,
    0xa2bfe8a1, 0xa81a664b, 0xc24b8b70, 0xc76c51a3, 0xd192e819, 0xd6990624, 0xf40e3585, 0x106aa070,
    0x19a4c116, 0x1e376c08, 0x2748774c, 0x34b0bcb5, 0x391c0cb3, 0x4ed8aa4a, 0x5b9cca4f, 0x682e6ff3,
    0x748f82ee, 0x78a5636f, 0x84c87814, 0x8cc70208, 0x90befffa, 0xa4506ceb, 0xbef9a3f7, 0xc67178f2,
];

pub struct Sha256 {
    state: [u32; 8],
    bitlen: u64,
    data: [u8; 64],
    datalen: usize,
}

impl Sha256 {
    pub fn new() -> Self {
        Self {
            state: [
                0x6a09e667, 0xbb67ae85, 0x3c6ef372, 0xa54ff53a,
                0x510e527f, 0x9b05688c, 0x1f83d9ab, 0x5be0cd19,
            ],
            bitlen: 0,
            data: [0u8; 64],
            datalen: 0,
        }
    }

    fn transform_block(state: &mut [u32; 8], block: &[u8]) {
        let mut m = [0u32; 64];
        for i in 0..16 {
            let j = i * 4;
            m[i] = ((block[j] as u32) << 24)
                | ((block[j + 1] as u32) << 16)
                | ((block[j + 2] as u32) << 8)
                | (block[j + 3] as u32);
        }
        for i in 16..64 {
            let s0 = m[i - 15].rotate_right(7) ^ m[i - 15].rotate_right(18) ^ (m[i - 15] >> 3);
            let s1 = m[i - 2].rotate_right(17) ^ m[i - 2].rotate_right(19) ^ (m[i - 2] >> 10);
            m[i] = m[i - 16]
                .wrapping_add(s0)
                .wrapping_add(m[i - 7])
                .wrapping_add(s1);
        }
        let [mut a, mut b, mut c, mut d, mut e, mut f, mut g, mut h] = *state;
        for i in 0..64 {
            let s1 = e.rotate_right(6) ^ e.rotate_right(11) ^ e.rotate_right(25);
            let ch = (e & f) ^ ((!e) & g);
            let temp1 = h
                .wrapping_add(s1)
                .wrapping_add(ch)
                .wrapping_add(K256[i])
                .wrapping_add(m[i]);
            let s0 = a.rotate_right(2) ^ a.rotate_right(13) ^ a.rotate_right(22);
            let maj = (a & b) ^ (a & c) ^ (b & c);
            let temp2 = s0.wrapping_add(maj);
            h = g;
            g = f;
            f = e;
            e = d.wrapping_add(temp1);
            d = c;
            c = b;
            b = a;
            a = temp1.wrapping_add(temp2);
        }
        state[0] = state[0].wrapping_add(a);
        state[1] = state[1].wrapping_add(b);
        state[2] = state[2].wrapping_add(c);
        state[3] = state[3].wrapping_add(d);
        state[4] = state[4].wrapping_add(e);
        state[5] = state[5].wrapping_add(f);
        state[6] = state[6].wrapping_add(g);
        state[7] = state[7].wrapping_add(h);
    }

    fn transform(&mut self) {
        Self::transform_block(&mut self.state, &self.data);
    }

    pub fn update(&mut self, mut bytes: &[u8]) {
        if self.datalen > 0 {
            let take = (64 - self.datalen).min(bytes.len());
            self.data[self.datalen..self.datalen + take].copy_from_slice(&bytes[..take]);
            self.datalen += take;
            bytes = &bytes[take..];
            if self.datalen == 64 {
                self.transform();
                self.bitlen = self.bitlen.wrapping_add(512);
                self.datalen = 0;
            }
        }
        while bytes.len() >= 64 {
            Self::transform_block(&mut self.state, &bytes[..64]);
            self.bitlen = self.bitlen.wrapping_add(512);
            bytes = &bytes[64..];
        }
        if !bytes.is_empty() {
            self.data[..bytes.len()].copy_from_slice(bytes);
            self.datalen = bytes.len();
        }
    }

    pub fn finalize(mut self) -> [u8; 32] {
        let mut i = self.datalen;
        self.bitlen = self.bitlen.wrapping_add((self.datalen as u64) * 8);
        if self.datalen < 56 {
            self.data[i] = 0x80;
            i += 1;
            while i < 56 {
                self.data[i] = 0;
                i += 1;
            }
        } else {
            self.data[i] = 0x80;
            i += 1;
            while i < 64 {
                self.data[i] = 0;
                i += 1;
            }
            self.transform();
            self.data[..56].fill(0);
        }
        self.data[56..64].copy_from_slice(&self.bitlen.to_be_bytes());
        self.transform();
        let mut out = [0u8; 32];
        for (idx, word) in self.state.iter().enumerate() {
            out[idx * 4..idx * 4 + 4].copy_from_slice(&word.to_be_bytes());
        }
        out
    }
}

#[cfg(unix)]
extern "C" {
    fn posix_fadvise(fd: i32, offset: i64, len: i64, advice: i32) -> i32;
}

pub fn sha256_file_bounded(path: &Path, max_bytes: i64, nocache: bool) -> Option<[u8; 32]> {
    let mut opts = OpenOptions::new();
    opts.read(true);
    #[cfg(unix)]
    {
        use std::os::unix::fs::OpenOptionsExt;
        opts.custom_flags(0o400000); /* O_NOFOLLOW */
    }
    let mut f = opts.open(path).ok()?;
    let file_len = f.metadata().map(|m| m.len() as i64).unwrap_or(0);
    let mut hasher = Sha256::new();
    hasher.update(&file_len.to_le_bytes());

    if max_bytes > 0 && file_len > max_bytes * 2 {
        let chunk = (max_bytes as usize / 2).clamp(4096, 32768);
        let mut buf = vec![0u8; chunk];
        for &off in &[0u64, (file_len as u64) / 2, (file_len as u64).saturating_sub(chunk as u64)] {
            if f.seek(SeekFrom::Start(off)).is_ok() {
                if let Ok(n) = f.read(&mut buf) {
                    hasher.update(&buf[..n]);
                }
            }
        }
        return Some(hasher.finalize());
    }

    let buf_sz = if max_bytes > 0 && max_bytes <= 65536 { 65536 } else { 262144 };
    let mut buf = vec![0u8; buf_sz];
    let mut total_read = 0i64;

    while total_read < max_bytes {
        let to_read = buf_sz.min((max_bytes - total_read) as usize);
        let n = f.read(&mut buf[..to_read]).ok()?;
        if n == 0 {
            break;
        }
        hasher.update(&buf[..n]);
        total_read += n as i64;
    }

    #[cfg(unix)]
    if nocache && total_read > 0 {
        use std::os::unix::io::AsRawFd;
        unsafe {
            let _ = posix_fadvise(f.as_raw_fd(), 0, file_len.max(total_read), 4 /* POSIX_FADV_DONTNEED */);
        }
    }
    #[cfg(not(unix))]
    let _ = (nocache, total_read);

    Some(hasher.finalize())
}

pub fn sha256_hex(hash: &[u8; 32]) -> String {
    const HEX: &[u8; 16] = b"0123456789abcdef";
    let mut s = String::with_capacity(64);
    for &b in hash {
        s.push(HEX[(b >> 4) as usize] as char);
        s.push(HEX[(b & 0xF) as usize] as char);
    }
    s
}

pub fn resolve_data_dir(subdir: &str) -> PathBuf {
    #[cfg(windows)]
    {
        if let Ok(appdata) = env::var("APPDATA") {
            let dir = PathBuf::from(appdata).join("DiskDude").join(subdir);
            if fs::create_dir_all(&dir).is_ok() {
                return dir;
            }
        }
    }
    #[cfg(not(windows))]
    {
        if let Ok(home) = env::var("HOME") {
            let base = if subdir == "Trash/files" || subdir == "Trash/info" {
                PathBuf::from(home).join(".local/share").join(subdir)
            } else {
                PathBuf::from(home).join(".local/share/DiskDude").join(subdir)
            };
            if fs::create_dir_all(&base).is_ok() {
                return base;
            }
        }
    }
    let fallback = PathBuf::from("/tmp/DiskDude").join(subdir);
    let _ = fs::create_dir_all(&fallback);
    fallback
}

pub fn write_file_atomic(path: &Path, content: &[u8]) -> bool {
    let tmp_path = path.with_extension("tmp");
    let ok = (|| -> std::io::Result<()> {
        let mut f = File::create(&tmp_path)?;
        f.write_all(content)?;
        f.sync_all()?;
        fs::rename(&tmp_path, path)?;
        Ok(())
    })();
    if ok.is_err() {
        let _ = fs::remove_file(&tmp_path);
        false
    } else {
        true
    }
}

unsafe fn write_c_buf(s: &str, buf: *mut c_char, cap: usize) {
    if buf.is_null() || cap == 0 {
        return;
    }
    let bytes = s.as_bytes();
    let copy_len = bytes.len().min(cap - 1);
    std::ptr::copy_nonoverlapping(bytes.as_ptr(), buf as *mut u8, copy_len);
    *buf.add(copy_len) = 0;
}

#[no_mangle]
pub unsafe extern "C" fn dd_format_bytes(bytes: i64, buf: *mut c_char, cap: usize) {
    let s = format_bytes(bytes);
    write_c_buf(&s, buf, cap);
}

#[no_mangle]
pub unsafe extern "C" fn dd_format_percent(ratio: f64, buf: *mut c_char, cap: usize) {
    let s = format_percent(ratio);
    write_c_buf(&s, buf, cap);
}

#[no_mangle]
pub unsafe extern "C" fn dd_format_relative_age(timestamp: f64, buf: *mut c_char, cap: usize) {
    let s = format_relative_age(timestamp, now_seconds());
    write_c_buf(&s, buf, cap);
}

#[no_mangle]
pub unsafe extern "C" fn dd_format_duration(seconds: f64, buf: *mut c_char, cap: usize) {
    let s = format_duration(seconds);
    write_c_buf(&s, buf, cap);
}

#[no_mangle]
pub unsafe extern "C" fn dd_abbreviate_label(name: *const c_char, buf: *mut c_char, cap: usize) {
    if name.is_null() {
        write_c_buf("", buf, cap);
        return;
    }
    let cstr = CStr::from_ptr(name).to_string_lossy();
    let s = abbreviate_label(&cstr);
    write_c_buf(&s, buf, cap);
}

#[no_mangle]
pub unsafe extern "C" fn dd_truncate_middle(
    s_ptr: *const c_char,
    max_chars: usize,
    buf: *mut c_char,
    cap: usize,
) {
    if s_ptr.is_null() {
        write_c_buf("", buf, cap);
        return;
    }
    let cstr = CStr::from_ptr(s_ptr).to_string_lossy();
    let s = truncate_middle(&cstr, max_chars);
    write_c_buf(&s, buf, cap);
}

#[no_mangle]
pub unsafe extern "C" fn dd_sha256_file(
    path: *const c_char,
    max_bytes: i64,
    nocache: bool,
    out_hash: *mut u8,
) -> bool {
    if path.is_null() || out_hash.is_null() {
        return false;
    }
    let p_str = CStr::from_ptr(path).to_string_lossy();
    if let Some(digest) = sha256_file_bounded(Path::new(p_str.as_ref()), max_bytes, nocache) {
        std::ptr::copy_nonoverlapping(digest.as_ptr(), out_hash, 32);
        true
    } else {
        false
    }
}

#[no_mangle]
pub unsafe extern "C" fn dd_sha256_hex(hash: *const u8, out_hex: *mut c_char) {
    if hash.is_null() || out_hex.is_null() {
        return;
    }
    let slice = std::slice::from_raw_parts(hash, 32);
    let mut arr = [0u8; 32];
    arr.copy_from_slice(slice);
    let s = sha256_hex(&arr);
    write_c_buf(&s, out_hex, 65);
}

#[no_mangle]
pub unsafe extern "C" fn dd_get_data_dir(subdir: *const c_char, buf: *mut c_char, cap: usize) {
    let sub = if subdir.is_null() {
        ""
    } else {
        CStr::from_ptr(subdir).to_str().unwrap_or("")
    };
    let dir = resolve_data_dir(sub);
    write_c_buf(&dir.to_string_lossy(), buf, cap);
}

#[no_mangle]
pub unsafe extern "C" fn dd_write_file_atomic(path: *const c_char, content: *const c_char) -> bool {
    if path.is_null() || content.is_null() {
        return false;
    }
    let p_str = CStr::from_ptr(path).to_string_lossy();
    let c_bytes = CStr::from_ptr(content).to_bytes();
    write_file_atomic(Path::new(p_str.as_ref()), c_bytes)
}

#[no_mangle]
pub unsafe extern "C" fn dd_exec_sync(
    prog: *const c_char,
    arg1: *const c_char,
    arg2: *const c_char,
    arg3: *const c_char,
) -> bool {
    if prog.is_null() {
        return false;
    }
    let prog_s = CStr::from_ptr(prog).to_string_lossy();
    let mut cmd = Command::new(OsStr::new(prog_s.as_ref()));
    cmd.stdin(Stdio::null())
        .stdout(Stdio::null())
        .stderr(Stdio::null());
    for a in [arg1, arg2, arg3] {
        if !a.is_null() {
            let s = CStr::from_ptr(a).to_string_lossy();
            cmd.arg(s.as_ref());
        }
    }
    cmd.status().map(|st| st.success()).unwrap_or(false)
}
