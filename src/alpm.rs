use libc::*;
use std::cmp::Ordering;
use std::ffi::CString;
use error::Result;

#[link(name = "alpm")]
extern "C" {
    // TODO: replace with a pure rust version
    fn alpm_pkg_vercmp(a: *const c_char, b: *const c_char) -> c_int;
}

pub fn pkg_vercmp(a: &str, b: &str) -> Result<Ordering> {
    let a = CString::new(a)?;
    let b = CString::new(b)?;
    let res = unsafe { alpm_pkg_vercmp(a.as_ptr(), b.as_ptr()) };
    Ok(res.cmp(&0))
}

#[test]
fn test_version_cmp() {
    let res = pkg_vercmp("1.5-1", "1.5").unwrap();
    assert_eq!(res, Ordering::Equal);
}
