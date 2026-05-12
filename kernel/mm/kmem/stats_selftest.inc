void kmem_get_stats(kmem_stats_t *out) {
    if (!out) return;
    u64 flags = spin_lock_irqsave(&kmem_lock);
    *out = stats;
    spin_unlock_irqrestore(&kmem_lock, flags);
}

void kmem_dump(void) {
    kmem_stats_t s;
    kmem_get_stats(&s);
    kprintf("kmem: heap=%llu KiB used=%llu free=%llu allocs=%llu fails=%llu\n",
            (unsigned long long)(s.heap_bytes / 1024),
            (unsigned long long)s.used_bytes,
            (unsigned long long)s.free_bytes,
            (unsigned long long)s.allocation_count,
            (unsigned long long)s.failed_allocations);
    u64 flags = spin_lock_irqsave(&kmem_lock);
    usize i = 0;
    for (kmem_block_t *b = heap_head; b && i < 16u; b = b->next, ++i) {
        kprintf("  block%llu %p size=%llu %s\n", (unsigned long long)i, b,
                (unsigned long long)b->size, b->free ? "free" : "used");
    }
    spin_unlock_irqrestore(&kmem_lock, flags);
}

bool kmem_selftest(void) {
    void *a = kmalloc(24);
    void *b = kmalloc(4096);
    void *c = kcalloc(8, 32);
    void *d = 0;
    void *big1 = 0;
    void *big2 = 0;
    void *probe = 0;
    bool ok = false;

    if (!a || !b || !c) goto cleanup;
    memset(a, 0x11, 24);
    memset(b, 0x22, 4096);
    d = krealloc(a, 128);
    a = 0;
    if (!d) goto cleanup;
    d = krealloc(d, 32);
    if (!d) goto cleanup;

    /* Force at least two heap extensions, then free list-neighbor blocks.
       The allocator must not coalesce them unless their addresses are really adjacent. */
    big1 = kmalloc(90000);
    big2 = kmalloc(90000);
    if (!big1 || !big2 || big1 == big2) goto cleanup;
    memset(big1, 0x33, 90000);
    memset(big2, 0x44, 90000);

    kfree(b); b = 0;
    kfree(c); c = 0;
    kfree(d); d = 0;
    kfree(big1); big1 = 0;
    kfree(big2); big2 = 0;

    probe = kmalloc(1024);
    if (!probe) goto cleanup;
    memset(probe, 0x55, 1024);
    ok = true;

cleanup:
    if (probe) kfree(probe);
    if (big2) kfree(big2);
    if (big1) kfree(big1);
    if (d) kfree(d);
    if (c) kfree(c);
    if (b) kfree(b);
    if (a) kfree(a);
    return ok;
}
