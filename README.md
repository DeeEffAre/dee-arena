# dee-arena
stb style single header Arena Allocator implementation in C

## Features

- **Fixed Arena** — contiguous pre-allocated buffer, O(1) alloc
- **Chained Arena** — grows via linked list of chunks, no fixed capacity
- **Thread-safe** by default (CAS loop for fixed, mutex for chained)
- **Zero-overhead single-thread mode** — define `ARENA_SINGLE_THREAD` before including the header
- 16-byte aligned allocations

## Quickstart

```c
#define ARENA_IMPLEMENTATION
#include "arena.h"

int main(void) {
  // Chained arena — grows as needed
  Arena arena;
  arena_init(&arena, 0);  // 0 = default chunk size (64KB)

  int *a = arena_alloc(&arena, sizeof(int) * 256);
  float *b = arena_alloc(&arena, sizeof(float) * 128);

  arena_reset(&arena);  // re-use all memory without free
  arena_free(&arena);   // free everything

  // Fixed arena — pre-allocated, very fast (single pointer bump)
  ArenaFixed fixed;
  arena_fixed_init(&fixed, 1024 * 1024);

  int *c = arena_fixed_alloc(&fixed, sizeof(int) * 64);

  arena_fixed_reset(&fixed);
  arena_fixed_free(&fixed);

  return 0;
}
```

## Single-thread mode

Thread safety adds overhead (atomic CAS, mutex locks). If you don't need it:

```c
#define ARENA_SINGLE_THREAD
#define ARENA_IMPLEMENTATION
#include "arena.h"
```

No atomics, no pthread — compiles with any C standard library.

## API

### Chained Arena

| Function | Description |
|---|---|
| `arena_init` | Initialize arena with chunk size |
| `arena_alloc` | Allocate aligned memory |
| `arena_reset` | Reset all chunks for re-use |
| `arena_free` | Free all chunks |
| `arena_get_stats` | Get capacity/used/chunk count |

### Fixed Arena

| Function | Description |
|---|---|
| `arena_fixed_init` | Initialize with malloc'd buffer |
| `arena_fixed_init_with_external_buffer` | Use an existing buffer (stack, static, etc.) |
| `arena_fixed_alloc` | Bump allocate |
| `arena_fixed_reset` | Reset allocator position |
| `arena_fixed_free` | Free buffer (only if arena owns it) |
```
