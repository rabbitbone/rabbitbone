#include <aurora/console.h>
#include <aurora/drivers.h>
#include <aurora/arch/cpu.h>
#include <aurora/arch/io.h>
#include <aurora/memory.h>
#include <aurora/vmm.h>
#include <aurora/kmem.h>
#include <aurora/log.h>
#include <aurora/panic.h>
#include <aurora/libc.h>
#include <aurora/block.h>
#include <aurora/mbr.h>
#include <aurora/ext4.h>
#include <aurora/vfs.h>
#include <aurora/ktest.h>
#include <aurora/syscall.h>
#include <aurora/elf64.h>
#include <aurora/task.h>
#include <aurora/process.h>
#include <aurora/scheduler.h>
#include <aurora/user_bins.h>
#include <aurora/version.h>

#define LINE_MAX 192u
#define CAT_BUF 256u

static bool parse_u64_dec_checked(const char *s, u64 *out) {
    if (!s || !*s || !out) return false;
    u64 v = 0;
    for (const char *p = s; *p; ++p) {
        if (*p < '0' || *p > '9') return false;
        u64 d = (u64)(*p - '0');
        if (v > (0xffffffffffffffffull - d) / 10ull) return false;
        v = v * 10ull + d;
    }
    *out = v;
    return true;
}

static void print_help(void) {
    kprintf("commands:\n");
    kprintf("  help              show this help\n");
    kprintf("  clear             clear screen\n");
    kprintf("  uname             show kernel version\n");
    kprintf("  ticks             show PIT tick counter\n");
    kprintf("  mem               show physical allocator stats\n");
    kprintf("  heap              show kernel heap stats\n");
    kprintf("  vmm               show virtual memory stats\n");
    kprintf("  ktest             run kernel self-tests\n");
    kprintf("  logs              dump kernel log ring\n");
    kprintf("  disks             show block devices and MBR partitions\n");
    kprintf("  mounts            show VFS mount table\n");
    kprintf("  ls [PATH]         list VFS directory\n");
    kprintf("  stat PATH         show VFS node metadata\n");
    kprintf("  cat PATH          print VFS file\n");
    kprintf("  PIPELINE          run user programs connected by | with < > 2> redirection\n");
    kprintf("  write PATH TEXT   write TEXT to writable VFS file\n");
    kprintf("  mkdir PATH        create ramfs directory\n");
    kprintf("  touch PATH TEXT   create/replace ramfs file\n");
    kprintf("  rm PATH           unlink writable VFS file or empty dir\n");
    kprintf("  mv OLD NEW        rename within same writable mount\n");
    kprintf("  truncate PATH N   resize writable VFS file\n");
    kprintf("  ext4              raw EXT4 probe of first Linux MBR partition\n");
    kprintf("  syscall PATH      test syscall open/read on PATH\n");
    kprintf("  fdprobe PATH      open PATH and show fd metadata\n");
    kprintf("  run PATH [ARGS]   execute ring3 ELF64 user program\n");
    kprintf("  lastproc          show last user process result\n");
    kprintf("  procs             show user process registry\n");
    kprintf("  spawn PATH        run PATH synchronously and store pid\n");
    kprintf("  wait PID          show completed process metadata\n");
    kprintf("  qspawn PATH       queue PATH for scheduler dispatch\n");
    kprintf("  runq [N]          run queued scheduler jobs\n");
    kprintf("  sched             show process scheduler state\n");
    kprintf("  preempt           show PIT preemption/timeslice counters\n");
    kprintf("  ps                show kernel task table\n");
    kprintf("  schedtest         run cooperative task scheduler self-test\n");
    kprintf("  elf PATH          parse/load ELF64 image from VFS without executing\n");
    kprintf("  userbins          validate embedded /bin ELF payloads\n");
    kprintf("  panic             trigger test panic\n");
    kprintf("  reboot            reboot through keyboard controller\n");
    kprintf("  halt              halt CPU\n");
    kprintf("  echo TEXT         print TEXT\n");
}

static void log_writer(const char *line) { kprintf("%s", line); }

static bool print_dir_entry_ext4(const ext4_dirent_t *entry, void *ctx) {
    (void)ctx;
    kprintf("  ino=%u type=%u %s\n", entry->inode, entry->file_type, entry->name);
    return true;
}

