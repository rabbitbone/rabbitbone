#![no_std]

pub mod syscall_dispatch;
pub mod vfs_route;
pub mod usercopy;
pub mod path_policy;

extern "C" {
    fn aurora_rust_panic(msg: *const u8) -> !;
}

#[panic_handler]
fn panic(_: &core::panic::PanicInfo<'_>) -> ! {
    unsafe { aurora_rust_panic(b"panic\0".as_ptr()) }
}

#[no_mangle]
pub extern "C" fn rust_eh_personality() {}

#[no_mangle]
pub extern "C" fn aurora_rust_panic_bounds_check(_index: usize, _len: usize) -> ! {
    unsafe { aurora_rust_panic(b"bounds-check\0".as_ptr()) }
}
