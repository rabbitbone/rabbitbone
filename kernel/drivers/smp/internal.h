#ifndef RABBITBONE_DRIVERS_SMP_INTERNAL_H
#define RABBITBONE_DRIVERS_SMP_INTERNAL_H

#include <rabbitbone/smp.h>
#include <rabbitbone/acpi.h>
#include <rabbitbone/apic.h>
#include <rabbitbone/libc.h>
#include <rabbitbone/console.h>
#include <rabbitbone/log.h>
#include <rabbitbone/format.h>
#include <rabbitbone/kmem.h>
#include <rabbitbone/vmm.h>
#include <rabbitbone/memory.h>
#include <rabbitbone/spinlock.h>
#include <rabbitbone/arch/cpu.h>
#include <rabbitbone/arch/io.h>
#include <rabbitbone/arch/gdt.h>
#include <rabbitbone/arch/idt.h>
#include <rabbitbone/drivers.h>
#include <rabbitbone/task.h>
#include <rabbitbone/scheduler.h>
#include <rabbitbone/timer.h>
#include <rabbitbone/panic.h>

#define SMP_BOOT_WAIT_ROUNDS 250000u
#define SMP_BOOT_WAIT_PAUSES 64u
#define SMP_BOOT_INIT_DELAY_PAUSES 8192u
#define SMP_BOOT_SIPI_DELAY_PAUSES 4096u
#define SMP_BOOT_WAIT_TSC_MIN_DELTA 500000000ull
#define SMP_MONITOR_PERIOD_TICKS 25u
#define SMP_MONITOR_REPORT_PERIOD_TICKS 100u
#define SMP_XCALL_DEFAULT_TIMEOUT_TICKS 100u
#define SMP_XCALL_SLOW_NS 2000000ull
#define SMP_CALL_ITEM_SLOW_NS 1000000ull
#define SMP_AP_ACTIVE_POLL_NS 1000000ull
#define SMP_AP_ACTIVE_POLL_BATCH 96u
#define SMP_CALL_DRAIN_BATCH 64u
#define SMP_CALL_WAIT_REKICK_TICKS 1u
#define SMP_CALL_WAIT_SPIN_LIMIT 10000000u
#define SMP_CALL_CANCEL_SPIN_LIMIT 2000000u
#define SMP_RFLAGS_IF 0x200ull

extern u8 smp_trampoline_start[];
extern u8 smp_trampoline_end[];
extern u32 smp_trampoline_cr3_offset[];
extern u32 smp_trampoline_entry64_offset[];
extern u32 smp_trampoline_stack_top_offset[];
extern u32 smp_trampoline_logical_id_offset[];


typedef struct smp_boot_mailbox {
    volatile u64 generation;
    volatile u32 logical_id;
    volatile u32 apic_id;
    volatile u32 active;
    volatile u32 acked;
    volatile u32 stage;
    volatile u32 error;
    volatile u32 sipi_count;
    volatile u32 reserved;
    volatile u64 cr3;
    volatile u64 entry64;
    volatile u64 stack_top;
    volatile u64 trampoline_base;
    volatile u64 trampoline_size;
    volatile u64 tsc_entered;
} smp_boot_mailbox_t;

typedef struct smp_call_item {
    volatile u64 seq;
    volatile u32 done;
    volatile u32 running;
    volatile u32 failed;
    volatile u32 queued_remote;
    volatile u32 heap_owned;
    u32 flags;
    u32 src_cpu;
    u32 dst_cpu;
    smp_call_fn_t fn;
    void *ctx;
    u64 queued_ns;
    u64 started_ns;
    u64 finished_ns;
    struct smp_call_item *next;
} smp_call_item_t;

typedef struct smp_call_queue {
    spinlock_t lock;
    smp_call_item_t *head;
    smp_call_item_t *tail;
    volatile u32 depth;
    volatile u32 high_water;
    volatile u64 enqueued;
    volatile u64 executed;
    volatile u64 sync_executed;
    volatile u64 async_executed;
    volatile u64 sync_bypasses;
    volatile u64 ipi_executed;
    volatile u64 kicks;
    volatile u64 empty_ipis;
    volatile u64 max_wait_ns;
    volatile u64 max_exec_ns;
    volatile u64 slow_items;
    volatile u64 last_seq;
} smp_call_queue_t;

static smp_info_t smp_info;
static spinlock_t smp_lock;
static smp_call_queue_t smp_call_queues[SMP_MAX_CPUS];
static volatile u64 smp_call_seq;
static smp_boot_mailbox_t smp_boot_mailboxes[SMP_MAX_CPUS];
static volatile bool smp_booting_cpu;
static volatile u32 smp_boot_target_id;
static volatile u32 smp_boot_ack_id;
static volatile u64 smp_boot_ack_generation;
static volatile u64 smp_monitor_last_ticks;
static volatile u64 smp_monitor_last_heartbeat[SMP_MAX_CPUS];
static volatile u64 smp_monitor_last_report_ticks[SMP_MAX_CPUS];
static volatile u64 smp_percpu_generation;
static volatile bool smp_percpu_system_ready;

#define SMP_MSR_GS_BASE 0xc0000101u

static inline void smp_cpu_pause(void) { __asm__ volatile("pause"); }
static inline u64 smp_now_ticks(void) { return pit_ticks(); }
static inline u32 smp_trampoline_offset(const u32 *p) { return p ? __atomic_load_n(p, __ATOMIC_RELAXED) : 0u; }

static u32 smp_current_id_from_apic(void);
static smp_percpu_state_t *smp_current_percpu_fast(void);
static void smp_percpu_install_cpu(u32 cpu_id);
static bool smp_cpu_id_valid(u32 cpu_id);
static bool smp_queue_pending(u32 cpu_id);
static void smp_note_anomaly(u32 cpu_id, const char *msg);
static void smp_recount_locked(void);
static void smp_wake_cpu(u32 cpu_id);
static void smp_record_xcall_duration(u64 elapsed_ns, bool ok);
static void smp_record_ipi_event(u32 cpu_id, u32 vector, u32 reason);
static void smp_report_xcall_timeout(u32 dst_cpu, const smp_call_item_t *item, u64 age_ns);
static void smp_arm_active_poll(smp_cpu_info_t *cpu);
static u32 smp_drain_cpu_queue(u32 cpu_id, u32 budget);
static u32 smp_drain_ipi_immediate_queue(u32 cpu_id, u32 budget);
static bool smp_cancel_queued_call_item(u32 cpu_id, smp_call_item_t *target);
static void smp_wait_stack_item_safe_or_panic(smp_call_item_t *item, u32 cpu_id);
static bool smp_xcall_context_allowed(u32 cpu_mask, u32 flags);
static void smp_call_queue_init(void);
static void smp_boot_set_stage(u32 logical_id, u32 stage);
static void smp_boot_set_error(u32 logical_id, u32 error);
static void smp_boot_mailbox_reset(u32 logical_id, u64 generation);
static void smp_record_cpu_arch_state(u32 cpu_id);

#endif
