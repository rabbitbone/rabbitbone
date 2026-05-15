#include <rabbitbone/ringbuf.h>
#include <rabbitbone/libc.h>

static bool ringbuf_valid(const ringbuf_t *rb) {
    return rb && rb->data && rb->capacity != 0 && rb->count <= rb->capacity && rb->head < rb->capacity && rb->tail < rb->capacity;
}

void ringbuf_init(ringbuf_t *rb, void *storage, usize capacity) {
    if (!rb) return;
    rb->data = (u8 *)storage;
    rb->capacity = storage ? capacity : 0;
    rb->head = 0;
    rb->tail = 0;
    rb->count = 0;
}

bool ringbuf_push(ringbuf_t *rb, u8 value) {
    if (!ringbuf_valid(rb) || rb->count >= rb->capacity) return false;
    rb->data[rb->tail] = value;
    rb->tail = (rb->tail + 1u) % rb->capacity;
    ++rb->count;
    return true;
}

bool ringbuf_pop(ringbuf_t *rb, u8 *out) {
    if (!ringbuf_valid(rb) || rb->count == 0) return false;
    if (out) *out = rb->data[rb->head];
    rb->head = (rb->head + 1u) % rb->capacity;
    --rb->count;
    return true;
}

usize ringbuf_write(ringbuf_t *rb, const void *data, usize size) {
    if (!rb || !data) return 0;
    const u8 *p = (const u8 *)data;
    usize n = 0;
    while (n < size && ringbuf_push(rb, p[n])) ++n;
    return n;
}

usize ringbuf_read(ringbuf_t *rb, void *data, usize size) {
    if (!rb || !data) return 0;
    u8 *p = (u8 *)data;
    usize n = 0;
    while (n < size && ringbuf_pop(rb, &p[n])) ++n;
    return n;
}

usize ringbuf_peek(const ringbuf_t *rb, void *data, usize size) {
    if (!ringbuf_valid(rb) || !data) return 0;
    u8 *out = (u8 *)data;
    usize n = size < rb->count ? size : rb->count;
    usize pos = rb->head;
    for (usize i = 0; i < n; ++i) {
        out[i] = rb->data[pos];
        pos = (pos + 1u) % rb->capacity;
    }
    return n;
}

void ringbuf_clear(ringbuf_t *rb) {
    if (!rb) return;
    rb->head = rb->tail = rb->count = 0;
}

usize ringbuf_size(const ringbuf_t *rb) { return ringbuf_valid(rb) ? rb->count : 0; }
usize ringbuf_capacity(const ringbuf_t *rb) { return ringbuf_valid(rb) ? rb->capacity : 0; }
usize ringbuf_space(const ringbuf_t *rb) { return ringbuf_valid(rb) ? rb->capacity - rb->count : 0; }
bool ringbuf_empty(const ringbuf_t *rb) { return !ringbuf_valid(rb) || rb->count == 0; }
bool ringbuf_full(const ringbuf_t *rb) { return ringbuf_valid(rb) && rb->count == rb->capacity; }

bool ringbuf_selftest(void) {
    u8 storage[8];
    ringbuf_t rb;
    ringbuf_init(&rb, storage, sizeof(storage));
    if (!ringbuf_empty(&rb) || ringbuf_capacity(&rb) != 8u) return false;
    const char *a = "abcdef";
    if (ringbuf_write(&rb, a, 6) != 6u) return false;
    if (ringbuf_size(&rb) != 6u || ringbuf_space(&rb) != 2u) return false;
    u8 x = 0;
    if (!ringbuf_pop(&rb, &x) || x != 'a') return false;
    if (!ringbuf_pop(&rb, &x) || x != 'b') return false;
    const char *b = "WXYZ";
    if (ringbuf_write(&rb, b, 4) != 4u) return false;
    if (!ringbuf_full(&rb)) return false;
    char out[9];
    memset(out, 0, sizeof(out));
    if (ringbuf_peek(&rb, out, 8) != 8u) return false;
    if (memcmp(out, "cdefWXYZ", 8) != 0) return false;
    memset(out, 0, sizeof(out));
    if (ringbuf_read(&rb, out, 8) != 8u) return false;
    if (strcmp(out, "cdefWXYZ") != 0) return false;
    return ringbuf_empty(&rb);
}
