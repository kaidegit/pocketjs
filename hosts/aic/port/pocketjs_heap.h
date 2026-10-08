#ifndef POCKETJS_HEAP_H
#define POCKETJS_HEAP_H

#include <rtthread.h>

/** Claim the private pool for every heap_caps allocation. Call once from the
 * init thread before the guest task starts; idempotent. On failure the
 * heap_caps calls fall back to the system heap. */
void pocketjs_heap_init(void);

/** Usage of the private pool when it is active, of the system heap when not. */
void pocketjs_heap_stats(rt_size_t *total, rt_size_t *used,
                         rt_size_t *max_used);

#endif /* POCKETJS_HEAP_H */
