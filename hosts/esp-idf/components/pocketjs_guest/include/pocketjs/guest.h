#pragma once

#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>

#include "esp_err.h"

#ifdef __cplusplus
extern "C" {
#endif

#define POCKETJS_GUEST_ABI_VERSION 1U
#define POCKETJS_GUEST_MAX_TOUCHES 8U

typedef struct pocketjs_guest pocketjs_guest_t;

typedef struct {
  size_t struct_size;
  size_t heap_limit;
  size_t stack_limit;
  bool prefer_psram;
  /** QuickJS malloc-size GC trigger; 0 keeps the engine default (256 kB).
   * Larger values trade peak heap for fewer stop-the-world collections. */
  size_t gc_threshold;
} pocketjs_guest_config_t;

typedef struct {
  size_t struct_size;
  uint32_t buttons;
  /** (x << 8) | y, with both normalized axes in 0..255 and 128 centered. */
  uint32_t analog;
  const uint32_t *touches;
  const int32_t *touch_hits;
  size_t touch_count;
} pocketjs_guest_frame_t;

typedef struct {
  size_t struct_size;
  uint32_t frames;
  uint32_t frame_errors;
  uint32_t jobs;
  size_t heap_used;
  size_t heap_limit;
} pocketjs_guest_stats_t;

/** Engine-side memory breakdown from JS_ComputeMemoryUsage plus the quota. */
typedef struct {
  size_t struct_size;
  size_t malloc_size;  /* live bytes QuickJS charged through the allocator */
  size_t malloc_limit; /* the JS_SetMemoryLimit quota */
  uint64_t malloc_count;
  uint64_t atom_count;
  uint64_t str_count;
  uint64_t obj_count;
  uint64_t shape_count;
  uint64_t prop_count;
  uint64_t js_func_code_size;
} pocketjs_guest_memstats_t;

/** Allocator-side guest accounting: charged bytes live/peak (payload plus
 * the guest allocation header) and the copy-based realloc statistics.
 * Counters require CONFIG_POCKETJS_GUEST_ALLOC_STATS; without it the accessor
 * still succeeds but reports zeros. */
typedef struct {
  size_t struct_size;
  size_t live;
  size_t peak;
  uint32_t allocs;
  uint32_t frees;
  uint32_t reallocs;
  uint32_t realloc_shrinks; /* requests smaller than the previous block */
  size_t realloc_max;       /* largest single realloc request seen */
} pocketjs_guest_allocstats_t;

void pocketjs_guest_config_defaults(pocketjs_guest_config_t *config);

esp_err_t pocketjs_guest_create(const pocketjs_guest_config_t *config,
                                pocketjs_guest_t **out_guest);

/** Evaluate one global IIFE. Surfaces must be installed before this call. */
esp_err_t pocketjs_guest_eval(pocketjs_guest_t *guest, const char *source,
                              size_t source_size, const char *label);

typedef enum {
  /** JS_EVAL_FLAG_COMPILE_ONLY returned: the source parsed and compiled. */
  POCKETJS_GUEST_EVAL_COMPILED = 1,
  /** JS_EvalFunction is about to run: top-level init and mount begin here.
   * Reported before the call so allocation failures during the execute
   * stage can be labeled as such (result is ESP_OK, unused). */
  POCKETJS_GUEST_EVAL_EXECUTING = 2,
  /** JS_EvalFunction returned and the frame surface was resolved. */
  POCKETJS_GUEST_EVAL_EXECUTED = 3,
} pocketjs_guest_eval_stage_t;

typedef void (*pocketjs_guest_eval_stage_fn)(pocketjs_guest_t *guest,
                                            pocketjs_guest_eval_stage_t stage,
                                            esp_err_t result, void *user);

/** Staged variant of pocketjs_guest_eval for memory diagnostics: compile the
 * source once (compile-only eval), report, then execute the compiled
 * function and report again. One script, one compile, one execute — the
 * callback fires between them so the caller can sample the heap. Semantics
 * after the execute stage match pocketjs_guest_eval (globalThis.frame is
 * fetched, pending jobs drain). The compiled stage reports only on success;
 * the executed stage reports failures too, with the failing esp_err_t. */
esp_err_t pocketjs_guest_eval_stages(pocketjs_guest_t *guest,
                                     const char *source, size_t source_size,
                                     const char *label,
                                     pocketjs_guest_eval_stage_fn on_stage,
                                     void *user);

/** Call globalThis.frame(...) once and drain every pending Promise job. */
esp_err_t pocketjs_guest_frame(pocketjs_guest_t *guest,
                               const pocketjs_guest_frame_t *frame);

/** Ask the current or next JavaScript turn to stop through QuickJS's handler.
 * This is the only guest API that may be called outside the owner task. */
void pocketjs_guest_interrupt(pocketjs_guest_t *guest);

esp_err_t pocketjs_guest_stats(pocketjs_guest_t *guest,
                               pocketjs_guest_stats_t *out_stats);

/** Engine memory breakdown (never fails on a live guest). */
esp_err_t pocketjs_guest_memstats(pocketjs_guest_t *guest,
                                  pocketjs_guest_memstats_t *out_stats);

/** Allocator accounting; zeros unless CONFIG_POCKETJS_GUEST_ALLOC_STATS. */
void pocketjs_guest_allocstats(pocketjs_guest_t *guest,
                               pocketjs_guest_allocstats_t *out_stats);

/** Run one full JS garbage collection pass now. Use from an idle frame to
 * keep the engine's adaptive trigger from firing mid-render. */
void pocketjs_guest_run_gc(pocketjs_guest_t *guest);

void pocketjs_guest_destroy(pocketjs_guest_t *guest);

#ifdef __cplusplus
}
#endif