static bool print_dir_entry_vfs(const vfs_dirent_t *entry, void *ctx) {
    (void)ctx;
    const char *type = entry->type == VFS_NODE_DIR ? "dir" : entry->type == VFS_NODE_DEV ? "dev" : "file";
    kprintf("  %-4s ino=%u size=%llu %s\n", type, entry->inode, (unsigned long long)entry->size, entry->name);
    return true;
}

static void cmd_disks(void) {
    usize n = block_count();
    kprintf("block devices: %llu\n", (unsigned long long)n);
    for (usize i = 0; i < n; ++i) {
        block_device_t *dev = block_get(i);
        kprintf("%llu: %s sectors=%llu sector_size=%u\n", (unsigned long long)i, dev->name,
                (unsigned long long)dev->sector_count, dev->sector_size);
        mbr_table_t mbr;
        if (mbr_read(dev, &mbr)) {
            for (usize p = 0; p < 4; ++p) {
                if (!mbr.part[p].sector_count) continue;
                kprintf("  part%llu type=%02x boot=%u lba=%u sectors=%u\n", (unsigned long long)(p + 1), mbr.part[p].type,
                        mbr.part[p].bootable ? 1 : 0, mbr.part[p].lba_first, mbr.part[p].sector_count);
            }
        } else {
            kprintf("  no valid MBR\n");
        }
    }
}

static void cmd_ext4(void) {
    for (usize i = 0; i < block_count(); ++i) {
        block_device_t *dev = block_get(i);
        mbr_table_t mbr;
        if (!mbr_read(dev, &mbr)) continue;
        const mbr_partition_t *part = mbr_find_linux(&mbr);
        if (!part) continue;
        ext4_mount_t mnt;
        ext4_status_t st = ext4_mount(dev, part->lba_first, &mnt);
        if (st != EXT4_OK) {
            kprintf("ext4: mount failed on %s: %s\n", dev->name, ext4_status_name(st));
            return;
        }
        kprintf("ext4: mounted %s lba=%u block=%llu groups=%u inodes/group=%u\n",
                dev->name, part->lba_first, (unsigned long long)mnt.block_size, mnt.group_count, mnt.inodes_per_group);
        ext4_inode_disk_t root;
        st = ext4_read_inode(&mnt, EXT4_ROOT_INO, &root);
        if (st != EXT4_OK) { kprintf("ext4: root inode failed: %s\n", ext4_status_name(st)); return; }
        st = ext4_list_dir(&mnt, &root, print_dir_entry_ext4, 0);
        if (st != EXT4_OK) kprintf("ext4: list failed: %s\n", ext4_status_name(st));
        return;
    }
    kprintf("ext4: no Linux MBR partition found\n");
}

static void cmd_ls(const char *arg) {
    const char *path = arg && *arg ? arg : "/";
    vfs_status_t st = vfs_list(path, print_dir_entry_vfs, 0);
    if (st != VFS_OK) kprintf("ls: %s: %s\n", path, vfs_status_name(st));
}

static void cmd_stat(const char *path) {
    if (!path || !*path) { kprintf("usage: stat PATH\n"); return; }
    vfs_stat_t st;
    vfs_status_t r = vfs_stat(path, &st);
    if (r != VFS_OK) { kprintf("stat: %s: %s\n", path, vfs_status_name(r)); return; }
    const char *type = st.type == VFS_NODE_DIR ? "directory" : st.type == VFS_NODE_DEV ? "device" : "file";
    kprintf("%s: type=%s size=%llu mode=%o inode=%u fs=%u\n", path, type,
            (unsigned long long)st.size, st.mode, st.inode, st.fs_id);
}

static void cmd_cat(const char *path) {
    if (!path || !*path) { kprintf("usage: cat PATH\n"); return; }
    u8 buf[CAT_BUF];
    u64 off = 0;
    for (;;) {
        usize got = 0;
        vfs_status_t st = vfs_read(path, off, buf, sizeof(buf), &got);
        if (st != VFS_OK) { kprintf("cat: %s: %s\n", path, vfs_status_name(st)); return; }
        if (!got) break;
        for (usize i = 0; i < got; ++i) console_putc((char)buf[i]);
        off += got;
    }
    kprintf("\n");
}

