#ifndef AURORA_RINGBUF_H
#define AURORA_RINGBUF_H
#include <aurora/types.h>
#if defined(__cplusplus)
extern "C" {
#endif

typedef struct ringbuf {
    u8 *data;
    usize capacity;
    usize head;
    usize tail;
    usize count;
} ringbuf_t;

void ringbuf_init(ringbuf_t *rb, void *storage, usize capacity);
bool ringbuf_push(ringbuf_t *rb, u8 value);
bool ringbuf_pop(ringbuf_t *rb, u8 *out);
usize ringbuf_write(ringbuf_t *rb, const void *data, usize size);
usize ringbuf_read(ringbuf_t *rb, void *data, usize size);
usize ringbuf_peek(const ringbuf_t *rb, void *data, usize size);
void ringbuf_clear(ringbuf_t *rb);
usize ringbuf_size(const ringbuf_t *rb);
usize ringbuf_capacity(const ringbuf_t *rb);
usize ringbuf_space(const ringbuf_t *rb);
bool ringbuf_empty(const ringbuf_t *rb);
bool ringbuf_full(const ringbuf_t *rb);
bool ringbuf_selftest(void);

#if defined(__cplusplus)
}
#endif
#endif
