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