static void cmd_write(const char *arg, bool create) {
    if (!arg || !*arg) { kprintf(create ? "usage: touch PATH TEXT\n" : "usage: write PATH TEXT\n"); return; }
    char path[VFS_PATH_MAX];
    usize i = 0;
    while (arg[i] && arg[i] != ' ' && i + 1u < sizeof(path)) { path[i] = arg[i]; ++i; }
    if (arg[i] && arg[i] != ' ') { kprintf("%s: path too long\n", create ? "touch" : "write"); return; }
    path[i] = 0;
    while (arg[i] == ' ') ++i;
    const char *text = arg + i;
    vfs_status_t st;
    if (create) st = vfs_create(path, text, strlen(text));
    else {
        usize wrote = 0;
        st = vfs_write(path, 0, text, strlen(text), &wrote);
    }
    if (st != VFS_OK) kprintf("%s: %s\n", create ? "touch" : "write", vfs_status_name(st));
}

static void cmd_syscall(const char *path) {
    if (!path || !*path) path = "/etc/version";
    syscall_result_t r = syscall_dispatch(AURORA_SYS_OPEN, (u64)(uptr)path, 0, 0, 0, 0, 0);
    if (r.error) { kprintf("syscall open failed: %lld\n", (long long)r.error); return; }
    char buf[80];
    syscall_result_t rr = syscall_dispatch(AURORA_SYS_READ, (u64)r.value, (u64)(uptr)buf, sizeof(buf) - 1u, 0, 0, 0);
    if (rr.error) kprintf("syscall read failed: %lld\n", (long long)rr.error);
    else {
        buf[rr.value] = 0;
        kprintf("syscall read(%s): %s\n", path, buf);
    }
    syscall_dispatch(AURORA_SYS_CLOSE, (u64)r.value, 0, 0, 0, 0, 0);
}


static void cmd_fdprobe(const char *path) {
    if (!path || !*path) path = "/";
    syscall_result_t r = syscall_dispatch(AURORA_SYS_OPEN, (u64)(uptr)path, 0, 0, 0, 0, 0);
    if (r.error) { kprintf("fdprobe open failed: %lld\n", (long long)r.error); return; }
    u64 h = (u64)r.value;
    aurora_fdinfo_t info;
    memset(&info, 0, sizeof(info));
    r = syscall_dispatch(AURORA_SYS_FDINFO, h, (u64)(uptr)&info, 0, 0, 0, 0);
    if (r.error) {
        kprintf("fdprobe fdinfo failed: %lld\n", (long long)r.error);
        syscall_dispatch(AURORA_SYS_CLOSE, h, 0, 0, 0, 0, 0);
        return;
    }
    kprintf("fd=%u type=%u offset=%llu size=%llu inode=%u fs=%u path=%s\n", info.handle, info.type,
            (unsigned long long)info.offset, (unsigned long long)info.size, info.inode, info.fs_id, info.path);
    if (info.type == VFS_NODE_DIR) {
        for (u64 i = 0; i < 8; ++i) {
            aurora_dirent_t de;
            r = syscall_dispatch(AURORA_SYS_READDIR, h, i, (u64)(uptr)&de, 0, 0, 0);
            if (r.error || r.value == 0) break;
            kprintf("  [%llu] type=%u ino=%u size=%llu %s\n", (unsigned long long)i, de.type, de.inode,
                    (unsigned long long)de.size, de.name);
        }
    }
    syscall_dispatch(AURORA_SYS_CLOSE, h, 0, 0, 0, 0, 0);
}

static int split_args(char *arg, const char **argv, int max_args) {
    int argc = 0;
    char *p = arg;
    while (p && *p && argc < max_args) {
        while (*p == ' ' || *p == '\t') ++p;
        if (!*p) break;
        argv[argc++] = p;
        while (*p && *p != ' ' && *p != '\t') ++p;
        if (*p) *p++ = 0;
    }
    return argc;
}


#define PIPELINE_MAX_STAGES 4u
#define PIPELINE_MAX_TOKENS 16u

typedef struct shell_pipeline_segment {
    const char *argv[PROCESS_ARG_MAX];
    int argc;
    char path[VFS_PATH_MAX];
    char in_path[VFS_PATH_MAX];
    char out_path[VFS_PATH_MAX];
    char err_path[VFS_PATH_MAX];
} shell_pipeline_segment_t;

static bool shell_copy_token(char *dst, usize dst_size, const char *src) {
    if (!dst || !dst_size || !src || !*src) return false;
    usize n = strnlen(src, dst_size);
    if (n >= dst_size) return false;
    memcpy(dst, src, n + 1u);
    return true;
}

