#include <rabbitbone/arch/gdt.h>
typedef struct sys_kctl_out {
    char *buf;
    usize cap;
    usize len;
} sys_kctl_out_t;

static void kctl_len_add(sys_kctl_out_t *out, usize add) {
    if (!out || add == 0) return;
    if (out->len > (usize)-1 - add) out->len = (usize)-1;
    else out->len += add;
}

static void kctl_append_raw(sys_kctl_out_t *out, const char *s) {
    if (!out || !s) return;
    while (*s) {
        if (out->cap && out->len < out->cap - 1u) out->buf[out->len] = *s;
        kctl_len_add(out, 1u);
        ++s;
    }
}

static void kctl_appendf(sys_kctl_out_t *out, const char *fmt, ...) {
    if (!out || !fmt) return;
    if (!out->cap || out->len >= out->cap) {
        char sink[1];
        __builtin_va_list ap;
        __builtin_va_start(ap, fmt);
        int wrote = kvsnprintf(sink, sizeof(sink), fmt, ap);
        __builtin_va_end(ap);
        if (wrote > 0) kctl_len_add(out, (usize)wrote);
        return;
    }
    usize rem = out->cap - out->len;
    __builtin_va_list ap;
    __builtin_va_start(ap, fmt);
    int wrote = kvsnprintf(out->buf + out->len, rem, fmt, ap);
    __builtin_va_end(ap);
    if (wrote <= 0) return;
    kctl_len_add(out, (usize)wrote);
}

static void kctl_finish(sys_kctl_out_t *out) {
    if (!out || !out->cap) return;
    usize pos = out->len < out->cap ? out->len : out->cap - 1u;
    out->buf[pos] = 0;
}

static void kctl_log_line_writer(const char *line, void *ctx) {
    kctl_append_raw((sys_kctl_out_t *)ctx, line);
}


extern u64 arch_call_on_stack(u64 stack_top, void (*fn)(u64), u64 arg);
extern void arch_call_on_stack_finish(u64 result) RABBITBONE_NORETURN;

#define KCTL_KTEST_STACK_SIZE (64u * 1024u)
#define KCTL_KTEST_TSS_STACK_SIZE (64u * 1024u)
#define KCTL_KTEST_STACK_GUARD 4096u

typedef struct kctl_ktest_stacks {
    void *exec_stack;
    void *tss_stack;
    uptr old_rsp0;
} kctl_ktest_stacks_t;

static bool kctl_stack_top_checked(void *base, usize size, uptr *top_out) {
    if (!base || !top_out) return false;
    uptr sum = 0;
    if (__builtin_add_overflow((uptr)base, (uptr)size, &sum)) return false;
    *top_out = sum & ~(uptr)0xfull;
    return true;
}

static void kctl_run_full_ktest_body(u64 arg) {
    kctl_ktest_stacks_t *stacks = (kctl_ktest_stacks_t *)(uptr)arg;

    /*
     * Full ktest runs nested ring3 programs from a live sudo syscall.  The CPU
     * uses TSS.rsp0 on every ring3->ring0 transition.  If rsp0 still points at
     * the original shell syscall stack, nested user tests overwrite the return
     * frame of the sudo syscall and the kernel later jumps into stack memory
     * after KTEST_STATUS: PASS.  Give nested transitions their own ring0 stack
     * for the whole full-test window.
     */
    if (stacks && stacks->tss_stack) {
        uptr tss_top = 0;
        if (kctl_stack_top_checked(stacks->tss_stack, KCTL_KTEST_TSS_STACK_SIZE, &tss_top)) {
            gdt_set_kernel_stack(tss_top);
        }
    }

    cpu_sti();
    bool ok_all = ktest_run_all();
    cpu_cli();

    if (stacks) {
        gdt_set_kernel_stack(stacks->old_rsp0);
    }
    arch_call_on_stack_finish(ok_all ? 1u : 0u);
    __builtin_unreachable();
}

