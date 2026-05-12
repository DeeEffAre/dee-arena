#ifndef ARENA_H
#define ARENA_H

#include <stddef.h>
#include <stdint.h>

#define DEFAULT_ARENA_CHUNK_SIZE 1024 * 64

typedef struct ArenaChunk {
  struct ArenaChunk *next;
  size_t capacity;
  size_t used;
  uint8_t data[];
} ArenaChunk;

typedef struct {
  size_t total_capacity;
  size_t total_used;
  size_t chunk_count;
} ArenaStats;

#ifdef ARENA_SINGLE_THREAD

// Chained Arena
typedef struct {
  ArenaChunk *first;
  ArenaChunk *last;
  size_t chunk_size;
} Arena;

void arena_init(Arena *arena, size_t chunk_size);
void *arena_alloc(Arena *arena, size_t size);
void arena_free(Arena *arena);
void arena_reset(Arena *arena);
void arena_get_stats(Arena *arena, ArenaStats *stats);

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

#else // ARENA_SINGLE THREAD

#include <pthread.h>
#include <stdatomic.h>

// Chained Arena
typedef struct {
  ArenaChunk *first;
  ArenaChunk *last;
  size_t chunk_size;
  pthread_mutex_t mutex_lock;
} Arena;

void arena_init(Arena *arena, size_t chunk_size);
void *arena_alloc(Arena *arena, size_t size);
void arena_free(Arena *arena);
void arena_reset(Arena *arena);
void arena_get_stats(Arena *arena, ArenaStats *stats);

// Fixed Arena
typedef struct {
  uint8_t *buf;
  size_t capacity;
  atomic_size_t curr;
  int owns_buffer;
} ArenaFixed;

void arena_fixed_init_with_external_buffer(ArenaFixed *arena,
                                           void *external_buffer, size_t size);
void arena_fixed_init(ArenaFixed *arena, size_t size);
void *arena_fixed_alloc(ArenaFixed *arena, size_t size);
void arena_fixed_reset(ArenaFixed *arena);
void arena_fixed_free(ArenaFixed *arena);

#endif // ARENA_SINGLE THREAD

#endif // !ARENA_H

#ifdef ARENA_IMPLEMENTATION

#include <stddef.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>

#ifndef ARENA_ALIGNMENT
#define ARENA_ALIGNMENT 16
#endif

#define ARENA_ALIGN_CUSTOM(size, alignment)                                    \
  (((size) + (alignment) - 1) & ~((alignment) - 1))

#define ARENA_ALIGN(size) ARENA_ALIGN_CUSTOM(size, ARENA_ALIGNMENT)

static ArenaChunk *create_chunk(size_t size) {
  size_t total_size = sizeof(ArenaChunk) + size;
  ArenaChunk *chunk = (ArenaChunk *)malloc(total_size);
  if (!chunk) {
    return NULL;
  }

  chunk->next = NULL;
  chunk->capacity = size;
  chunk->used = 0;
  return chunk;
}

#ifdef ARENA_SINGLE_THREAD

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
  if (arena->buf) {
    arena->capacity = size;
    arena->curr = 0;
    arena->owns_buffer = 1;
  }
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

void arena_init(Arena *arena, size_t chunk_size) {
  arena->first = NULL;
  arena->last = NULL;

  arena->chunk_size = (chunk_size > 0) ? chunk_size : DEFAULT_ARENA_CHUNK_SIZE;
}

void *arena_alloc(Arena *arena, size_t size) {
  size_t aligned_size = ARENA_ALIGN(size);
  size_t size_to_allocate =
      aligned_size > arena->chunk_size ? aligned_size : arena->chunk_size;

  if (arena->last == NULL) {
    ArenaChunk *chunk = create_chunk(size_to_allocate);
    if (!chunk) {
      fprintf(stderr, "Out of memory\n");
      return NULL;
    }

    arena->first = chunk;
    arena->last = chunk;
  }

  // current chunk has enough capacity
  if (arena->last->capacity - arena->last->used >= aligned_size) {
    void *ptr = &arena->last->data[arena->last->used];
    arena->last->used += aligned_size;
    return ptr;
  }

  // current chunk has not enough capacity look for an existing empty chunk with
  // enough capacity
  ArenaChunk *next = arena->last->next;
  while (next != NULL) {
    if (next->capacity >= aligned_size) {
      arena->last = next;
      void *ptr = &arena->last->data[arena->last->used];
      arena->last->used += aligned_size;
      return ptr;
    }
    next = next->next;
  }

  // no existing chunk has enough capacity, create a new one
  ArenaChunk *new_chunk = create_chunk(size_to_allocate);
  if (!new_chunk) {
    fprintf(stderr, "Out of memory\n");
    return NULL;
  }

  arena->last->next = new_chunk;
  arena->last = new_chunk;

  void *ptr = &arena->last->data[arena->last->used];
  arena->last->used += aligned_size;
  return ptr;
}

void arena_free(Arena *arena) {
  ArenaChunk *curr_chunk = arena->first;
  while (curr_chunk) {
    ArenaChunk *next_chunk = curr_chunk->next;
    free(curr_chunk);
    curr_chunk = next_chunk;
  }

  arena->first = NULL;
  arena->last = NULL;
}

void arena_reset(Arena *arena) {
  ArenaChunk *curr_chunk = arena->first;
  while (curr_chunk) {
    curr_chunk->used = 0;
    curr_chunk = curr_chunk->next;
  }

  arena->last = arena->first;
}

