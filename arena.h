#ifndef ARENA_H
#define ARENA_H

#include <stddef.h>
#include <stdint.h>

// Fixed Arena
typedef struct {
  uint8_t *buf;
  size_t capacity;
  size_t curr;
  int owns_buffer;
} ArenaFixed;

void arena_fixed_init_with_external_buffer(ArenaFixed *arena,
                                           void *external_buffer, size_t size);
void arena_fixed_init(ArenaFixed *arena, size_t size);
void *arena_fixed_alloc(ArenaFixed *arena, size_t size);
void arena_fixed_reset(ArenaFixed *arena);
void arena_fixed_free(ArenaFixed *arena);

#endif // !ARENA_H

#ifdef ARENA_IMPLEMENTATION

#include <stddef.h>
#include <stdint.h>
#include <stdlib.h>

#ifndef ARENA_ALIGNMENT
#define ARENA_ALIGNMENT 16
#endif

#define ARENA_ALIGN_CUSTOM(size, alignment)                                    \
  (((size) + (alignment) - 1) & ~((alignment) - 1))

#define ARENA_ALIGN(size) ARENA_ALIGN_CUSTOM(size, ARENA_ALIGNMENT)

// Fixed Arena Functions
void arena_fixed_init_with_external_buffer(ArenaFixed *arena,
                                           void *external_buffer, size_t size) {

  uintptr_t base_addr = (uintptr_t)external_buffer;
  uintptr_t aligned_addr = ARENA_ALIGN(base_addr);
  size_t padding = (size_t)(aligned_addr - base_addr);

  if (padding > size) {
    arena->buf = NULL;
    arena->capacity = 0;
  } else {
    arena->buf = (uint8_t *)aligned_addr;
    arena->capacity = size - padding;
  }

  arena->curr = 0;
  arena->owns_buffer = 0;
}

void arena_fixed_init(ArenaFixed *arena, size_t size) {

  arena->buf = (uint8_t *)malloc(size);
  arena->capacity = size;
  arena->curr = 0;
  arena->owns_buffer = 1;
}

void *arena_fixed_alloc(ArenaFixed *arena, size_t size) {
  size_t aligned_size = ARENA_ALIGN(size);

  if (arena->curr + aligned_size <= arena->capacity) {
    void *ptr = &arena->buf[arena->curr];
    arena->curr += aligned_size;
    return ptr;
  }

  return NULL;
}

void arena_fixed_reset(ArenaFixed *arena) { arena->curr = 0; }

void arena_fixed_free(ArenaFixed *arena) {
  if (arena->owns_buffer) {
    free(arena->buf);
  }
}

#endif // !ARENA_IMPLEMENTATION