static bool shell_resolve_program(shell_pipeline_segment_t *seg, const char *name) {
    if (!seg || !name || !*name) return false;
    if (strcmp(name, "cat") == 0) name = "stdcat";
    if (strchr(name, '/')) return shell_copy_token(seg->path, sizeof(seg->path), name);
    const char prefix[] = "/bin/";
    usize pn = sizeof(prefix) - 1u;
    usize nn = strlen(name);
    if (pn + nn + 1u > sizeof(seg->path)) return false;
    memcpy(seg->path, prefix, pn);
    memcpy(seg->path + pn, name, nn + 1u);
    return true;
}

static bool parse_pipeline_segment(char *text, shell_pipeline_segment_t *seg) {
    if (!text || !seg) return false;
    memset(seg, 0, sizeof(*seg));
    const char *tokens[PIPELINE_MAX_TOKENS];
    int ntok = split_args(text, tokens, (int)PIPELINE_MAX_TOKENS);
    if (ntok <= 0) return false;
    for (int i = 0; i < ntok; ++i) {
        const char *t = tokens[i];
        if (strcmp(t, "<") == 0 || strcmp(t, ">") == 0 || strcmp(t, "2>") == 0) {
            if (i + 1 >= ntok) return false;
            char *dst = strcmp(t, "<") == 0 ? seg->in_path : strcmp(t, ">") == 0 ? seg->out_path : seg->err_path;
            if (!shell_copy_token(dst, VFS_PATH_MAX, tokens[++i])) return false;
            continue;
        }
        if (t[0] == '>' && t[1]) {
            if (!shell_copy_token(seg->out_path, sizeof(seg->out_path), t + 1)) return false;
            continue;
        }
        if (t[0] == '<' && t[1]) {
            if (!shell_copy_token(seg->in_path, sizeof(seg->in_path), t + 1)) return false;
            continue;
        }
        if (t[0] == '2' && t[1] == '>' && t[2]) {
            if (!shell_copy_token(seg->err_path, sizeof(seg->err_path), t + 2)) return false;
            continue;
        }
        if (seg->argc >= (int)PROCESS_ARG_MAX) return false;
        seg->argv[seg->argc++] = t;
    }
    if (seg->argc <= 0) return false;
    if (!shell_resolve_program(seg, seg->argv[0])) return false;
    seg->argv[0] = seg->path;
    return true;
}

static void release_snapshot_range(u8 *snapshots, usize snapshot_size, usize first, usize count) {
    if (!snapshots || !snapshot_size) return;
    for (usize i = first; i < count; ++i) {
        syscall_release_user_handle_snapshot(snapshots + i * snapshot_size, snapshot_size);
    }
}

