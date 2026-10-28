/* Fixed-size diagnostic counters. No sample buffers or dynamic allocation. */
#ifndef POCKETJS_AIC_PERF_H
#define POCKETJS_AIC_PERF_H
#include <stdint.h>

enum pocketjs_perf_phase {
  PJS_PERF_HIT, PJS_PERF_JS, PJS_PERF_TICK, PJS_PERF_DRAW,
  PJS_PERF_PREPARE, PJS_PERF_RASTER, PJS_PERF_PRESENT, PJS_PERF_VSYNC,
  PJS_PERF_GE_CONVERT, PJS_PERF_GE_WAIT, PJS_PERF_GE_COPY, PJS_PERF_COUNT
};

#ifdef POCKETJS_AIC_DIAG
uint32_t pocketjs_perf_now(void);
void pocketjs_perf_record(unsigned phase, uint32_t started);
void pocketjs_perf_report(uint32_t window_ms);
#define PJS_PERF_START(name) const uint32_t name = pocketjs_perf_now()
#define PJS_PERF_END(phase, name) pocketjs_perf_record(phase, name)
#else
#define PJS_PERF_START(name) ((void)0)
#define PJS_PERF_END(phase, name) ((void)0)
static inline void pocketjs_perf_report(uint32_t window_ms) { (void)window_ms; }
#endif
#endif
