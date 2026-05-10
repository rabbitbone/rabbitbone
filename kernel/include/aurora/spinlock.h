#ifndef AURORA_SPINLOCK_H
#define AURORA_SPINLOCK_H
#include <aurora/types.h>
#if defined(__cplusplus)
extern "C" {
#endif

typedef struct spinlock {
    volatile u32 locked;
} spinlock_t;

static inline void spinlock_init(spinlock_t *lock) { if (lock) lock->locked = 0; }
static inline void spin_lock(spinlock_t *lock) {
    while (__sync_lock_test_and_set(&lock->locked, 1u)) {
        while (lock->locked) __asm__ volatile("pause");
    }
}
static inline bool spin_try_lock(spinlock_t *lock) {
    return __sync_lock_test_and_set(&lock->locked, 1u) == 0;
}
static inline void spin_unlock(spinlock_t *lock) {
    __sync_lock_release(&lock->locked);
}

static inline u64 irq_save(void) {
#if defined(AURORA_HOST_TEST)
    return 0;
#else
    u64 flags;
    __asm__ volatile("pushfq; popq %0; cli" : "=r"(flags) :: "memory");
    return flags;
#endif
}

static inline void irq_restore(u64 flags) {
#if defined(AURORA_HOST_TEST)
    (void)flags;
#else
    __asm__ volatile("pushq %0; popfq" :: "r"(flags) : "memory", "cc");
#endif
}

static inline u64 spin_lock_irqsave(spinlock_t *lock) {
    u64 flags = irq_save();
    spin_lock(lock);
    return flags;
}

static inline void spin_unlock_irqrestore(spinlock_t *lock, u64 flags) {
    spin_unlock(lock);
    irq_restore(flags);
}

#if defined(__cplusplus)
}
#endif
#endif