static bool kctl_run_full_ktest_on_private_stack(void) {
    kctl_ktest_stacks_t stacks;
    memset(&stacks, 0, sizeof(stacks));

    stacks.exec_stack = kmalloc(KCTL_KTEST_STACK_SIZE);
    stacks.tss_stack = kmalloc(KCTL_KTEST_TSS_STACK_SIZE);
    if (!stacks.exec_stack || !stacks.tss_stack) {
        if (stacks.exec_stack) kfree(stacks.exec_stack);
        if (stacks.tss_stack) kfree(stacks.tss_stack);
        return false;
    }
    memset(stacks.exec_stack, 0, KCTL_KTEST_STACK_SIZE);
    memset(stacks.tss_stack, 0, KCTL_KTEST_TSS_STACK_SIZE);
    stacks.old_rsp0 = gdt_current_kernel_stack();

    uptr exec_top = 0;
    if (!kctl_stack_top_checked(stacks.exec_stack, KCTL_KTEST_STACK_SIZE, &exec_top) ||
        exec_top <= KCTL_KTEST_STACK_GUARD) {
        gdt_set_kernel_stack(stacks.old_rsp0);
        kfree(stacks.tss_stack);
        kfree(stacks.exec_stack);
        return false;
    }
    exec_top = (exec_top - KCTL_KTEST_STACK_GUARD) & ~(uptr)0xfull;

    u64 ok = arch_call_on_stack((u64)exec_top, kctl_run_full_ktest_body, (u64)(uptr)&stacks);

    /* Restore again after the bridge returns, covering both normal and
     * accidental body-return paths. */
    if (stacks.old_rsp0) gdt_set_kernel_stack(stacks.old_rsp0);
    kfree(stacks.tss_stack);
    kfree(stacks.exec_stack);
    return ok != 0;
}

static syscall_result_t kctl_copy_text(u64 out_ptr, u64 out_len64, const char *tmp, usize used) {
    if (!out_ptr || out_len64 == 0 || out_len64 > RABBITBONE_KCTL_OUT_MAX || (usize)out_len64 != out_len64) return err(VFS_ERR_INVAL);
    usize cap = (usize)out_len64;
    usize n = used < cap ? used + 1u : cap;
    if (n > cap) n = cap;
    if (!copy_out_buf(out_ptr, tmp, n)) return err(VFS_ERR_INVAL);
    if (used > (usize)0x7fffffffffffffffull) return ok(0x7fffffffffffffffll);
    return ok((i64)used);
}

static bool kctl_root_required(u32 op) {
    switch (op) {
        case RABBITBONE_KCTL_OP_VMM:
        case RABBITBONE_KCTL_OP_KTEST:
        case RABBITBONE_KCTL_OP_LOGS:
        case RABBITBONE_KCTL_OP_PCI:
        case RABBITBONE_KCTL_OP_ACPI:
        case RABBITBONE_KCTL_OP_APIC:
        case RABBITBONE_KCTL_OP_HPET:
        case RABBITBONE_KCTL_OP_SMP:
        case RABBITBONE_KCTL_OP_BOOT:
        case RABBITBONE_KCTL_OP_PANIC:
        case RABBITBONE_KCTL_OP_REBOOT:
        case RABBITBONE_KCTL_OP_HALT:
            return true;
        default:
            return false;
    }
}

