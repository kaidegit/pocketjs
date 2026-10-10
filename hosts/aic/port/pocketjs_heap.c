/* heap_caps implementation over the RT-Thread system heap.
 *
 * The system heap itself is TLSF (bsp/artinchip/drv/mem/rt_tlsf_heap.c in the
 * SDK, selected with RT_USING_USERHEAP), so the reused components can charge
 * their allocations straight to rt_malloc()/rt_free(): alloc and free answer
 * in bounded time and fragmentation stays local, which is what the old
 * private pool existed to guarantee while the system heap still used the
 * memheap best-fit search. No block is carved out up front; the QuickJS
 * guest, the Rust allocator and the RT-Thread threads share one pool.
 *
 * Each block still stores a three-slot header just below the aligned
 * address: the back distance to the raw block, the total charged bytes, and
 * the payload size, which heap_caps_free and heap_caps_realloc read back.
 * rt_malloc() alignment is only RT_ALIGN_SIZE, so heap_caps_aligned_alloc
 * (the Rust allocator asks for 32-byte and more) performs the alignment
 * itself over the raw block. */
#include <rtconfig.h>
#include <rtthread.h>

#include "esp_heap_caps.h"
#include "pocketjs_heap.h"

#define POCKETJS_MIN_ALIGN (sizeof(void *) * 2)
#define POCKETJS_HEADER_SLOTS 3U

void pocketjs_heap_init(void) {
  /* The unified system heap needs no carve-out; kept as a no-op so host
   * startup ordering and call sites stay unchanged. */
}

void pocketjs_heap_stats(rt_size_t *total, rt_size_t *used,
                         rt_size_t *max_used) {
  rt_memory_info(total, used, max_used);
}

static void *aligned_alloc_internal(size_t alignment, size_t size) {
  if (alignment < POCKETJS_MIN_ALIGN) {
    alignment = POCKETJS_MIN_ALIGN;
  }
  const size_t overhead = POCKETJS_HEADER_SLOTS * sizeof(size_t);
  if (size > SIZE_MAX - (alignment + overhead)) {
    return RT_NULL;
  }
  const size_t charged = size + alignment + overhead;
  rt_uint8_t *raw = rt_malloc(charged);
  if (raw == RT_NULL) {
    return RT_NULL;
  }
  uintptr_t address = (uintptr_t)(raw + overhead);
  address = (address + alignment - 1) & ~(uintptr_t)(alignment - 1);
  ((size_t *)address)[-1] = (size_t)(address - (uintptr_t)raw);
  ((size_t *)address)[-2] = (size_t)charged;
  ((size_t *)address)[-3] = size;
  return (void *)address;
}

void *heap_caps_malloc(size_t size, unsigned caps) {
  (void)caps;
  return aligned_alloc_internal(POCKETJS_MIN_ALIGN, size);
}

void *heap_caps_calloc(size_t count, size_t size, unsigned caps) {
  (void)caps;
  size_t total;
  if (count != 0 && size > SIZE_MAX / count) {
    return RT_NULL;
  }
  total = count * size;
  void *memory = aligned_alloc_internal(POCKETJS_MIN_ALIGN, total);
  if (memory != RT_NULL) {
    rt_memset(memory, 0, total);
  }
  return memory;
}

void *heap_caps_aligned_alloc(size_t alignment, size_t size, unsigned caps) {
  (void)caps;
  return aligned_alloc_internal(alignment, size);
}

void *heap_caps_realloc(void *ptr, size_t size, unsigned caps) {
  if (ptr == RT_NULL) {
    return heap_caps_malloc(size, caps);
  }
  const size_t previous = ((size_t *)ptr)[-3];
  void *fresh = heap_caps_malloc(size, caps);
  if (fresh == RT_NULL) {
    return RT_NULL;
  }
  if (previous != 0U) {
    rt_memcpy(fresh, ptr, previous < size ? previous : size);
  }
  heap_caps_free(ptr);
  return fresh;
}

void heap_caps_free(void *ptr) {
  if (ptr == RT_NULL) {
    return;
  }
  const size_t back = ((size_t *)ptr)[-1];
  rt_uint8_t *raw = (rt_uint8_t *)ptr - back;
  rt_free(raw);
}
