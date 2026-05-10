pub const VFS_PATH_MAX: usize = 256;
pub const VFS_NAME_MAX: usize = 64;
pub const VFS_MAX_MOUNTS: usize = 16;

#[repr(C)]
#[derive(Clone, Copy)]
pub struct MountView {
    pub active: u8,
    pub _pad: [u8; 7],
    pub path: [u8; VFS_PATH_MAX],
}

#[repr(C)]
#[derive(Clone, Copy)]
pub struct RouteOut {
    pub status: i32,
    pub found: u8,
    pub _pad: [u8; 7],
    pub mount_index: usize,
    pub normalized: [u8; VFS_PATH_MAX],
    pub relative: [u8; VFS_PATH_MAX],
}

#[derive(Clone, Copy, Eq, PartialEq)]
enum RouteError {
    Invalid,
    NoEntry,
}

#[inline(always)]
unsafe fn slice_get(buf: &[u8], idx: usize) -> u8 {
    *buf.as_ptr().add(idx)
}

#[inline(always)]
unsafe fn arr_get<const N: usize>(buf: &[u8; N], idx: usize) -> u8 {
    *buf.as_ptr().add(idx)
}

#[inline(always)]
unsafe fn arr_set<const N: usize>(buf: &mut [u8; N], idx: usize, value: u8) {
    *buf.as_mut_ptr().add(idx) = value;
}

#[inline(always)]
unsafe fn mount_ref(mounts: &[MountView; VFS_MAX_MOUNTS], idx: usize) -> &MountView {
    &*mounts.as_ptr().add(idx)
}

#[inline(always)]
unsafe fn comps_byte_ptr(comps: *mut [u8; VFS_NAME_MAX], comp: usize) -> *mut u8 {
    comps.add(comp) as *mut u8
}

fn cstr_len(buf: &[u8]) -> usize {
    let mut i = 0;
    let len = buf.len();
    while i < len && unsafe { slice_get(buf, i) } != 0 { i += 1; }
    i
}

fn fixed_cstr_len<const N: usize>(buf: &[u8; N]) -> usize {
    let mut i = 0;
    while i < N && unsafe { arr_get(buf, i) } != 0 { i += 1; }
    i
}

fn push_byte(out: &mut [u8; VFS_PATH_MAX], pos: &mut usize, b: u8) -> Result<(), RouteError> {
    if *pos + 1 >= VFS_PATH_MAX { return Err(RouteError::Invalid); }
    unsafe {
        arr_set(out, *pos, b);
        *pos += 1;
        arr_set(out, *pos, 0);
    }
    Ok(())
}

fn normalize(input: &[u8]) -> Result<[u8; VFS_PATH_MAX], RouteError> {
    let len = cstr_len(input);
    if len == 0 || len >= VFS_PATH_MAX || unsafe { slice_get(input, 0) } != b'/' { return Err(RouteError::Invalid); }
    let mut comps = [[0u8; VFS_NAME_MAX]; 32];
    let comps_ptr = comps.as_mut_ptr();
    let mut count = 0usize;
    let mut i = 0usize;
    while i < len {
        while i < len && unsafe { slice_get(input, i) } == b'/' { i += 1; }
        let start = i;
        while i < len && unsafe { slice_get(input, i) } != b'/' { i += 1; }
        let clen = i - start;
        if clen == 0 || (clen == 1 && unsafe { slice_get(input, start) } == b'.') { continue; }
        if clen == 2 && unsafe { slice_get(input, start) } == b'.' && unsafe { slice_get(input, start + 1) } == b'.' {
            count = count.saturating_sub(1);
            continue;
        }
        if clen >= VFS_NAME_MAX || count >= 32 { return Err(RouteError::Invalid); }
        let comp_ptr = unsafe { comps_byte_ptr(comps_ptr, count) };
        let mut j = 0;
        while j < clen {
            unsafe { *comp_ptr.add(j) = slice_get(input, start + j); }
            j += 1;
        }
        unsafe { *comp_ptr.add(clen) = 0; }
        count += 1;
    }
    let mut out = [0u8; VFS_PATH_MAX];
    let mut pos = 0usize;
    push_byte(&mut out, &mut pos, b'/')?;
    let mut c = 0usize;
    while c < count {
        let comp_ref = unsafe { &*comps_ptr.add(c) };
        let clen = fixed_cstr_len(comp_ref);
        let mut j = 0usize;
        while j < clen {
            let b = unsafe { arr_get(comp_ref, j) };
            push_byte(&mut out, &mut pos, b)?;
            j += 1;
        }
        if c + 1 < count { push_byte(&mut out, &mut pos, b'/')?; }
        c += 1;
    }
    Ok(out)
}

fn path_depth(path: &[u8; VFS_PATH_MAX]) -> usize {
    let len = fixed_cstr_len(path);
    if len == 1 && unsafe { arr_get(path, 0) } == b'/' { return 0; }
    let mut d = 0usize;
    let mut in_component = false;
    let mut i = 0usize;
    while i < len {
        if unsafe { arr_get(path, i) } == b'/' { in_component = false; }
        else if !in_component { in_component = true; d += 1; }
        i += 1;
    }
    d
}

fn mount_matches(mount: &[u8; VFS_PATH_MAX], path: &[u8; VFS_PATH_MAX]) -> bool {
    let ml = fixed_cstr_len(mount);
    if ml == 0 { return false; }
    if ml == 1 && unsafe { arr_get(mount, 0) } == b'/' { return true; }
    let pl = fixed_cstr_len(path);
    if ml > pl { return false; }
    let mut i = 0usize;
    while i < ml {
        if unsafe { arr_get(mount, i) } != unsafe { arr_get(path, i) } { return false; }
        i += 1;
    }
    (unsafe { arr_get(path, ml) }) == 0 || (unsafe { arr_get(path, ml) }) == b'/'
}

