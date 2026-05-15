pub const PAGE_SIZE: u64 = 4096;
pub const USER_MIN: u64 = 0x0000_0000_0001_0000;
pub const USER_LIMIT: u64 = 0x0000_8000_0000_0000;

#[repr(C)]
#[derive(Clone, Copy)]
pub struct UserCopyStep {
    pub ok: u8,
    pub _pad: [u8; 7],
    pub page_offset: usize,
    pub chunk: usize,
    pub remaining_after: usize,
}

#[derive(Clone, Copy, Eq, PartialEq)]
enum RangeError {
    NullWithSize,
    LowAddress,
    Overflow,
    AboveLimit,
}

fn range_check(addr: u64, size: usize) -> Result<(), RangeError> {
    if size == 0 { return Ok(()); }
    if addr == 0 { return Err(RangeError::NullWithSize); }
    if addr < USER_MIN { return Err(RangeError::LowAddress); }
    let size64 = size as u64;
    let end = match addr.checked_add(size64 - 1) {
        Some(v) => v,
        None => return Err(RangeError::Overflow),
    };
    if end >= USER_LIMIT { return Err(RangeError::AboveLimit); }
    Ok(())
}

fn copy_step(addr: u64, remaining: usize) -> Result<UserCopyStep, RangeError> {
    range_check(addr, remaining)?;
    let page_offset = (addr & (PAGE_SIZE - 1)) as usize;
    let max_in_page = PAGE_SIZE as usize - page_offset;
    let chunk = if remaining < max_in_page { remaining } else { max_in_page };
    Ok(UserCopyStep { ok: 1, _pad: [0; 7], page_offset, chunk, remaining_after: remaining - chunk })
}

#[no_mangle]
pub extern "C" fn rabbitbone_rust_user_range_check(addr: u64, size: usize) -> bool {
    range_check(addr, size).is_ok()
}

#[no_mangle]
pub extern "C" fn rabbitbone_rust_user_copy_step(addr: u64, remaining: usize, out: *mut UserCopyStep) -> bool {
    if out.is_null() { return false; }
    match copy_step(addr, remaining) {
        Ok(v) => { unsafe { *out = v; } true }
        Err(_) => { unsafe { *out = UserCopyStep { ok: 0, _pad: [0; 7], page_offset: 0, chunk: 0, remaining_after: remaining }; } false }
    }
}

#[no_mangle]
pub extern "C" fn rabbitbone_rust_usercopy_selftest() -> bool {
    if !range_check(USER_MIN, 1).is_ok() { return false; }
    if range_check(0, 1).is_ok() { return false; }
    if range_check(USER_MIN - 1, 1).is_ok() { return false; }
    if range_check(USER_LIMIT - 1, 1).is_err() { return false; }
    if range_check(USER_LIMIT - 1, 2).is_ok() { return false; }
    if range_check(u64::MAX - 4, 8).is_ok() { return false; }
    let s = match copy_step(USER_MIN + 4094, 8) { Ok(v) => v, Err(_) => return false };
    if s.page_offset != 4094 || s.chunk != 2 || s.remaining_after != 6 { return false; }
    true
}
