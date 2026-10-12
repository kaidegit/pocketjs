#ifndef POCKETJS_HEAP_H
#define POCKETJS_HEAP_H

#include <rtthread.h>

/** No-op kept for startup-order stability: allocations go straight to the
 * unified TLSF system heap (RT_USING_USERHEAP), no pool is claimed. */
void pocketjs_heap_init(void);

/** Usage of the unified system heap. */
void pocketjs_heap_stats(rt_size_t *total, rt_size_t *used,
                         rt_size_t *max_used);

/* --- P0 memory diagnostics -------------------------------------------------
 * Every heap_caps_* block funnels through this port, so the accounting below
 * captures the QuickJS guest, the Rust UI allocator and the port itself in
 * one place (system-side rt_malloc users such as threads and lwIP are not
 * included; read the TLSF numbers for those). The counters are always
 * compiled in: two additions and a compare per allocation. */

typedef struct {
  rt_size_t live;   /* charged bytes: payload + align slack + 3-slot header */
  rt_size_t peak;   /* high-water mark of live since boot or last reset */
  rt_size_t blocks; /* live block count */
  rt_uint32_t fails; /* allocator NULLs since boot (ring holds the latest) */
} pocketjs_heap_diag_t;

void pocketjs_heap_diag(pocketjs_heap_diag_t *out);

/** Label the current startup stage so allocation failures record where they
 * happened; the pointer must point at storage that outlives the next call. */
void pocketjs_heap_diag_set_stage(const char *stage);

/** Drop the peak back to live (S6 delta runs such as wlan wifi_on). */
void pocketjs_heap_diag_reset_peak(void);

/** Print the recorded allocation failures ([PJS-DIAG] heap-fail lines). */
void pocketjs_heap_diag_print_fails(void);

#endif /* POCKETJS_HEAP_H */
