#ifndef POCKETJS_AIC_GC_H
#define POCKETJS_AIC_GC_H

#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>

/* Frame-boundary policy, shared with the native probe. No allocation, heap
 * walk or QuickJS calls here. The caller measures live bytes again AFTER GC.
 * The clock is a wrapping 32-bit host tick counter. */
typedef struct {
  size_t live;
  uint32_t tick;
  bool initialized;
} pocketjs_aic_gc_t;

static inline bool pocketjs_aic_gc_due(const pocketjs_aic_gc_t *state,
                                      uint32_t tick, uint32_t tick_hz,
                                      size_t live, size_t available, bool idle) {
  if (!state->initialized) return true;
  const bool pressure = available <= 1024U * 1024U;
  const size_t growth = pressure ? 32U * 1024U : 128U * 1024U;
  if (live > state->live && live - state->live >= growth) return true;
  return (idle || pressure) && (uint32_t)(tick - state->tick) >= tick_hz * 2U;
}

static inline void pocketjs_aic_gc_collected(pocketjs_aic_gc_t *state,
                                            uint32_t tick, size_t live) {
  state->live = live;
  state->tick = tick;
  state->initialized = true;
}

#endif