static void cmd_pipeline(char *line) {
    char *parts[PIPELINE_MAX_STAGES];
    shell_pipeline_segment_t segs[PIPELINE_MAX_STAGES];
    usize stages = 0;
    char *p = line;
    while (p && stages < PIPELINE_MAX_STAGES) {
        parts[stages++] = p;
        char *bar = strchr(p, '|');
        if (!bar) break;
        *bar = 0;
        p = bar + 1;
    }
    if (strchr(p ? p : "", '|')) { kprintf("pipeline: too many stages (max %u)\n", PIPELINE_MAX_STAGES); return; }
    for (usize i = 0; i < stages; ++i) {
        while (*parts[i] == ' ' || *parts[i] == '\t') ++parts[i];
        if (!parse_pipeline_segment(parts[i], &segs[i])) { kprintf("pipeline: invalid stage %llu\n", (unsigned long long)i); return; }
        if (i > 0 && segs[i].in_path[0]) { kprintf("pipeline: input redirection conflicts with pipe at stage %llu\n", (unsigned long long)i); return; }
        if (i + 1u < stages && segs[i].out_path[0]) { kprintf("pipeline: output redirection conflicts with pipe at stage %llu\n", (unsigned long long)i); return; }
    }
    usize snapshot_size = syscall_user_handle_snapshot_size();
    u8 *snapshots = (u8 *)kmalloc(snapshot_size * stages);
    if (!snapshots) { kprintf("pipeline: no memory\n"); return; }
    memset(snapshots, 0, snapshot_size * stages);
    for (usize i = 0; i < stages; ++i) syscall_prepare_user_handle_snapshot(snapshots + i * snapshot_size, snapshot_size);

    bool ok = true;
    for (usize i = 0; ok && i < stages; ++i) {
        u8 *snap = snapshots + i * snapshot_size;
        if (segs[i].in_path[0] && !syscall_snapshot_open_vfs(snap, snapshot_size, AURORA_STDIN, segs[i].in_path, 0, false)) ok = false;
        if (segs[i].out_path[0] && !syscall_snapshot_open_vfs(snap, snapshot_size, AURORA_STDOUT, segs[i].out_path, 0, true)) ok = false;
        if (segs[i].err_path[0] && !syscall_snapshot_open_vfs(snap, snapshot_size, AURORA_STDERR, segs[i].err_path, 0, true)) ok = false;
    }
    for (usize i = 0; ok && i + 1u < stages; ++i) {
        if (!syscall_snapshot_pipe_between(snapshots + i * snapshot_size, snapshot_size, AURORA_STDOUT,
                                           snapshots + (i + 1u) * snapshot_size, snapshot_size, AURORA_STDIN)) ok = false;
    }
    if (!ok) {
        kprintf("pipeline: fd setup failed\n");
        release_snapshot_range(snapshots, snapshot_size, 0, stages);
        kfree(snapshots);
        return;
    }

    u32 pids[PIPELINE_MAX_STAGES];
    memset(pids, 0, sizeof(pids));
    usize spawned = 0;
    for (; spawned < stages; ++spawned) {
        process_status_t st = process_spawn_async_snapshot(segs[spawned].argv[0], segs[spawned].argc, segs[spawned].argv,
                                                           snapshots + spawned * snapshot_size, snapshot_size, &pids[spawned]);
        if (st != PROC_OK) {
            kprintf("pipeline: spawn %s failed: %s\n", segs[spawned].argv[0], process_status_name(st));
            break;
        }
    }
    if (spawned < stages) {
        release_snapshot_range(snapshots, snapshot_size, spawned, stages);
        kfree(snapshots);
        if (spawned > 0) {
            process_result_t root;
            (void)process_run_until_idle(pids[0], &root);
        }
        return;
    }
    kfree(snapshots);
    process_result_t root;
    memset(&root, 0, sizeof(root));
    bool ran = process_run_until_idle(pids[0], &root);
    if (!ran) { kprintf("pipeline: scheduler did not complete root pid=%u\n", pids[0]); return; }
    for (usize i = 0; i < stages; ++i) {
        process_info_t info;
        if (process_lookup(pids[i], &info)) {
            kprintf("[%u] %s exit=%d state=%u status=%s\n", pids[i], segs[i].argv[0], info.exit_code, info.state, process_status_name((process_status_t)info.status));
        }
    }
}


static void cmd_mv(const char *arg) {
    const char *argv[2];
    int argc = split_args((char *)arg, argv, 2);
    if (argc != 2) { kprintf("usage: mv OLD NEW\n"); return; }
    vfs_status_t st = vfs_rename(argv[0], argv[1]);
    if (st != VFS_OK) kprintf("mv: %s\n", vfs_status_name(st));
}

static void cmd_truncate(const char *arg) {
    const char *argv[2];
    int argc = split_args((char *)arg, argv, 2);
    if (argc != 2) { kprintf("usage: truncate PATH SIZE\n"); return; }
    u64 size = 0;
    if (!parse_u64_dec_checked(argv[1], &size)) { kprintf("truncate: invalid size\n"); return; }
    vfs_status_t st = vfs_truncate(argv[0], size);
    if (st != VFS_OK) kprintf("truncate: %s\n", vfs_status_name(st));
}

static void cmd_run(char *arg) {
    const char *argv[PROCESS_ARG_MAX];
    int argc = split_args(arg, argv, (int)PROCESS_ARG_MAX);
    if (argc <= 0) { kprintf("usage: run PATH [ARGS]\n"); return; }
    process_result_t result;
    process_status_t st = process_exec(argv[0], argc, argv, &result);
    kprintf("process: status=%s pid=%u exit=%d faulted=%u pages=%llu entry=%p\n",
            process_status_name(st), result.pid, result.exit_code, result.faulted ? 1u : 0u,
            (unsigned long long)result.mapped_pages, (void *)(uptr)result.entry);
    if (result.faulted) {
        kprintf("  fault vector=%llu rip=%p addr=%p\n", (unsigned long long)result.fault_vector,
                (void *)(uptr)result.fault_rip, (void *)(uptr)result.fault_addr);
    }
}

