static bool align16_checked(usize n, usize *out) {
    usize t = 0;
    if (__builtin_add_overflow(n, (usize)15u, &t)) return false;
    if (out) *out = t & ~(usize)15u;
    return true;
}

static usize align16(usize n) { usize out = 0; return align16_checked(n, &out) ? out : 0; }
static void *block_payload(kmem_block_t *b) { return (void *)(b + 1); }
static kmem_block_t *payload_block(void *p) { return ((kmem_block_t *)p) - 1; }

static void insert_block(kmem_block_t *b) {
    b->next = 0;
    b->prev = heap_tail;
    if (heap_tail) heap_tail->next = b;
    else heap_head = b;
    heap_tail = b;
}

static u8 *block_end(kmem_block_t *b) {
    return b ? ((u8 *)block_payload(b)) + b->size : 0;
}

static bool blocks_physically_adjacent(kmem_block_t *left, kmem_block_t *right) {
    return left && right && block_end(left) == (u8 *)right;
}

static void merge_forward(kmem_block_t *b) {
    while (b && b->next && b->free && b->next->free && blocks_physically_adjacent(b, b->next)) {
        kmem_block_t *n = b->next;
        usize merged = 0;
        if (__builtin_add_overflow(b->size, sizeof(kmem_block_t), &merged) ||
            __builtin_add_overflow(merged, n->size, &merged)) PANIC("heap block size overflow at %p", b);
        b->size = merged;
        stats.free_bytes += sizeof(kmem_block_t);
        b->next = n->next;
        if (n->next) n->next->prev = b;
        else heap_tail = b;
        n->magic = 0;
    }
}

static void split_block(kmem_block_t *b, usize wanted) {
    wanted = align16(wanted);
    if (!b || !b->free) return;
    usize threshold = 0;
    if (__builtin_add_overflow(wanted, sizeof(kmem_block_t), &threshold) ||
        __builtin_add_overflow(threshold, (usize)KMEM_MIN_SPLIT, &threshold) ||
        b->size < threshold) return;
    u8 *raw = (u8 *)block_payload(b);
    kmem_block_t *rest = (kmem_block_t *)(raw + wanted);
    rest->magic = KMEM_MAGIC_FREE;
    rest->size = b->size - wanted - sizeof(kmem_block_t);
    rest->free = true;
    rest->prev = b;
    rest->next = b->next;
    if (rest->next) rest->next->prev = rest;
    else heap_tail = rest;
    b->next = rest;
    b->size = wanted;
    stats.free_bytes -= sizeof(kmem_block_t);
}

static void split_allocated_block(kmem_block_t *b, usize wanted) {
    wanted = align16(wanted);
    if (!b || b->free) return;
    usize threshold = 0;
    if (__builtin_add_overflow(wanted, sizeof(kmem_block_t), &threshold) ||
        __builtin_add_overflow(threshold, (usize)KMEM_MIN_SPLIT, &threshold) ||
        b->size < threshold) return;
    usize old_size = b->size;
    u8 *raw = (u8 *)block_payload(b);
    kmem_block_t *rest = (kmem_block_t *)(raw + wanted);
    rest->magic = KMEM_MAGIC_FREE;
    rest->size = old_size - wanted - sizeof(kmem_block_t);
    rest->free = true;
    rest->prev = b;
    rest->next = b->next;
    if (rest->next) rest->next->prev = rest;
    else heap_tail = rest;
    b->next = rest;
    b->size = wanted;
    stats.used_bytes -= old_size - wanted;
    stats.free_bytes += rest->size;
    merge_forward(rest);
}

static bool add_heap_pages(usize pages) {
    if (pages == 0 || pages > ((usize)-1) / PAGE_SIZE) return false;
    usize bytes = pages * PAGE_SIZE;
    if (bytes <= sizeof(kmem_block_t)) return false;
#if defined(RABBITBONE_HOST_TEST)
    void *mem = malloc(bytes);
    if (!mem) return false;
#else
    void *mem = memory_alloc_contiguous_pages_below(pages, MEMORY_KERNEL_DIRECT_LIMIT);
#endif
    if (!mem) return false;
    kmem_block_t *b = (kmem_block_t *)mem;
    b->magic = KMEM_MAGIC_FREE;
    b->size = bytes - sizeof(kmem_block_t);
    b->free = true;
    b->prev = b->next = 0;
    insert_block(b);
    stats.heap_bytes += bytes;
    stats.free_bytes += b->size;
    stats.page_bytes_requested += bytes;
    return true;
}

