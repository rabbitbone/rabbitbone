#ifndef RABBITBONE_RUST_H
#define RABBITBONE_RUST_H
#include <rabbitbone/types.h>
#include <rabbitbone/syscall.h>
#if defined(__cplusplus)
extern "C" {
#endif

#define RABBITBONE_RUST_VFS_PATH_MAX 256u
#define RABBITBONE_RUST_VFS_MAX_MOUNTS 16u

typedef struct rabbitbone_rust_sysargs {
    u64 a0;
    u64 a1;
    u64 a2;
    u64 a3;
    u64 a4;
    u64 a5;
} rabbitbone_rust_sysargs_t;

typedef struct rabbitbone_rust_mount_view {
    u8 active;
    u8 pad[7];
    char path[RABBITBONE_RUST_VFS_PATH_MAX];
} rabbitbone_rust_mount_view_t;

typedef struct rabbitbone_rust_route_out {
    i32 status;
    u8 found;
    u8 pad[7];
    usize mount_index;
    char normalized[RABBITBONE_RUST_VFS_PATH_MAX];
    char relative[RABBITBONE_RUST_VFS_PATH_MAX];
} rabbitbone_rust_route_out_t;


typedef struct rabbitbone_rust_user_copy_step {
    u8 ok;
    u8 pad[7];
    usize page_offset;
    usize chunk;
    usize remaining_after;
} rabbitbone_rust_user_copy_step_t;

syscall_result_t rabbitbone_rust_syscall_dispatch(u64 no, rabbitbone_rust_sysargs_t args);
const u8 *rabbitbone_rust_syscall_name(u64 no);
bool rabbitbone_rust_syscall_selftest(void);

i32 rabbitbone_rust_vfs_route(const rabbitbone_rust_mount_view_t *mounts, const u8 *input, usize input_len, rabbitbone_rust_route_out_t *out);
bool rabbitbone_rust_vfs_route_selftest(void);

bool rabbitbone_rust_user_range_check(u64 addr, usize size);
bool rabbitbone_rust_user_copy_step(u64 addr, usize remaining, rabbitbone_rust_user_copy_step_t *out);
bool rabbitbone_rust_usercopy_selftest(void);
bool rabbitbone_rust_path_policy_check(const u8 *ptr, usize max_len);
bool rabbitbone_rust_path_no_traversal_check(const u8 *ptr, usize max_len, bool allow_relative);
bool rabbitbone_rust_path_policy_selftest(void);

#if defined(__cplusplus)
}
#endif
#endif
