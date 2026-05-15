static void kmem_init_locked(void) {
    memset(&stats, 0, sizeof(stats));
    heap_head = heap_tail = 0;
    initialized = true;
    if (!add_heap_pages(KMEM_BOOT_PAGES)) PANIC("kernel heap bootstrap failed");
    KLOG(LOG_INFO, "kmem", "heap initialized: %llu KiB", (unsigned long long)(stats.heap_bytes / 1024));
}

void kmem_init(void) {
    u64 flags = spin_lock_irqsave(&kmem_lock);
    if (!initialized) kmem_init_locked();
    spin_unlock_irqrestore(&kmem_lock, flags);
}

static void *kmalloc_locked(usize size) {
    if (!initialized) kmem_init_locked();
    if (size == 0) return 0;
    if (size > KMALLOC_MAX || !align16_checked(size, &size) || size == 0) { stats.failed_allocations++; return 0; }
retry:
    for (kmem_block_t *b = heap_head; b; b = b->next) {
        if (b->magic != KMEM_MAGIC_FREE && b->magic != KMEM_MAGIC_ALLOC) PANIC("heap metadata corruption at %p", b);
        if (b->free && b->size >= size) {
            split_block(b, size);
            b->free = false;
            b->magic = KMEM_MAGIC_ALLOC;
            stats.used_bytes += b->size;
            stats.free_bytes -= b->size;
            stats.allocation_count++;
            memset(block_payload(b), 0, b->size);
            return block_payload(b);
        }
    }
    usize needed = 0;
    if (__builtin_add_overflow(size, sizeof(kmem_block_t), &needed) ||
        __builtin_add_overflow(needed, (usize)PAGE_SIZE - 1u, &needed)) {
        stats.failed_allocations++;
        return 0;
    }
    usize pages = needed / PAGE_SIZE;
    if (pages < 4u) pages = 4u;
    if (add_heap_pages(pages)) goto retry;
    stats.failed_allocations++;
    KLOG(LOG_ERROR, "kmem", "allocation failed: %llu", (unsigned long long)size);
    return 0;
}

void *kmalloc(usize size) {
    u64 flags = spin_lock_irqsave(&kmem_lock);
    void *p = kmalloc_locked(size);
    spin_unlock_irqrestore(&kmem_lock, flags);
    return p;
}

void *kcalloc(usize count, usize size) {
    if (size && count > ((usize)-1) / size) return 0;
    return kmalloc(count * size);
}

static void kfree_locked(void *ptr) {
    if (!ptr) return;
    kmem_block_t *b = payload_block(ptr);
    if (b->magic != KMEM_MAGIC_ALLOC || b->free) PANIC("double free or invalid free at %p", ptr);
    b->free = true;
    b->magic = KMEM_MAGIC_FREE;
    stats.used_bytes -= b->size;
    stats.free_bytes += b->size;
    if (b->prev && b->prev->free && blocks_physically_adjacent(b->prev, b)) b = b->prev;
    merge_forward(b);
}

void *krealloc(void *ptr, usize new_size) {
    if (!ptr) return kmalloc(new_size);
    if (new_size == 0) { kfree(ptr); return 0; }

    u64 flags = spin_lock_irqsave(&kmem_lock);
    kmem_block_t *b = payload_block(ptr);
    if (b->magic != KMEM_MAGIC_ALLOC || b->free) PANIC("krealloc on invalid pointer %p", ptr);
    if (new_size > KMALLOC_MAX || !align16_checked(new_size, &new_size) || new_size == 0) {
        stats.failed_allocations++;
        spin_unlock_irqrestore(&kmem_lock, flags);
        return 0;
    }
    if (b->size >= new_size) {
        split_allocated_block(b, new_size);
        spin_unlock_irqrestore(&kmem_lock, flags);
        return ptr;
    }
    if (b->next && b->next->free && blocks_physically_adjacent(b, b->next)) {
        usize combined = 0;
        bool combined_ok = !__builtin_add_overflow(b->size, sizeof(kmem_block_t), &combined) &&
                           !__builtin_add_overflow(combined, b->next->size, &combined);
        if (combined_ok && combined >= new_size) {
            kmem_block_t *n = b->next;
            stats.free_bytes -= n->size;
            stats.used_bytes += sizeof(kmem_block_t) + n->size;
            b->size = combined;
            b->next = n->next;
            if (n->next) n->next->prev = b;
            else heap_tail = b;
            n->magic = 0;
            split_allocated_block(b, new_size);
            spin_unlock_irqrestore(&kmem_lock, flags);
            return ptr;
        }
    }
    usize old_size = b->size;
    void *np = kmalloc_locked(new_size);
    if (!np) {
        spin_unlock_irqrestore(&kmem_lock, flags);
        return 0;
    }
    memcpy(np, ptr, old_size);
    kfree_locked(ptr);
    spin_unlock_irqrestore(&kmem_lock, flags);
    return np;
}

void kfree(void *ptr) {
    u64 flags = spin_lock_irqsave(&kmem_lock);
    kfree_locked(ptr);
    spin_unlock_irqrestore(&kmem_lock, flags);
}

char *kstrdup(const char *s) {
    if (!s) return 0;
    usize len_no_nul = strnlen(s, KMALLOC_MAX);
    if (len_no_nul >= KMALLOC_MAX) return 0;
    usize len = 0;
    if (__builtin_add_overflow(len_no_nul, (usize)1u, &len)) return 0;
    char *out = (char *)kmalloc(len);
    if (!out) return 0;
    memcpy(out, s, len);
    return out;
}
