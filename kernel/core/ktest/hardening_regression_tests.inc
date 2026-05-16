typedef struct ktest_log_probe_ctx {
    const char *needle;
    bool found;
    bool unsafe_control;
    u32 lines;
} ktest_log_probe_ctx_t;

static void ktest_log_probe_writer(const char *line, void *ctx) {
    ktest_log_probe_ctx_t *p = (ktest_log_probe_ctx_t *)ctx;
    if (!line || !p) return;
    ++p->lines;
    if (p->needle && strstr(line, p->needle)) p->found = true;
    for (usize i = 0; line[i]; ++i) {
        unsigned char c = (unsigned char)line[i];
        if ((c < 0x20u && c != '\n' && c != '\t') || c == 0x7fu) p->unsafe_control = true;
    }
}

static void ktest_vappend_proxy(rabbitbone_buf_out_t *out, const char *fmt, ...) {
    __builtin_va_list ap;
    __builtin_va_start(ap, fmt);
    rabbitbone_buf_vappendf(out, fmt, ap);
    __builtin_va_end(ap);
}

static bool ktest_build_many_component_path(char *out, usize cap, u32 count) {
    if (!out || cap == 0) return false;
    out[0] = 0;
    for (u32 i = 0; i < count; ++i) {
        if (strlcat(out, "/a", cap) >= cap) return false;
    }
    return true;
}

static void test_hardening_paths_and_format(void) {
    char out[VFS_PATH_MAX];
    char many[VFS_PATH_MAX];
    memset(out, 0, sizeof(out));
    memset(many, 0, sizeof(many));

    check(ktest_build_many_component_path(many, sizeof(many), 40u) && path_normalize(many, out, sizeof(out)) && strcmp(out, many) == 0,
          "path normalizer keeps valid paths with more than 32 components");
    strlcpy(out, "/stale", sizeof(out));
    check(!path_normalize("/../../escape", out, sizeof(out)) && out[0] == 0,
          "path normalizer rejects root escape and clears stale output");
    strlcpy(out, "/stale", sizeof(out));
    check(!path_join("/a", "../../escape", out, sizeof(out)) && out[0] == 0,
          "path join rejects leaf escaping above root and clears stale output");
    check(path_join("/a/b", "../../escape", out, sizeof(out)) && strcmp(out, "/escape") == 0,
          "path join permits relative leaf to climb to filesystem root without escaping it");
    strlcpy(out, "/stale", sizeof(out));
    check(!path_normalize("/bad\\name", out, sizeof(out)) && out[0] == 0,
          "path normalizer rejects backslash component bytes");
    const char bad_control[] = { '/', 'b', 'a', 'd', '/', 'x', (char)0x1f, 0 };
    strlcpy(out, "/stale", sizeof(out));
    check(!path_normalize(bad_control, out, sizeof(out)) && out[0] == 0,
          "path normalizer rejects control component bytes");
    strlcpy(out, "/stale", sizeof(out));
    check(!path_normalize("/bad\177name", out, sizeof(out)) && out[0] == 0,
          "path normalizer rejects DEL component bytes");

    const char *cursor = "/ok/bad\\component";
    char component[VFS_NAME_MAX];
    check(path_next_component(&cursor, component, sizeof(component)) && strcmp(component, "ok") == 0,
          "path_next_component returns first valid component");
    strlcpy(component, "stale", sizeof(component));
    check(!path_next_component(&cursor, component, sizeof(component)) && component[0] == 0,
          "path_next_component rejects unsafe later component and clears output");

    char dst[12];
    memset(dst, 'Z', sizeof(dst));
    check(strlcpy(dst, "rabbitbone", sizeof(dst)) == 10u && strcmp(dst, "rabbitbone") == 0,
          "strlcpy reports source length and terminates destination");
    check(strlcat(dst, "+os", sizeof(dst)) == 13u && strcmp(dst, "rabbitbone+") == 0,
          "strlcat reports attempted length and truncates safely");
    memzero_explicit(dst, sizeof(dst));
    bool wiped = true;
    for (usize i = 0; i < sizeof(dst); ++i) if (dst[i] != 0) wiped = false;
    check(wiped, "memzero_explicit wipes caller buffer");

    char fmtbuf[32];
    rabbitbone_buf_out_t bo;
    rabbitbone_buf_init(&bo, fmtbuf, sizeof(fmtbuf));
    rabbitbone_buf_appendf(&bo, "%064u", 7u);
    check(fmtbuf[sizeof(fmtbuf) - 1u] == 0 && strlen(fmtbuf) == sizeof(fmtbuf) - 1u,
          "bounded formatter fills caller buffer without tiny temp truncation");
    bo.used = sizeof(fmtbuf) + 100u;
    rabbitbone_buf_append_raw(&bo, "x");
    check(bo.used == sizeof(fmtbuf) - 1u && fmtbuf[sizeof(fmtbuf) - 1u] == 0,
          "bounded formatter clamps corrupted used before raw append");
    rabbitbone_buf_init(&bo, fmtbuf, sizeof(fmtbuf));
    ktest_vappend_proxy(&bo, "%s:%u:%x", "vappend", 42u, 0x2au);
    check(strcmp(fmtbuf, "vappend:42:2a") == 0, "bounded formatter vappend path matches appendf semantics");
}