static void cmd_spawn(char *arg) {
    const char *argv[PROCESS_ARG_MAX];
    int argc = split_args(arg, argv, (int)PROCESS_ARG_MAX);
    if (argc <= 0) { kprintf("usage: spawn PATH [ARGS]\n"); return; }
    u32 pid = 0;
    process_result_t result;
    process_status_t st = process_spawn(argv[0], argc, argv, &pid, &result);
    kprintf("spawn: status=%s pid=%u exit=%d faulted=%u asid=%llu\n", process_status_name(st), pid, result.exit_code, result.faulted ? 1u : 0u, (unsigned long long)result.address_space_generation);
}

static void cmd_wait(const char *arg) {
    if (!arg || !*arg) { kprintf("usage: wait PID\n"); return; }
    u64 raw = 0;
    if (!parse_u64_dec_checked(arg, &raw)) { kprintf("wait: invalid PID\n"); return; }
    process_info_t info;
    if (!process_wait((u32)raw, &info)) { kprintf("wait: no completed process %llu\n", (unsigned long long)raw); return; }
    kprintf("pid=%u state=%u exit=%d status=%s asid=%llu name=%s\n", info.pid, info.state, info.exit_code, process_status_name((process_status_t)info.status), (unsigned long long)info.address_space_generation, info.name);
}


static void cmd_qspawn(char *arg) {
    const char *argv[PROCESS_ARG_MAX];
    int argc = split_args(arg, argv, (int)PROCESS_ARG_MAX);
    if (argc <= 0) { kprintf("usage: qspawn PATH [ARGS]\n"); return; }
    u32 job = 0;
    if (!scheduler_enqueue(argv[0], argc, argv, &job)) { kprintf("qspawn: enqueue failed\n"); return; }
    kprintf("queued job=%u path=%s\n", job, argv[0]);
}

static void cmd_runq(const char *arg) {
    u32 max = 0;
    if (arg && *arg) {
        u64 raw = 0;
        if (!parse_u64_dec_checked(arg, &raw) || raw > 0xffffffffu) { kprintf("runq: invalid count\n"); return; }
        max = (u32)raw;
    }
    u32 ran = scheduler_run_ready(max);
    kprintf("scheduler dispatched %u job(s)\n", ran);
}

static void cmd_preempt(void) {
    aurora_preemptinfo_t pi;
    if (!scheduler_preempt_info(&pi)) { kprintf("preempt: unavailable\n"); return; }
    kprintf("preempt: enabled=%u quantum=%u current_pid=%u slice=%u timer=%llu user=%llu kernel=%llu expirations=%llu last_tick=%llu rip=%p\n",
            pi.enabled, pi.quantum_ticks, pi.current_pid, pi.current_slice_ticks,
            (unsigned long long)pi.total_timer_ticks, (unsigned long long)pi.user_ticks,
            (unsigned long long)pi.kernel_ticks, (unsigned long long)pi.total_preemptions,
            (unsigned long long)pi.last_preempt_ticks, (void *)(uptr)pi.last_preempt_rip);
}

static void cmd_elf(const char *path) {
    if (!path || !*path) { kprintf("usage: elf PATH\n"); return; }
    elf_loaded_image_t img;
    elf_status_t st = elf64_load_from_vfs(path, &img);
    if (st != ELF_OK) { kprintf("elf: %s: %s\n", path, elf_status_name(st)); return; }
    kprintf("elf: entry=%p segments=%u\n", (void *)(uptr)img.entry, img.segment_count);
    for (u16 i = 0; i < img.segment_count; ++i) {
        kprintf("  seg%u vaddr=%p mem=%llu file=%llu flags=%x loaded=%p\n", i, (void *)(uptr)img.segments[i].vaddr,
                (unsigned long long)img.segments[i].memsz, (unsigned long long)img.segments[i].filesz,
                img.segments[i].flags, img.segments[i].memory);
    }
    elf64_free_image(&img);
}