fn compute_relative(mount: &[u8; VFS_PATH_MAX], norm: &[u8; VFS_PATH_MAX]) -> [u8; VFS_PATH_MAX] {
    let mut out = [0u8; VFS_PATH_MAX];
    let ml = fixed_cstr_len(mount);
    if ml == 1 && unsafe { arr_get(mount, 0) } == b'/' {
        out = *norm;
        return out;
    }
    if ml < VFS_PATH_MAX && unsafe { arr_get(norm, ml) } == 0 {
        unsafe { arr_set(&mut out, 0, b'/'); arr_set(&mut out, 1, 0); }
        return out;
    }
    let mut src = ml;
    let mut dst = 0usize;
    while src < VFS_PATH_MAX && unsafe { arr_get(norm, src) } != 0 && dst + 1 < VFS_PATH_MAX {
        let b = unsafe { arr_get(norm, src) };
        unsafe { arr_set(&mut out, dst, b); }
        dst += 1;
        src += 1;
    }
    if dst == 0 { unsafe { arr_set(&mut out, 0, b'/'); arr_set(&mut out, 1, 0); } }
    out
}

fn route(mounts: &[MountView; VFS_MAX_MOUNTS], input: &[u8]) -> Result<RouteOut, RouteError> {
    let norm = normalize(input)?;
    let mut best: Option<(usize, usize)> = None;
    let mut i = 0usize;
    while i < VFS_MAX_MOUNTS {
        let m = unsafe { mount_ref(mounts, i) };
        if m.active != 0 && mount_matches(&m.path, &norm) {
            let depth = path_depth(&m.path);
            if best.map_or(true, |(_, d)| depth >= d) { best = Some((i, depth)); }
        }
        i += 1;
    }
    let (idx, _) = match best { Some(v) => v, None => return Err(RouteError::NoEntry) };
    let m = unsafe { mount_ref(mounts, idx) };
    Ok(RouteOut {
        status: 0,
        found: 1,
        _pad: [0; 7],
        mount_index: idx,
        normalized: norm,
        relative: compute_relative(&m.path, &norm),
    })
}

#[no_mangle]
pub extern "C" fn aurora_rust_vfs_route(mounts: *const MountView, input: *const u8, input_len: usize, out: *mut RouteOut) -> i32 {
    if mounts.is_null() || input.is_null() || out.is_null() || input_len == 0 || input_len > VFS_PATH_MAX { return -3; }
    let mounts_ref = unsafe { &*(mounts as *const [MountView; VFS_MAX_MOUNTS]) };
    let input_ref = unsafe { core::slice::from_raw_parts(input, input_len) };
    match route(mounts_ref, input_ref) {
        Ok(v) => { unsafe { *out = v; } 0 }
        Err(RouteError::NoEntry) => -1,
        Err(RouteError::Invalid) => -3,
    }
}

#[no_mangle]
pub extern "C" fn aurora_rust_vfs_route_selftest() -> bool {
    let mut mounts = [MountView { active: 0, _pad: [0; 7], path: [0; VFS_PATH_MAX] }; VFS_MAX_MOUNTS];
    unsafe {
        let m0 = mounts.as_mut_ptr().add(0);
        (*m0).active = 1;
        arr_set(&mut (*m0).path, 0, b'/');
        let m1 = mounts.as_mut_ptr().add(1);
        (*m1).active = 1;
        arr_set(&mut (*m1).path, 0, b'/');
        arr_set(&mut (*m1).path, 1, b'd');
        arr_set(&mut (*m1).path, 2, b'i');
        arr_set(&mut (*m1).path, 3, b's');
        arr_set(&mut (*m1).path, 4, b'k');
        arr_set(&mut (*m1).path, 5, b'0');
    }
    let mut out = RouteOut { status: 99, found: 0, _pad: [0; 7], mount_index: 99, normalized: [0; VFS_PATH_MAX], relative: [0; VFS_PATH_MAX] };
    let path = b"/disk0/../disk0/hello.txt\0";
    let rc = aurora_rust_vfs_route(mounts.as_ptr(), path.as_ptr(), path.len(), &mut out as *mut RouteOut);
    if rc != 0 || out.found != 1 || out.mount_index != 1 { return false; }
    if (unsafe { arr_get(&out.normalized, 0) }) != b'/' || (unsafe { arr_get(&out.normalized, 1) }) != b'd' || (unsafe { arr_get(&out.relative, 0) }) != b'/' || (unsafe { arr_get(&out.relative, 1) }) != b'h' { return false; }
    let disk_root = b"/disk0\0";
    let rc_root = aurora_rust_vfs_route(mounts.as_ptr(), disk_root.as_ptr(), disk_root.len(), &mut out as *mut RouteOut);
    if rc_root != 0 || out.mount_index != 1 || (unsafe { arr_get(&out.relative, 0) }) != b'/' || (unsafe { arr_get(&out.relative, 1) }) != 0 { return false; }
    let rel = b"relative/path\0";
    if aurora_rust_vfs_route(mounts.as_ptr(), rel.as_ptr(), rel.len(), &mut out as *mut RouteOut) != -3 { return false; }
    let bad = b"\0";
    aurora_rust_vfs_route(mounts.as_ptr(), bad.as_ptr(), bad.len(), &mut out as *mut RouteOut) == -3
}