static void test_hardening_logs_and_kctl(void) {
    static const char kmsg_payload[] = { 'k', 't', 'e', 's', 't', '-', 's', 'a', 'n', 'i', 't', 'i', 'z', 'e', ':', 'a', (char)1, 'b', (char)0x7f, '\t', 'c', 0 };
    usize wrote = 0;
    check(vfs_write("/dev/kmsg", 0, kmsg_payload, sizeof(kmsg_payload) - 1u, &wrote) == VFS_OK && wrote == sizeof(kmsg_payload) - 1u,
          "/dev/kmsg accepts user log payload for sanitization test");
    ktest_log_probe_ctx_t kmsg = { "ktest-sanitize:a?b?\tc", false, false, 0 };
    log_dump_ring_ctx(ktest_log_probe_writer, &kmsg);
    check(kmsg.found && !kmsg.unsafe_control && kmsg.lines > 0u,
          "log ring dump ctx exposes sanitized /dev/kmsg line without unsafe control bytes");

    check(!log_line_should_emit_serial(LOG_INFO, "[INFO] ktest: [ ok    ] clean pass\n"),
          "serial VM log filter suppresses info/pass lines");
    check(log_line_should_emit_serial(LOG_WARN, "[WARN] ktest: [ skip  ] skipped case\n"),
          "serial VM log filter keeps warnings and skips");
    check(log_line_should_emit_serial(LOG_ERROR, "[ERROR] ktest: [ fail  ] failing case\n"),
          "serial VM log filter keeps errors and fails");
    check(log_line_should_emit_serial(LOG_INFO, "[INFO] ktest: [ ambiguous ] unclear case\n"),
          "serial VM log filter keeps ambiguous attention markers");
    check(log_formatted_line_should_emit_serial("[ERROR] ktest: [ result] suite: passed=1 failed=1 skipped=0\n"),
          "panic VM log ring filter keeps formatted error lines");

    log_write(LOG_INFO, "ktest-hardening", "ctx-dump-%u", 304u);
    ktest_log_probe_ctx_t direct = { "ctx-dump-304", false, false, 0 };
    log_dump_ring_ctx(ktest_log_probe_writer, &direct);
    check(direct.found && direct.lines >= kmsg.lines,
          "log_dump_ring_ctx carries caller-local context through dump");

    log_write(LOG_INFO, "ktest-hardening", "kctl-tail-marker-%u", 304u);
    char *logs = (char *)kmalloc(RABBITBONE_KCTL_OUT_MAX);
    if (logs) {
        memset(logs, 0, RABBITBONE_KCTL_OUT_MAX);
        syscall_result_t r = syscall_dispatch(RABBITBONE_SYS_KCTL, RABBITBONE_KCTL_OP_LOGS, (u64)(uptr)logs, RABBITBONE_KCTL_OUT_MAX, 0, 0, 0);
        check(!r.error && strstr(logs, "kctl-tail-marker-304") != 0,
              "kctl logs returns caller-local sanitized log output");
        check(!r.error && strstr(logs, "logs:\n") == logs,
              "kctl logs keeps stable header while tailing recent ring entries");
        kfree(logs);
    } else {
        skip("kctl logs caller-local output allocation");
    }
}