static void execute(char *line) {
    while (*line == ' ' || *line == '\t') ++line;
    if (strchr(line, '|')) { cmd_pipeline(line); return; }
    char *arg = strchr(line, ' ');
    if (arg) { *arg++ = 0; while (*arg == ' ') ++arg; }
    else arg = line + strlen(line);

    if (line[0] == 0) return;
    if (strcmp(line, "help") == 0) print_help();
    else if (strcmp(line, "clear") == 0) console_clear();
    else if (strcmp(line, "uname") == 0) kprintf("%s\n", AURORA_UNAME_TEXT);
    else if (strcmp(line, "ticks") == 0) kprintf("ticks=%llu\n", (unsigned long long)pit_ticks());
    else if (strcmp(line, "mem") == 0) memory_dump_map();
    else if (strcmp(line, "heap") == 0) kmem_dump();
    else if (strcmp(line, "vmm") == 0) vmm_dump();
    else if (strcmp(line, "ktest") == 0) ktest_run_all();
    else if (strcmp(line, "logs") == 0) log_dump_ring(log_writer);
    else if (strcmp(line, "disks") == 0) cmd_disks();
    else if (strcmp(line, "mounts") == 0) vfs_dump_mounts();
    else if (strcmp(line, "ls") == 0) cmd_ls(arg);
    else if (strcmp(line, "stat") == 0) cmd_stat(arg);
    else if (strcmp(line, "cat") == 0) cmd_cat(arg);
    else if (strcmp(line, "write") == 0) cmd_write(arg, false);
    else if (strcmp(line, "touch") == 0) cmd_write(arg, true);
    else if (strcmp(line, "mkdir") == 0) { vfs_status_t st = vfs_mkdir(arg); if (st != VFS_OK) kprintf("mkdir: %s\n", vfs_status_name(st)); }
    else if (strcmp(line, "rm") == 0) { vfs_status_t st = vfs_unlink(arg); if (st != VFS_OK) kprintf("rm: %s\n", vfs_status_name(st)); }
    else if (strcmp(line, "mv") == 0) cmd_mv(arg);
    else if (strcmp(line, "truncate") == 0) cmd_truncate(arg);
    else if (strcmp(line, "ext4") == 0) cmd_ext4();
    else if (strcmp(line, "syscall") == 0) cmd_syscall(arg);
    else if (strcmp(line, "fdprobe") == 0) cmd_fdprobe(arg);
    else if (strcmp(line, "run") == 0) cmd_run(arg);
    else if (strcmp(line, "lastproc") == 0) process_dump_last();
    else if (strcmp(line, "procs") == 0) process_dump_table();
    else if (strcmp(line, "spawn") == 0) cmd_spawn(arg);
    else if (strcmp(line, "wait") == 0) cmd_wait(arg);
    else if (strcmp(line, "qspawn") == 0) cmd_qspawn(arg);
    else if (strcmp(line, "runq") == 0) cmd_runq(arg);
    else if (strcmp(line, "sched") == 0) scheduler_dump();
    else if (strcmp(line, "preempt") == 0) cmd_preempt();
    else if (strcmp(line, "ps") == 0) task_dump();
    else if (strcmp(line, "schedtest") == 0) kprintf("task selftest: %s\n", task_selftest() ? "ok" : "failed");
    else if (strcmp(line, "elf") == 0) cmd_elf(arg);
    else if (strcmp(line, "userbins") == 0) kprintf("user bins: %s\n", user_bins_selftest() ? "ok" : "failed");
    else if (strcmp(line, "panic") == 0) PANIC("manual panic requested from shell");
    else if (strcmp(line, "reboot") == 0) cpu_reboot();
    else if (strcmp(line, "halt") == 0) cpu_halt_forever();
    else if (strcmp(line, "echo") == 0) kprintf("%s\n", arg);
    else kprintf("unknown command: %s\n", line);
}

void shell_run(void) {
    char line[LINE_MAX];
    usize len = 0;
    kprintf("\n%s. Type 'help'. Try: ls /, cat /etc/motd, ktest.\n> ", AURORA_CLI_BANNER);
    for (;;) {
        char c;
        if (!keyboard_getc(&c)) {
            cpu_sti();
            cpu_hlt();
            continue;
        }
        if (c == '\n') {
            console_putc('\n');
            line[len] = 0;
            execute(line);
            len = 0;
            kprintf("> ");
        } else if (c == '\b') {
            if (len > 0) { --len; console_putc('\b'); }
        } else if (len + 1 < LINE_MAX && c >= 32 && c <= 126) {
            line[len++] = c;
            console_putc(c);
        }
    }
}
