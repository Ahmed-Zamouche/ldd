#include "ringbuffer.h"

/**@Note: ring buffer is lock-free algorithms. when  the producer task place dat
 * into one end of the buffer and consummer task remove data from the other end.
 */

#define ringbuffer_lock(rb)
#define ringbuffer_unlock(rb)

static bool _ringbuffer_is_empty(const ringbuffer_t *const rb) {
  return (rb->size == 0);
}

bool ringbuffer_is_empty(const ringbuffer_t *const rb) {
  bool is_empty;
  ringbuffer_lock(rb);
  is_empty = _ringbuffer_is_empty(rb);
  ringbuffer_unlock(rb);
  return is_empty;
}

static bool _ringbuffer_is_full(const ringbuffer_t *const rb) {
  return (rb->size == rb->capacity);
}

bool ringbuffer_is_full(const ringbuffer_t *const rb) {
  bool is_full;
  ringbuffer_lock(rb);
  is_full = _ringbuffer_is_full(rb);
  ringbuffer_unlock(rb);
  return is_full;
}

size_t ringbuffer_size(const ringbuffer_t *const rb) { return rb->size; }

size_t ringbuffer_capacity(const ringbuffer_t *const rb) {
  return rb->capacity;
}

int ringbuffer_put(ringbuffer_t *rb, uint8_t u8) {

  int res = -1;

  ringbuffer_lock(rb);

  if (!_ringbuffer_is_full(rb)) {
    rb->buffer[rb->wr_pos] = u8;
    rb->wr_pos = (rb->wr_pos + 1) % rb->capacity;
    rb->size++;
    res = 0;
  }

  ringbuffer_unlock(rb);

  return res;
}

int ringbuffer_get(ringbuffer_t *rb, uint8_t *pU8) {
  int res = -1;

  ringbuffer_lock(rb);

  if (!_ringbuffer_is_empty(rb)) {
    *pU8 = rb->buffer[rb->rd_pos];
    rb->rd_pos = (rb->rd_pos + 1) % rb->capacity;
    rb->size--;
    res = 0;
  }

  ringbuffer_unlock(rb);

  return res;
}

void ringbuffer_reset(ringbuffer_t *rb) {
  rb->size = 0;
  rb->wr_pos = 0;
  rb->rd_pos = 0;
}

void ringbuffer_wrap(ringbuffer_t *rb, uint8_t *buffer, size_t capacity) {
  rb->buffer = buffer;
  rb->capacity = capacity;
}