static void test_hardening_syscall_edges(void) {
    syscall_result_t r;
    r = syscall_dispatch(RABBITBONE_SYS_KILL, 0x80000000ull, RABBITBONE_SIGTERM, 0, 0, 0, 0);
    check(r.value == -1 && r.error == VFS_ERR_INVAL, "syscall kill rejects raw 32-bit INT_MIN pid");
    r = syscall_dispatch(RABBITBONE_SYS_KILL, 0xffffffff80000000ull, RABBITBONE_SIGTERM, 0, 0, 0, 0);
    check(r.value == -1 && r.error == VFS_ERR_INVAL, "syscall kill rejects sign-extended INT_MIN pid");
    r = syscall_dispatch(RABBITBONE_SYS_KILL, 0xffffffffull, RABBITBONE_SIGTERM, 0, 0, 0, 0);
    check(r.error != VFS_ERR_INVAL, "syscall kill accepts raw 32-bit -1 pid shape before backend resolution");
    check(r.error == VFS_ERR_NOENT || r.error == VFS_ERR_PERM || r.error == 0,
          "syscall kill maps syntactically valid raw 32-bit -1 to backend result, not validation failure");
    r = syscall_dispatch(RABBITBONE_SYS_KILL, 0xfffffffeull, RABBITBONE_SIGTERM, 0, 0, 0, 0);
    check(r.error != VFS_ERR_INVAL, "syscall kill accepts raw 32-bit negative process-group pid shape before backend resolution");
    check(r.error == VFS_ERR_NOENT || r.error == VFS_ERR_PERM || r.error == 0,
          "syscall kill maps syntactically valid raw 32-bit process group to backend result, not validation failure");
    r = syscall_dispatch(RABBITBONE_SYS_SIGACTION, RABBITBONE_SIGUSR1, 0, 0, 0, 0, 0);
    check(r.value == -1 && r.error == VFS_ERR_INVAL, "syscall sigaction rejects no-op null act and oldact");
    r = syscall_dispatch(RABBITBONE_SYS_SIGPROCMASK, RABBITBONE_SIG_SETMASK, 0, 0, 0, 0, 0);
    check(r.value == -1 && r.error == VFS_ERR_INVAL, "syscall sigprocmask rejects no-op null set and oldset");
    r = syscall_dispatch(RABBITBONE_SYS_MPROTECT, 0x100000ull, 4096u, RABBITBONE_PROT_WRITE | RABBITBONE_PROT_EXEC, 0, 0, 0);
    check(r.value == -1 && r.error == VFS_ERR_INVAL, "syscall mprotect rejects W+X before VM layer");
    r = syscall_dispatch(RABBITBONE_SYS_MUNMAP, 0x100001ull, 4096u, 0, 0, 0, 0);
    check(r.value == -1 && r.error == VFS_ERR_INVAL, "syscall munmap rejects unaligned address before VM layer");
    r = syscall_dispatch(RABBITBONE_SYS_MMAP, 0, 4096u, RABBITBONE_PROT_READ, RABBITBONE_MAP_ANON, (u64)-1ll, 0);
    check(r.value == -1 && r.error == VFS_ERR_INVAL, "syscall mmap rejects missing private/shared mode before VM layer");
    r = syscall_dispatch(RABBITBONE_SYS_CHMOD, (u64)(uptr)"/tmp", 02000u, 0, 0, 0, 0);
    check(r.value == -1 && r.error == VFS_ERR_INVAL, "syscall chmod rejects setgid bit in Rust/C boundary policy");
    r = syscall_dispatch(RABBITBONE_SYS_CRED, RABBITBONE_CRED_OP_GET, 0, 0, 0, 0, 0);
    check(r.value == -1 && r.error == VFS_ERR_INVAL, "syscall cred get requires output pointer");
    r = syscall_dispatch(RABBITBONE_SYS_CRED, RABBITBONE_CRED_OP_LOGIN, (u64)(uptr)"root", 0, 0, 0, 0);
    check(r.value == -1 && r.error == VFS_ERR_INVAL, "syscall cred login requires password pointer");
    r = syscall_dispatch(RABBITBONE_SYS_CRED, RABBITBONE_CRED_OP_SET_EUID, 0, 0x100000000ull, 0, 0, 0);
    check(r.value == -1 && r.error == VFS_ERR_INVAL, "syscall cred set_euid rejects values wider than u32");
    r = syscall_dispatch(RABBITBONE_SYS_SUDO, RABBITBONE_SUDO_OP_VALIDATE, 0, 0x80000000ull, 0, 0, 0);
    check(r.value == -1 && r.error == VFS_ERR_INVAL, "syscall sudo validate rejects unknown flags");
    r = syscall_dispatch(RABBITBONE_SYS_SUDO, RABBITBONE_SUDO_OP_SET_TIMEOUT, 0, RABBITBONE_SUDO_MAX_TTL_TICKS + 1u, 0, 0, 0);
    check(r.value == -1 && r.error == VFS_ERR_INVAL, "syscall sudo set_timeout rejects TTL above ABI cap");
    r = syscall_dispatch(RABBITBONE_SYS_MMAP, 0, 4096u, RABBITBONE_PROT_READ, RABBITBONE_MAP_ANON | RABBITBONE_MAP_PRIVATE | RABBITBONE_MAP_SHARED, (u64)-1ll, 0);
    check(r.value == -1 && r.error == VFS_ERR_INVAL, "syscall mmap rejects both private and shared flags");
    r = syscall_dispatch(RABBITBONE_SYS_MMAP, 123u, 4096u, RABBITBONE_PROT_READ, RABBITBONE_MAP_ANON | RABBITBONE_MAP_PRIVATE | RABBITBONE_MAP_FIXED, (u64)-1ll, 0);
    check(r.value == -1 && r.error == VFS_ERR_INVAL, "syscall mmap rejects unaligned MAP_FIXED address");
    r = syscall_dispatch(RABBITBONE_SYS_KCTL, RABBITBONE_KCTL_OP_MAX, 0, 0, 0, 0, 0);
    check(r.value == -1 && r.error == VFS_ERR_INVAL, "syscall kctl rejects op at ABI max");
    r = syscall_dispatch(RABBITBONE_SYS_KCTL, RABBITBONE_KCTL_OP_MEM, 0, 128u, 0, 0, 0);
    check(r.value == -1 && r.error == VFS_ERR_INVAL, "syscall kctl rejects missing output buffer for non-terminal op");
    char *kctl_mem = (char *)kmalloc(512u);
    if (kctl_mem) {
        memset(kctl_mem, 0, 512u);
        r = syscall_dispatch(RABBITBONE_SYS_KCTL, RABBITBONE_KCTL_OP_MEM, (u64)(uptr)kctl_mem, 512u, 0, 0, 0);
        check(!r.error && strstr(kctl_mem, "mem:") != 0 && strstr(kctl_mem, "free_frames=") != 0,
              "syscall kctl mem writes bounded heap-backed diagnostics");
        kfree(kctl_mem);
    } else {
        skip("syscall kctl mem diagnostic allocation");
    }
}

static void test_hardening_regressions(void) {
    suite_begin("hardening/regression coverage");
    test_hardening_paths_and_format();
    test_hardening_logs_and_kctl();
    test_hardening_syscall_edges();
    suite_end();
}
