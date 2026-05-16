#ifndef RABBITBONE_SMP_H
#define RABBITBONE_SMP_H
#include <rabbitbone/types.h>
#include <rabbitbone/arch/cpu.h>
#if defined(__cplusplus)
extern "C" {
#endif

#define SMP_MAX_CPUS 16u
#define SMP_TRAMPOLINE_BASE 0x7000ull
#define SMP_TRAMPOLINE_MAX_SIZE 4096u
#define SMP_AP_STACK_SIZE (64u * 1024u)
#define SMP_IPI_WAKE_VECTOR 240u
#define SMP_IPI_RESCHEDULE_VECTOR 241u
#define SMP_CPU_MASK_ALL 0xffffffffu
typedef u32 smp_cpumask_t;
#define SMP_CPUMASK_NONE ((smp_cpumask_t)0u)
#define SMP_CPUMASK_CPU(cpu) ((smp_cpumask_t)(1u << (cpu)))
#define SMP_CPUMASK_VALID ((SMP_MAX_CPUS >= 32u) ? 0xffffffffu : ((1u << SMP_MAX_CPUS) - 1u))
#define SMP_XCALL_F_ASYNC 0x00000001u
#define SMP_XCALL_F_ALLOW_IRQ_CONTEXT 0x00000002u
#define SMP_XCALL_F_ALLOW_IRQS_DISABLED 0x00000004u
#define SMP_XCALL_F_ALLOW_NESTED 0x00000008u
#define SMP_XCALL_F_FORCE_UNICAST 0x00000010u
#define SMP_XCALL_F_IPI_IMMEDIATE 0x00000020u
#define SMP_WAKE_REASON_XCALL 0x00000001u
#define SMP_WAKE_REASON_RESCHEDULE 0x00000002u
#define SMP_WAKE_REASON_TIMER 0x00000004u
#define SMP_WAKE_REASON_SPURIOUS 0x80000000u
#define SMP_XCALL_LATENCY_BUCKETS 6u

#define SMP_BOOT_STAGE_NONE 0u
#define SMP_BOOT_STAGE_STACK_ALLOCATED 1u
#define SMP_BOOT_STAGE_MAILBOX_READY 2u
#define SMP_BOOT_STAGE_TRAMPOLINE_WRITABLE 3u
#define SMP_BOOT_STAGE_TRAMPOLINE_COPIED 4u
#define SMP_BOOT_STAGE_TRAMPOLINE_PATCHED 5u
#define SMP_BOOT_STAGE_TRAMPOLINE_LOCKED 6u
#define SMP_BOOT_STAGE_INIT_SENT 7u
#define SMP_BOOT_STAGE_SIPI1_SENT 8u
#define SMP_BOOT_STAGE_SIPI2_SENT 9u
#define SMP_BOOT_STAGE_ENTERED_C 10u
#define SMP_BOOT_STAGE_CPU_INIT 11u
#define SMP_BOOT_STAGE_GDT_READY 12u
#define SMP_BOOT_STAGE_STACKS_READY 13u
#define SMP_BOOT_STAGE_PERCPU_READY 14u
#define SMP_BOOT_STAGE_APIC_READY 15u
#define SMP_BOOT_STAGE_TIMER_READY 16u
#define SMP_BOOT_STAGE_ONLINE 17u

#define SMP_BOOT_ERROR_NONE 0u
#define SMP_BOOT_ERROR_BAD_CPU 1u
#define SMP_BOOT_ERROR_DISABLED_CPU 2u
#define SMP_BOOT_ERROR_STACK_ALLOC 3u
#define SMP_BOOT_ERROR_TRAMPOLINE_SIZE 4u
#define SMP_BOOT_ERROR_TRAMPOLINE_PROTECT_WRITE 5u
#define SMP_BOOT_ERROR_TRAMPOLINE_OFFSET 6u
#define SMP_BOOT_ERROR_TRAMPOLINE_CR3_HIGH 7u
#define SMP_BOOT_ERROR_TRAMPOLINE_PROTECT_EXEC 8u
#define SMP_BOOT_ERROR_INIT_IPI 9u
#define SMP_BOOT_ERROR_SIPI1 10u
#define SMP_BOOT_ERROR_SIPI2 11u
#define SMP_BOOT_ERROR_TIMEOUT 12u
#define SMP_BOOT_ERROR_DYNAMIC_STACKS 13u
#define SMP_BOOT_ERROR_APIC_LOCAL_INIT 14u


typedef void (*smp_call_fn_t)(u32 cpu_id, void *ctx);

typedef enum smp_cpu_state {
    SMP_CPU_POSSIBLE = 0,
    SMP_CPU_PRESENT = 1,
    SMP_CPU_OFFLINE = 2,
    SMP_CPU_BOOTING = 3,
    SMP_CPU_ONLINE = 4,
    SMP_CPU_ACTIVE = 5,
    SMP_CPU_DYING = 6,
    SMP_CPU_DEAD = 7,
    SMP_CPU_FAILED = 8,
} smp_cpu_state_t;

typedef struct smp_percpu_state smp_percpu_state_t;

typedef struct smp_percpu_state {
    smp_percpu_state_t *self;
    u32 logical_id;
    u32 apic_id;
    volatile u32 installed;
    volatile u32 online;
    volatile u32 interrupt_depth;
    volatile u32 preempt_depth;
    volatile u32 xcall_depth;
    volatile u64 generation;
    volatile u64 fast_id_hits;
    volatile u64 apic_fallbacks;
    void *current_process;
    void *current_address_space;
    volatile uptr current_cr3;
    volatile u64 tlb_generation_seen;
} smp_percpu_state_t;

typedef struct smp_cpu_info {
    u32 logical_id;
    u32 acpi_id;
    u32 apic_id;
    u32 package_id;
    u32 core_id;
    u32 thread_id;
    bool bsp;
    bool enabled;
    bool started;
    u32 state;
    u32 boot_attempts;
    u32 boot_failures;
    volatile u32 boot_stage;
    volatile u32 boot_error;
    volatile u32 boot_generation_seen;
    volatile u32 boot_sipi_count;
    volatile u64 boot_wait_rounds;
    volatile u64 boot_tsc_delta;
    u64 boot_cr3;
    u64 boot_entry;
    u64 boot_trampoline_base;
    u64 boot_trampoline_size;
    u64 stack_base;
    u64 stack_top;
    volatile u64 heartbeat;
    volatile u64 work_executed;
    volatile u64 idle_entries;
    volatile u64 idle_wakeups;
    volatile u64 idle_hlt_entries;
    volatile u64 idle_hlt_wakeups;
    volatile u64 ipi_wakeups;
    volatile u64 reschedule_ipi_wakeups;
    volatile u64 empty_ipis;
    volatile u64 idle_poll_spins;
    volatile u32 idle_polling;
    volatile u32 wake_reason_mask;
    volatile u64 wake_reason_xcall;
    volatile u64 wake_reason_reschedule;
    volatile u64 wake_reason_timer;
    volatile u64 wake_reason_spurious;
    volatile u64 active_poll_until_ns;
    volatile u32 last_ipi_vector;
    volatile u32 last_wake_reason;
    volatile u64 last_ipi_ticks;
    volatile u64 last_ipi_tsc;
    volatile u64 last_xcall_seq;
    volatile u32 last_xcall_src_cpu;
    volatile u32 last_xcall_flags;
    volatile uptr last_xcall_fn;
    volatile uptr last_xcall_ctx;
    volatile u64 last_xcall_queued_ns;
    volatile u64 last_xcall_start_ns;
    volatile u64 last_xcall_finish_ns;
    volatile u64 slow_xcall_items;
    volatile u64 queue_enqueued;
    volatile u64 queue_executed;
    volatile u64 queue_sync_executed;
    volatile u64 queue_async_executed;
    volatile u64 queue_sync_bypasses;
    volatile u64 queue_ipi_executed;
    volatile u64 queue_max_wait_ns;
    volatile u64 queue_max_exec_ns;
    volatile u64 local_timer_ticks;
    volatile u64 local_timer_user_ticks;
    volatile u64 local_timer_kernel_ticks;
    volatile u64 local_timer_eois;
    volatile u64 tlb_flushes;
    volatile u64 tlb_skips;
    volatile u64 tlb_last_generation;
    volatile u64 interrupt_entries;
    volatile u64 interrupt_exits;
    volatile u32 interrupt_depth;
    volatile u32 interrupt_max_depth;
    volatile u64 interrupt_depth_anomalies;
    volatile u64 timer_cpu_id_mismatches;
    volatile u32 queue_depth;
    volatile u32 queue_high_water;
    volatile u32 idle;
    volatile u32 draining;
    volatile u64 anomalies;
    cpu_arch_state_t arch_state;
} smp_cpu_info_t;

typedef struct smp_info {
    bool initialized;
    bool bootstrap_only;
    bool startup_attempted;
    bool background_service;
    u32 cpu_count;
    u32 enabled_cpu_count;
    u32 started_cpu_count;
    u32 failed_cpu_count;
    u32 bsp_apic_id;
    smp_cpumask_t possible_cpu_mask;
    smp_cpumask_t present_cpu_mask;
    smp_cpumask_t enabled_cpu_mask;
    smp_cpumask_t online_cpu_mask;
    smp_cpumask_t active_cpu_mask;
    smp_cpumask_t targetable_cpu_mask;
    smp_cpumask_t offline_cpu_mask;
    smp_cpumask_t booting_cpu_mask;
    smp_cpumask_t dying_cpu_mask;
    smp_cpumask_t dead_cpu_mask;
    smp_cpumask_t failed_cpu_mask;
    smp_cpumask_t bsp_cpu_mask;
    u64 boot_generation;
    u64 boot_prepare_count;
    u64 boot_trampoline_lock_count;
    u64 boot_timeouts;
    u64 boot_ipi_failures;
    u64 boot_last_failed_cpu;
    u64 total_cross_calls;
    u64 total_cross_call_failures;
    u64 total_cross_call_ns;
    u64 max_cross_call_ns;
    u64 slow_cross_calls;
    u64 xcall_latency_buckets[SMP_XCALL_LATENCY_BUCKETS];
    u64 xcall_timeouts;
    u64 xcall_timeout_reports;
    u64 xcall_last_timeout_cpu;
    u64 xcall_last_timeout_seq;
    uptr xcall_last_timeout_fn;
    uptr xcall_last_timeout_ctx;
    u32 xcall_last_timeout_flags;
    u64 xcall_last_timeout_age_ns;
    u64 async_cross_calls;
    u64 async_call_completions;
    u64 xcall_rejections;
    u64 xcall_reject_irq_context;
    u64 xcall_reject_irq_disabled;
    u64 xcall_reject_nested;
    u64 xcall_local_inline;
    u64 xcall_remote_enqueued;
    u64 total_call_items;
    u64 total_call_item_failures;
    u64 queue_requeues;
    u64 queue_enqueue_failures;
    u64 queue_max_depth;
    u64 queue_sync_executed;
    u64 queue_async_executed;
    u64 queue_sync_bypasses;
    u64 queue_ipi_executed;
    u64 tlb_shootdowns;
    u64 tlb_shootdown_failures;
    u64 tlb_pages;
    u64 tlb_max_ns;
    u64 tlb_targeted_shootdowns;
    u64 tlb_broadcast_shootdowns;
    u64 tlb_cpu_skips;
    u64 tlb_generation_updates;
    u64 tlb_last_generation;
    u32 tlb_last_target_mask;
    u32 tlb_last_completed_mask;
    u32 tlb_last_skipped_mask;
    u32 tlb_last_pages;
    u32 tlb_last_full_flush;
    uptr tlb_last_virt;
    usize tlb_last_length;
    uptr tlb_last_target_cr3;
    u64 monitor_passes;
    u64 monitor_anomalies;
    u64 idle_policy_version;
    u64 lifecycle_transitions;
    u64 lifecycle_invalid_transitions;
    u64 xcall_reject_inactive_cpu;
    u64 reschedule_reject_inactive_cpu;
    u64 wake_ipi_sent;
    u64 wake_ipi_failures;
    u64 reschedule_ipi_sent;
    u64 reschedule_ipi_failures;
    u64 arch_init_cpus;
    u64 arch_init_failures;
    u64 arch_xsave_cpus;
    u64 arch_invariant_tsc_cpus;
    bool ap_kernel_tasks_enabled;
    smp_cpu_info_t cpus[SMP_MAX_CPUS];
    smp_percpu_state_t percpu[SMP_MAX_CPUS];
} smp_info_t;

void smp_init_groundwork(void);
void smp_start_all_aps(void);
void smp_background_service_enable(bool enabled);
void smp_ap_kernel_tasks_enable(bool enabled);
void smp_ap_entry(u32 logical_id) RABBITBONE_NORETURN;
void smp_ap_poll_once(void);
void smp_monitor_tick(void);
u32 smp_current_cpu_id(void);
smp_percpu_state_t *smp_current_percpu(void);
bool smp_percpu_ready(void);
void smp_set_current_process(void *process);
void *smp_get_current_process(void);
void smp_set_current_address_space(void *space);
void *smp_get_current_address_space(void);
void smp_set_current_cr3(uptr cr3);
uptr smp_get_current_cr3(void);
u32 smp_started_cpu_count(void);
u32 smp_cpu_state(u32 cpu_id);
bool smp_cpu_is_online(u32 cpu_id);
bool smp_cpu_is_active(u32 cpu_id);
bool smp_cpu_is_targetable(u32 cpu_id);
smp_cpumask_t smp_possible_cpu_mask(void);
smp_cpumask_t smp_present_cpu_mask(void);
smp_cpumask_t smp_enabled_cpu_mask(void);
smp_cpumask_t smp_online_cpu_mask(void);
smp_cpumask_t smp_active_cpu_mask(void);
smp_cpumask_t smp_targetable_cpu_mask(void);
smp_cpumask_t smp_offline_cpu_mask(void);
smp_cpumask_t smp_booting_cpu_mask(void);
smp_cpumask_t smp_dying_cpu_mask(void);
smp_cpumask_t smp_dead_cpu_mask(void);
smp_cpumask_t smp_failed_cpu_mask(void);
smp_cpumask_t smp_started_cpu_mask(void);
bool smp_cpumask_cpu_valid(u32 cpu_id);
bool smp_cpumask_test_cpu(smp_cpumask_t mask, u32 cpu_id);
u32 smp_cpumask_weight(smp_cpumask_t mask);
u32 smp_run_on_mask_ex(u32 cpu_mask, smp_call_fn_t fn, void *ctx, u64 timeout_ticks, u32 flags);
bool smp_run_on_cpu_ex(u32 cpu_id, smp_call_fn_t fn, void *ctx, u64 timeout_ticks, u32 flags);
bool smp_run_on_cpu(u32 cpu_id, smp_call_fn_t fn, void *ctx, u64 timeout_ticks);
u32 smp_run_on_mask(u32 cpu_mask, smp_call_fn_t fn, void *ctx, u64 timeout_ticks);
bool smp_run_on_all(smp_call_fn_t fn, void *ctx, u64 timeout_ticks);
u32 smp_flush_local_call_queue(void);
u32 smp_run_on_mask_async(u32 cpu_mask, smp_call_fn_t fn, void *ctx);
bool smp_run_on_cpu_async(u32 cpu_id, smp_call_fn_t fn, void *ctx);
bool smp_send_reschedule_ipi(u32 cpu_id);
u32 smp_send_reschedule_ipi_mask(u32 cpu_mask);
void smp_note_cpu_wake_reason(u32 cpu_id, u32 reason);
bool smp_tlb_shootdown(uptr virt, usize length, uptr target_cr3, u64 timeout_ticks);
bool smp_tlb_shootdown_mask(uptr virt, usize length, uptr target_cr3, u32 cpu_mask, u64 generation, u64 timeout_ticks);
void smp_note_interrupt_enter(u32 vector, bool from_user);
void smp_note_interrupt_exit(u32 vector);
void smp_emit_diagnostic_snapshot(const char *reason);
const char *smp_xcall_latency_bucket_name(u32 bucket);
const smp_info_t *smp_get_info(void);
void smp_format_status(char *out, usize out_len);
const char *smp_cpu_state_name(u32 state);
const char *smp_boot_stage_name(u32 stage);
const char *smp_boot_error_name(u32 error);
bool smp_selftest(void);
bool smp_booting_identity_selftest(void);

#if defined(__cplusplus)
}
#endif
#endif
