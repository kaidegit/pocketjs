/* heap_caps implementation over a dedicated TLSF pool.
 *
 * The reused components allocate through heap_caps: the guest realm wants
 * PSRAM first, the Rust allocator asks for alignment. QuickJS frees and
 * reallocates in bursts, and the RT-Thread memheap's best-fit search walks a
 * free list that fragmentation grows without bound — allocation cost climbs
 * until every frame stalls. pocketjs_heap_init() instead claims one large
 * block from the system heap and manages it with TLSF (O(1) alloc/free,
 * fragmentation stays local and bounded), so the churn never reaches the
 * system heap. The guest's own heap_limit (4 MB) fails before the pool
 * (4.15 MB) does.
 *
 * pocketjs_heap_init() must run before the guest task starts; it is
 * idempotent. If the big block cannot be claimed, every call falls back to
 * rt_malloc. Each block stores a three-slot header just below the aligned
 * address: the back distance to the raw block, the total charged bytes, and
 * the payload size, which heap_caps_free and heap_caps_realloc read back. */
#include <rtconfig.h>
#include <rtthread.h>

#include "esp_heap_caps.h"
#include "pocketjs_heap.h"
#include "tlsf.h"

#define POCKETJS_MIN_ALIGN (sizeof(void *) * 2)
#define POCKETJS_POOL_BYTES (4150U * 1024U)
#define POCKETJS_HEADER_SLOTS 3U

static tlsf_t pocketjs_tlsf;
static rt_bool_t pocketjs_pool_ready = RT_FALSE;
static rt_bool_t pocketjs_pool_failed = RT_FALSE;
static rt_size_t pocketjs_live_bytes;
static rt_size_t pocketjs_live_peak;

void pocketjs_heap_init(void) {
  if (pocketjs_pool_ready || pocketjs_pool_failed) {
    return;
  }
  rt_uint8_t *block = rt_malloc(POCKETJS_POOL_BYTES);
  if (block == RT_NULL) {
    pocketjs_pool_failed = RT_TRUE;
    rt_kprintf("[PocketJS] heap: %u-byte private pool unavailable, "
               "falling back to the system heap\n",
               (unsigned)POCKETJS_POOL_BYTES);
    return;
  }
  pocketjs_tlsf = tlsf_create_with_pool(block, POCKETJS_POOL_BYTES);
  if (pocketjs_tlsf == NULL) {
    rt_free(block);
    pocketjs_pool_failed = RT_TRUE;
    rt_kprintf("[PocketJS] heap: TLSF pool init failed\n");
    return;
  }
  pocketjs_pool_ready = RT_TRUE;
  rt_kprintf("[PocketJS] heap: %u-byte TLSF pool ready\n",
             (unsigned)POCKETJS_POOL_BYTES);
}

void pocketjs_heap_stats(rt_size_t *total, rt_size_t *used,
                         rt_size_t *max_used) {
  if (pocketjs_pool_ready) {
    *total = POCKETJS_POOL_BYTES;
    *used = (rt_size_t)pocketjs_live_bytes;
    *max_used = (rt_size_t)pocketjs_live_peak;
    return;
  }
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
  rt_uint8_t *raw;
  if (pocketjs_pool_ready) {
    raw = tlsf_malloc(pocketjs_tlsf, charged);
    if (raw == RT_NULL) {
      return RT_NULL;
    }
    pocketjs_live_bytes += charged;
    if (pocketjs_live_bytes > pocketjs_live_peak) {
      pocketjs_live_peak = pocketjs_live_bytes;
    }
  } else {
    raw = rt_malloc(charged);
    if (raw == RT_NULL) {
      return RT_NULL;
    }
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
  const size_t charged = ((size_t *)ptr)[-2];
  rt_uint8_t *raw = (rt_uint8_t *)ptr - back;
  if (pocketjs_pool_ready) {
    tlsf_free(pocketjs_tlsf, raw);
    pocketjs_live_bytes -= charged;
    return;
  }
  rt_free(raw);
}
