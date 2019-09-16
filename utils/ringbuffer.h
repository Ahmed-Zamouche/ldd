#ifndef UTILS_RINGBUFFER_RINGBUFFER_H_
#define UTILS_RINGBUFFER_RINGBUFFER_H_

#ifdef __cplusplus
extern "C" {
#endif

#ifdef __KERNEL__
#include <linux/types.h>
#else
#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>
#endif

typedef struct ringbuffer_s {
  uint8_t *buffer;
  size_t wr_pos;
  size_t rd_pos;
  size_t size;
  size_t capacity;
} ringbuffer_t;

bool ringbuffer_is_empty(const ringbuffer_t *const);

bool ringbuffer_is_full(const ringbuffer_t *const);

size_t ringbuffer_size(const ringbuffer_t *const);

size_t ringbuffer_capacity(const ringbuffer_t *const);

int ringbuffer_put(ringbuffer_t *, uint8_t);

int ringbuffer_get(ringbuffer_t *, uint8_t *);

void ringbuffer_reset(ringbuffer_t *rb);

void ringbuffer_wrap(ringbuffer_t *, uint8_t *buffer, size_t capacity);

#ifdef __cplusplus
}
#endif

#endif // UTILS_RINGBUFFER_RINGBUFFER_H_
