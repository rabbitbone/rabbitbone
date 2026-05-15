pub const VFS_PATH_MAX: usize = crate::abi::RABBITBONE_PATH_MAX as usize;
pub const VFS_NAME_MAX: usize = crate::abi::RABBITBONE_NAME_MAX as usize;

#[derive(Clone, Copy, Eq, PartialEq)]
enum PathError {
    Null,
    Empty,
    TooLong,
    NotAbsolute,
    BadByte,
    ComponentTooLong,
    DotComponent,
}

#[inline(always)]
unsafe fn slice_get_unchecked(buf: &[u8], idx: usize) -> u8 {
    *buf.as_ptr().add(idx)
}

#[inline(always)]
unsafe fn slice_set_unchecked(buf: &mut [u8], idx: usize, value: u8) {
    *buf.as_mut_ptr().add(idx) = value;
}

fn cstr_len(buf: &[u8]) -> Result<usize, PathError> {
    let mut i = 0usize;
    let len = buf.len();
    while i < len {
        let b = unsafe { slice_get_unchecked(buf, i) };
        if b == 0 { return if i == 0 { Err(PathError::Empty) } else { Ok(i) }; }
        i += 1;
    }
    Err(PathError::TooLong)
}

fn component_is_dot(buf: &[u8], start: usize, len: usize) -> bool {
    len == 1 && unsafe { slice_get_unchecked(buf, start) } == b'.'
}

fn component_is_dotdot(buf: &[u8], start: usize, len: usize) -> bool {
    len == 2 && unsafe { slice_get_unchecked(buf, start) } == b'.' && unsafe { slice_get_unchecked(buf, start + 1) } == b'.'
}

fn validate_component(buf: &[u8], start: usize, len: usize) -> Result<(), PathError> {
    if len == 0 { return Ok(()); }
    if len >= VFS_NAME_MAX { return Err(PathError::ComponentTooLong); }
    if component_is_dot(buf, start, len) || component_is_dotdot(buf, start, len) { return Err(PathError::DotComponent); }
    Ok(())
}

fn validate_path_shape(buf: &[u8], require_absolute: bool, reject_dots: bool) -> Result<(), PathError> {
    if buf.is_empty() { return Err(PathError::Null); }
    let len = cstr_len(buf)?;
    if len >= VFS_PATH_MAX { return Err(PathError::TooLong); }
    if require_absolute && unsafe { slice_get_unchecked(buf, 0) } != b'/' { return Err(PathError::NotAbsolute); }
    let mut comp_start = 0usize;
    let mut comp_len = 0usize;
    let mut i = 0usize;
    while i < len {
        let b = unsafe { slice_get_unchecked(buf, i) };
        if b < 0x20 || b == 0x7f || b == b'\\' { return Err(PathError::BadByte); }
        if b == b'/' {
            if reject_dots { validate_component(buf, comp_start, comp_len)?; }
            comp_start = i + 1;
            comp_len = 0;
        } else {
            if comp_len == 0 { comp_start = i; }
            comp_len += 1;
            if comp_len >= VFS_NAME_MAX { return Err(PathError::ComponentTooLong); }
        }
        i += 1;
    }
    if reject_dots { validate_component(buf, comp_start, comp_len)?; }
    Ok(())
}

fn validate_path(buf: &[u8]) -> Result<(), PathError> {
    validate_path_shape(buf, true, true)
}

#[no_mangle]
pub extern "C" fn rabbitbone_rust_path_policy_check(ptr: *const u8, max_len: usize) -> bool {
    if ptr.is_null() || max_len == 0 || max_len > VFS_PATH_MAX { return false; }
    let slice = unsafe { core::slice::from_raw_parts(ptr, max_len) };
    validate_path(slice).is_ok()
}

#[no_mangle]
pub extern "C" fn rabbitbone_rust_path_no_traversal_check(ptr: *const u8, max_len: usize, allow_relative: bool) -> bool {
    if ptr.is_null() || max_len == 0 || max_len > VFS_PATH_MAX { return false; }
    let slice = unsafe { core::slice::from_raw_parts(ptr, max_len) };
    validate_path_shape(slice, !allow_relative, true).is_ok()
}

#[no_mangle]
pub extern "C" fn rabbitbone_rust_path_policy_selftest() -> bool {
    if !validate_path(b"/tmp/file\0").is_ok() { return false; }
    if validate_path(b"/disk0/../disk0/hello.txt\0").is_ok() { return false; }
    if validate_path(b"relative\0").is_ok() { return false; }
    if !validate_path(b"/disk0/hello.txt\0").is_ok() { return false; }
    if !validate_path_shape(b"relative/link\0", false, true).is_ok() { return false; }
    if validate_path_shape(b"../escape\0", false, true).is_ok() { return false; }
    if validate_path(b"/bad\\path\0").is_ok() { return false; }
    if validate_path(b"\0").is_ok() { return false; }
    let mut long = [b'a'; VFS_NAME_MAX + 2];
    unsafe {
        slice_set_unchecked(&mut long, 0, b'/');
        slice_set_unchecked(&mut long, VFS_NAME_MAX + 1, 0);
    }
    if validate_path(&long).is_ok() { return false; }
    true
}
