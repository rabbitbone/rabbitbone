#define AU_HEAP_ALIGN 16ul
#define AU_HEAP_PAGE 4096ul
#define AU_HEAP_MAGIC 0xa110c0deull

typedef struct au_heap_block {
    au_usize size;
    struct au_heap_block *prev;
    struct au_heap_block *next;
    unsigned int free;
    unsigned int reserved;
    au_u64 magic;
} au_heap_block_t;

static au_heap_block_t *au_heap_head;
static au_heap_block_t *au_heap_tail;

static int au_add_overflow_usize(au_usize a, au_usize b, au_usize *out) {
    return __builtin_add_overflow(a, b, out);
}

static int au_align_up_usize_checked(au_usize v, au_usize align, au_usize *out) {
    if (!align || (align & (align - 1u)) != 0) return 1;
    au_usize biased = 0;
    if (au_add_overflow_usize(v, align - 1u, &biased)) return 1;
    *out = biased & ~(align - 1u);
    return 0;
}

au_i64 au_brk(void *addr) {
    au_result_t r = au_syscall3(AU_SYS_BRK, (au_u64)(unsigned long)addr, 0, 0);
    return r.error ? r.error : r.value;
}

au_i64 au_sbrk(au_i64 increment) {
    au_result_t r = au_syscall3(AU_SYS_SBRK, (au_u64)increment, 0, 0);
    return r.error ? r.error : r.value;
}

void *sbrk(long increment) {
    au_i64 r = au_sbrk((au_i64)increment);
    return r < 0 ? (void *)-1 : (void *)(unsigned long)r;
}

void *au_mmap(void *addr, au_usize length, unsigned int prot, unsigned int flags) {
    au_result_t r = au_syscall6(AU_SYS_MMAP, (au_u64)(unsigned long)addr, (au_u64)length, (au_u64)prot, (au_u64)flags, 0, 0);
    return r.error ? (void *)-1 : (void *)(unsigned long)r.value;
}

au_i64 au_munmap(void *addr, au_usize length) {
    au_result_t r = au_syscall3(AU_SYS_MUNMAP, (au_u64)(unsigned long)addr, (au_u64)length, 0);
    return r.error ? r.error : r.value;
}

au_i64 au_mprotect(void *addr, au_usize length, unsigned int prot) {
    au_result_t r = au_syscall3(AU_SYS_MPROTECT, (au_u64)(unsigned long)addr, (au_u64)length, (au_u64)prot);
    return r.error ? r.error : r.value;
}

void *mmap(void *addr, au_usize length, unsigned int prot, unsigned int flags) {
    return au_mmap(addr, length, prot, flags);
}

au_i64 munmap(void *addr, au_usize length) {
    return au_munmap(addr, length);
}

au_i64 mprotect(void *addr, au_usize length, unsigned int prot) {
    return au_mprotect(addr, length, prot);
}

static au_heap_block_t *au_heap_find_free(au_usize size) {
    for (au_heap_block_t *b = au_heap_head; b; b = b->next) {
        if (b->free && b->size >= size) return b;
    }
    return 0;
}

static void au_heap_split(au_heap_block_t *b, au_usize size) {
    if (!b || b->size < size) return;
    au_usize needed = 0;
    if (au_add_overflow_usize(size, sizeof(au_heap_block_t) + AU_HEAP_ALIGN, &needed)) return;
    if (b->size < needed) return;
    au_heap_block_t *next = (au_heap_block_t *)((unsigned char *)(b + 1) + size);
    next->size = b->size - size - sizeof(au_heap_block_t);
    next->prev = b;
    next->next = b->next;
    next->free = 1u;
    next->reserved = 0;
    next->magic = AU_HEAP_MAGIC;
    if (next->next) next->next->prev = next;
    else au_heap_tail = next;
    b->next = next;
    b->size = size;
}

static void au_heap_coalesce(au_heap_block_t *b) {
    if (!b) return;
    if (b->next && b->next->free && b->next->magic == AU_HEAP_MAGIC) {
        au_heap_block_t *n = b->next;
        b->size += sizeof(au_heap_block_t) + n->size;
        b->next = n->next;
        if (b->next) b->next->prev = b;
        else au_heap_tail = b;
    }
    if (b->prev && b->prev->free && b->prev->magic == AU_HEAP_MAGIC) {
        au_heap_coalesce(b->prev);
    }
}

static au_heap_block_t *au_heap_extend(au_usize size) {
    au_usize total = 0;
    if (au_add_overflow_usize(sizeof(au_heap_block_t), size, &total)) return 0;
    au_usize request = 0;
    if (au_align_up_usize_checked(total, AU_HEAP_PAGE, &request)) return 0;
    void *mem = sbrk((long)request);
    if (mem == (void *)-1) return 0;
    au_heap_block_t *b = (au_heap_block_t *)mem;
    b->size = request - sizeof(au_heap_block_t);
    b->prev = au_heap_tail;
    b->next = 0;
    b->free = 0u;
    b->reserved = 0;
    b->magic = AU_HEAP_MAGIC;
    if (au_heap_tail) au_heap_tail->next = b;
    else au_heap_head = b;
    au_heap_tail = b;
    au_heap_split(b, size);
    return b;
}

void *malloc(au_usize size) {
    if (size == 0) return 0;
    if (au_align_up_usize_checked(size, AU_HEAP_ALIGN, &size)) return 0;
    au_heap_block_t *b = au_heap_find_free(size);
    if (b) {
        b->free = 0u;
        au_heap_split(b, size);
        return (void *)(b + 1);
    }
    b = au_heap_extend(size);
    return b ? (void *)(b + 1) : 0;
}

void free(void *ptr) {
    if (!ptr) return;
    au_heap_block_t *b = ((au_heap_block_t *)ptr) - 1;
    if (b->magic != AU_HEAP_MAGIC) return;
    b->free = 1u;
    au_heap_coalesce(b);
}

void *calloc(au_usize count, au_usize size) {
    au_usize total = 0;
    if (count != 0 && size > (~(au_usize)0) / count) return 0;
    total = count * size;
    void *p = malloc(total);
    if (p) au_memset(p, 0, total);
    return p;
}

void *realloc(void *ptr, au_usize size) {
    if (!ptr) return malloc(size);
    if (size == 0) { free(ptr); return 0; }
    if (au_align_up_usize_checked(size, AU_HEAP_ALIGN, &size)) return 0;
    au_heap_block_t *b = ((au_heap_block_t *)ptr) - 1;
    if (b->magic != AU_HEAP_MAGIC) return 0;
    if (b->size >= size) {
        au_heap_split(b, size);
        return ptr;
    }
    if (b->next && b->next->free && b->next->magic == AU_HEAP_MAGIC && b->size + sizeof(au_heap_block_t) + b->next->size >= size) {
        au_heap_block_t *n = b->next;
        b->size += sizeof(au_heap_block_t) + n->size;
        b->next = n->next;
        if (b->next) b->next->prev = b;
        else au_heap_tail = b;
        au_heap_split(b, size);
        return ptr;
    }
    void *fresh = malloc(size);
    if (!fresh) return 0;
    au_memcpy(fresh, ptr, b->size);
    free(ptr);
    return fresh;
}
