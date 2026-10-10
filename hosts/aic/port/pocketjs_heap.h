#ifndef POCKETJS_HEAP_H
#define POCKETJS_HEAP_H

#include <rtthread.h>

/** No-op kept for startup-order stability: allocations go straight to the
 * unified TLSF system heap (RT_USING_USERHEAP), no pool is claimed. */
void pocketjs_heap_init(void);

/** Usage of the unified system heap. */
void pocketjs_heap_stats(rt_size_t *total, rt_size_t *used,
                         rt_size_t *max_used);

#endif /* POCKETJS_HEAP_H */