syscall_result_t rabbitbone_sys_kctl(u64 op64, u64 out_ptr, u64 out_len64, u64 arg_ptr) {
    if (op64 >= RABBITBONE_KCTL_OP_MAX) return err(VFS_ERR_INVAL);
    u32 op = (u32)op64;
    if (kctl_root_required(op) && !sys_cred_root()) return err(VFS_ERR_PERM);
    bool terminal_op = op == RABBITBONE_KCTL_OP_PANIC || op == RABBITBONE_KCTL_OP_REBOOT || op == RABBITBONE_KCTL_OP_HALT;
    if (!terminal_op) {
        if (!out_ptr || out_len64 == 0 || out_len64 > RABBITBONE_KCTL_OUT_MAX || (usize)out_len64 != out_len64) return err(VFS_ERR_INVAL);
        if (!validate_user_write_range(out_ptr, (usize)out_len64)) return err(VFS_ERR_INVAL);
    }

    if (terminal_op) {
        switch (op) {
            case RABBITBONE_KCTL_OP_PANIC: {
                enum { KCTL_PANIC_MSG_MAX = 160 };
                char *msg = arg_ptr ? copy_string_arg_heap(arg_ptr, KCTL_PANIC_MSG_MAX) : 0;
                if (msg) {
                    PANIC("userspace requested panic: %s", msg);
                }
                PANIC("userspace requested panic");
            }
            case RABBITBONE_KCTL_OP_REBOOT:
                KLOG(LOG_WARN, "kctl", "userspace requested reboot");
                cpu_reboot();
                break;
            case RABBITBONE_KCTL_OP_HALT:
                KLOG(LOG_WARN, "kctl", "userspace requested halt");
                cpu_halt_forever();
                break;
            default:
                break;
        }
        return err(VFS_ERR_INVAL);
    }

    char *tmp = (char *)kmalloc(RABBITBONE_KCTL_OUT_MAX);
    if (!tmp) return err(VFS_ERR_NOMEM);
    memset(tmp, 0, RABBITBONE_KCTL_OUT_MAX);
    sys_kctl_out_t out = { tmp, RABBITBONE_KCTL_OUT_MAX, 0 };
    syscall_result_t result = err(VFS_ERR_INVAL);

    switch (op) {
        case RABBITBONE_KCTL_OP_MEM: {
            memory_stats_t st;
            memset(&st, 0, sizeof(st));
            memory_get_stats(&st);
            kctl_append_raw(&out, "mem:\n");
            kctl_appendf(&out, "  total_usable=%llu\n", (unsigned long long)st.total_usable);
            kctl_appendf(&out, "  used_bytes=%llu\n", (unsigned long long)st.used_bytes);
            kctl_appendf(&out, "  free_bytes=%llu\n", (unsigned long long)st.free_bytes);
            kctl_appendf(&out, "  frame_count=%llu\n", (unsigned long long)st.frame_count);
            kctl_appendf(&out, "  free_frames=%llu\n", (unsigned long long)st.free_frames);
            break;
        }
        case RABBITBONE_KCTL_OP_HEAP: {
            kmem_stats_t st;
            memset(&st, 0, sizeof(st));
            kmem_get_stats(&st);
            kctl_append_raw(&out, "heap:\n");
            kctl_appendf(&out, "  heap_bytes=%llu\n", (unsigned long long)st.heap_bytes);
            kctl_appendf(&out, "  used_bytes=%llu\n", (unsigned long long)st.used_bytes);
            kctl_appendf(&out, "  free_bytes=%llu\n", (unsigned long long)st.free_bytes);
            kctl_appendf(&out, "  allocation_count=%llu\n", (unsigned long long)st.allocation_count);
            kctl_appendf(&out, "  page_bytes_requested=%llu\n", (unsigned long long)st.page_bytes_requested);
            kctl_appendf(&out, "  failed_allocations=%llu\n", (unsigned long long)st.failed_allocations);
            break;
        }
        case RABBITBONE_KCTL_OP_VMM: {
            vmm_stats_t st;
            memset(&st, 0, sizeof(st));
            vmm_get_stats(&st);
            kctl_append_raw(&out, "vmm:\n");
            kctl_appendf(&out, "  mapped_4k_pages=%llu\n", (unsigned long long)st.mapped_4k_pages);
            kctl_appendf(&out, "  mapped_2m_pages=%llu\n", (unsigned long long)st.mapped_2m_pages);
            kctl_appendf(&out, "  table_pages=%llu\n", (unsigned long long)st.table_pages);
            kctl_appendf(&out, "  identity_bytes=%llu\n", (unsigned long long)st.identity_bytes);
            kctl_appendf(&out, "  pml4_physical=%p\n", (void *)st.pml4_physical);
            kctl_appendf(&out, "  current_pml4_physical=%p\n", (void *)st.current_pml4_physical);
            kctl_appendf(&out, "  spaces_created=%llu\n", (unsigned long long)st.spaces_created);
            kctl_appendf(&out, "  spaces_destroyed=%llu\n", (unsigned long long)st.spaces_destroyed);
            kctl_appendf(&out, "  kernel_text_ro=%u\n", st.kernel_text_ro ? 1u : 0u);
            kctl_appendf(&out, "  kernel_rodata_nx=%u\n", st.kernel_rodata_nx ? 1u : 0u);
            kctl_appendf(&out, "  kernel_data_nx=%u\n", st.kernel_data_nx ? 1u : 0u);
            break;
        }
        case RABBITBONE_KCTL_OP_KTEST: {
            bool isolated = false;
            bool need_isolation = process_user_active() || process_async_scheduler_active();
            if (need_isolation) {
                isolated = process_enter_kernel_test_context();
                if (!isolated) { result = err(VFS_ERR_INVAL); goto out; }
            }

            bool ok_all = kctl_run_full_ktest_on_private_stack();

            if (isolated) process_leave_kernel_test_context();
            cpu_sti();

            ktest_totals_t totals;
            memset(&totals, 0, sizeof(totals));
            ktest_get_last_totals(&totals);
            kctl_append_raw(&out, "ktest:\n");
            kctl_append_raw(&out, "  mode=full\n");
            kctl_appendf(&out, "  isolated=%u\n", isolated ? 1u : 0u);
            kctl_appendf(&out, "  result=%s\n", ok_all ? "pass" : "fail");
            kctl_appendf(&out, "  suites=%u\n", totals.suites);
            kctl_appendf(&out, "  passed=%u\n", totals.passed);
            kctl_appendf(&out, "  failed=%u\n", totals.failed);
            kctl_appendf(&out, "  skipped=%u\n", totals.skipped);
            break;
        }
        case RABBITBONE_KCTL_OP_LOGS: {
            kctl_append_raw(&out, "logs:\n");
            usize budget = 0;
            if (out.cap > out.len + 1u) budget = out.cap - out.len - 1u;
            log_dump_ring_tail_ctx(kctl_log_line_writer, &out, budget);
            break;
        }
        case RABBITBONE_KCTL_OP_DISKS:
        case RABBITBONE_KCTL_OP_BLK: {
            usize n = block_count();
            kctl_appendf(&out, "%s: count=%llu\n", op == RABBITBONE_KCTL_OP_BLK ? "blk" : "disks", (unsigned long long)n);
            for (usize i = 0; i < n; ++i) {
                block_device_t *dev = block_get(i);
                if (!dev) continue;
                kctl_appendf(&out, "  block%llu: name=%s driver=%s sectors=%llu sector_size=%u max_transfer=%u flags=0x%x\n",
                             (unsigned long long)i, dev->name, block_driver_name(dev), (unsigned long long)dev->sector_count,
                             dev->sector_size, dev->max_transfer_sectors, dev->flags);
            }
            break;
        }
        case RABBITBONE_KCTL_OP_PCI: {
            pci_format_devices(out.buf, out.cap);
            out.len = strnlen(out.buf, out.cap);
            break;
        }

        case RABBITBONE_KCTL_OP_ACPI: {
            acpi_format_status(out.buf, out.cap);
            out.len = strnlen(out.buf, out.cap);
            break;
        }
        case RABBITBONE_KCTL_OP_APIC: {
            apic_format_status(out.buf, out.cap);
            out.len = strnlen(out.buf, out.cap);
            break;
        }
        case RABBITBONE_KCTL_OP_HPET: {
            hpet_format_status(out.buf, out.cap);
            out.len = strnlen(out.buf, out.cap);
            break;
        }
        case RABBITBONE_KCTL_OP_TIMER: {
            timer_format_status(out.buf, out.cap);
            out.len = strnlen(out.buf, out.cap);
            break;
        }
        case RABBITBONE_KCTL_OP_SMP: {
            smp_format_status(out.buf, out.cap);
            out.len = strnlen(out.buf, out.cap);
            break;
        }
        case RABBITBONE_KCTL_OP_SIGNALS: {
            process_format_signals(out.buf, out.cap);
            out.len = strnlen(out.buf, out.cap);
            break;
        }
        case RABBITBONE_KCTL_OP_JOBS: {
            process_format_jobs(out.buf, out.cap);
            out.len = strnlen(out.buf, out.cap);
            break;
        }
        case RABBITBONE_KCTL_OP_BOOT: {
            bootinfo_format_status(out.buf, out.cap);
            out.len = strnlen(out.buf, out.cap);
            break;
        }
        case RABBITBONE_KCTL_OP_EXT4: {
            vfs_statvfs_t sv;
            memset(&sv, 0, sizeof(sv));
            vfs_status_t vs = vfs_statvfs("/disk0", &sv);
            if (vs != VFS_OK) {
                kctl_appendf(&out, "ext4: /disk0 unavailable: %s\n", vfs_status_name(vs));
            } else {
                kctl_append_raw(&out, "ext4:\n");
                kctl_appendf(&out, "  fs=%s mount=%s fs_id=%u\n", sv.fs_name, sv.mount_path, sv.fs_id);
                kctl_appendf(&out, "  block_size=%llu total_blocks=%llu free_blocks=%llu avail_blocks=%llu\n",
                             (unsigned long long)sv.block_size, (unsigned long long)sv.total_blocks,
                             (unsigned long long)sv.free_blocks, (unsigned long long)sv.avail_blocks);
                kctl_appendf(&out, "  total_inodes=%llu free_inodes=%llu flags=%u max_name=%u\n",
                             (unsigned long long)sv.total_inodes, (unsigned long long)sv.free_inodes,
                             sv.flags, sv.max_name_len);
            }
            break;
        }
        default:
            result = err(VFS_ERR_INVAL);
            goto out;
    }

    kctl_finish(&out);
    result = kctl_copy_text(out_ptr, out_len64, tmp, out.len);
out:
    kfree(tmp);
    return result;
}