void arena_get_stats(Arena *arena, ArenaStats *stats) {
  size_t total_capacity = 0;
  size_t total_used = 0;
  size_t chunk_count = 0;
  ArenaChunk *curr_chunk = arena->first;
  while (curr_chunk) {
    chunk_count += 1;
    total_capacity += curr_chunk->capacity;
    total_used += curr_chunk->used;
    curr_chunk = curr_chunk->next;
  }

  stats->total_used = total_used;
  stats->total_capacity = total_capacity;
  stats->chunk_count = chunk_count;
}

#else // ARENA_SINGLE_THREAD

#include <pthread.h>

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

  atomic_store_explicit(&arena->curr, 0, memory_order_relaxed);
  arena->owns_buffer = 0;
}

void arena_fixed_init(ArenaFixed *arena, size_t size) {

  arena->buf = (uint8_t *)malloc(size);
  if (arena->buf) {
    arena->capacity = size;
    atomic_store_explicit(&arena->curr, 0, memory_order_relaxed);
    arena->owns_buffer = 1;
  }
}

void *arena_fixed_alloc(ArenaFixed *arena, size_t size) {
  size_t aligned_size = ARENA_ALIGN(size);

  size_t curr = atomic_load_explicit(&arena->curr, memory_order_relaxed);

  while (1) {
    size_t next = curr + aligned_size;
    if (next > arena->capacity) {
      return NULL;
    }
    if (atomic_compare_exchange_strong_explicit(&arena->curr, &curr, next,
                                                memory_order_relaxed,
                                                memory_order_relaxed)) {
      return &arena->buf[curr];
    }
  }
}

void arena_fixed_reset(ArenaFixed *arena) {
  atomic_store_explicit(&arena->curr, 0, memory_order_relaxed);
}

void arena_fixed_free(ArenaFixed *arena) {
  if (arena->owns_buffer) {
    free(arena->buf);
  }
}

void arena_init(Arena *arena, size_t chunk_size) {
  arena->first = NULL;
  arena->last = NULL;

  arena->chunk_size = (chunk_size > 0) ? chunk_size : DEFAULT_ARENA_CHUNK_SIZE;
  pthread_mutex_init(&arena->mutex_lock, NULL);
}

void *arena_alloc(Arena *arena, size_t size) {
  size_t aligned_size = ARENA_ALIGN(size);
  size_t size_to_allocate =
      aligned_size > arena->chunk_size ? aligned_size : arena->chunk_size;

  pthread_mutex_lock(&arena->mutex_lock);
  if (arena->last == NULL) {

    ArenaChunk *chunk = create_chunk(size_to_allocate);
    if (!chunk) {
      fprintf(stderr, "Out of memory\n");
      pthread_mutex_unlock(&arena->mutex_lock);
      return NULL;
    }

    arena->first = chunk;
    arena->last = chunk;
  }

  // current chunk has enough capacity
  if (arena->last->capacity - arena->last->used >= aligned_size) {
    void *ptr = &arena->last->data[arena->last->used];
    arena->last->used += aligned_size;
    pthread_mutex_unlock(&arena->mutex_lock);
    return ptr;
  }

  // current chunk has not enough capacity look for an existing empty chunk with
  // enough capacity
  ArenaChunk *next = arena->last->next;
  while (next != NULL) {
    if (next->capacity >= aligned_size) {
      arena->last = next;
      void *ptr = &arena->last->data[arena->last->used];
      arena->last->used += aligned_size;
      pthread_mutex_unlock(&arena->mutex_lock);
      return ptr;
    }
    next = next->next;
  }

  // no existing chunk has enough capacity, create a new one
  ArenaChunk *new_chunk = create_chunk(size_to_allocate);
  if (!new_chunk) {
    fprintf(stderr, "Out of memory\n");
    pthread_mutex_unlock(&arena->mutex_lock);
    return NULL;
  }

  arena->last->next = new_chunk;
  arena->last = new_chunk;

  void *ptr = &arena->last->data[arena->last->used];
  arena->last->used += aligned_size;
  pthread_mutex_unlock(&arena->mutex_lock);
  return ptr;
}

void arena_free(Arena *arena) {
  pthread_mutex_lock(&arena->mutex_lock);
  ArenaChunk *curr_chunk = arena->first;
  while (curr_chunk) {
    ArenaChunk *next_chunk = curr_chunk->next;
    free(curr_chunk);
    curr_chunk = next_chunk;
  }

  arena->first = NULL;
  arena->last = NULL;
  pthread_mutex_unlock(&arena->mutex_lock);
  pthread_mutex_destroy(&arena->mutex_lock);
}

void arena_reset(Arena *arena) {
  pthread_mutex_lock(&arena->mutex_lock);
  ArenaChunk *curr_chunk = arena->first;
  while (curr_chunk) {
    curr_chunk->used = 0;
    curr_chunk = curr_chunk->next;
  }

  arena->last = arena->first;
  pthread_mutex_unlock(&arena->mutex_lock);
}

void arena_get_stats(Arena *arena, ArenaStats *stats) {
  size_t total_capacity = 0;
  size_t total_used = 0;
  size_t chunk_count = 0;
  pthread_mutex_lock(&arena->mutex_lock);
  ArenaChunk *curr_chunk = arena->first;
  while (curr_chunk) {
    chunk_count += 1;
    total_capacity += curr_chunk->capacity;
    total_used += curr_chunk->used;
    curr_chunk = curr_chunk->next;
  }
  pthread_mutex_unlock(&arena->mutex_lock);

  stats->total_used = total_used;
  stats->total_capacity = total_capacity;
  stats->chunk_count = chunk_count;
}

#endif // ARENA_SINGLE_THREAD

#endif // !ARENA_IMPLEMENTATION
