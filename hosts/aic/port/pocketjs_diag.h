/* Staged memory diagnostics for the AIC port (P0 of docs/aic-memory-
 * optimization.md §3). Compiled in when the firmware build passes
 * -DPOCKETJS_AIC_DIAG (SConscript: POCKETJS_DIAG=0 in the environment turns
 * it off); without it every entry point below compiles to nothing. */
#ifndef POCKETJS_DIAG_H
#define POCKETJS_DIAG_H

#include <rtthread.h>

#include "pocketjs/guest.h"

/** Longest stage label pocketjs_diag_request keeps (NUL included). Defined
 * in both configurations so callers can size local buffers unconditionally. */
#define POCKETJS_DIAG_LABEL_MAX 24U

#ifdef POCKETJS_AIC_DIAG

/** Install the temporary TS profiler monotonic clock before bundle eval. */
esp_err_t pocketjs_diag_install(pocketjs_guest_t *guest);

/** Read hardware clock rates, cache CSRs and SYSMAP attributes. */
void pocketjs_diag_hw(void);

/** Print one [PJS-DIAG] sample block: system/TLSF heap with the free-block
 * walk, port allocation accounting, CMA, the pocketjs task stack high-water
 * and — when guest is non-NULL — the QuickJS engine and allocator stats.
 * Must run on the guest task (startup boundaries, frame loop); console-driven
 * samples go through pocketjs_diag_request instead. Also updates the failure
 * stage label used by pocketjs_heap_diag_set_stage. */
void pocketjs_diag_sample(const char *stage, pocketjs_guest_t *guest);

/** Print the allocation-failure ring and the port counters without a full
 * sample (used on startup failure paths before a guest exists). */
void pocketjs_diag_dump(void);

/** Drop the recorded peaks back to the current live values, for S6 delta
 * runs such as `wlan wifi_on` followed by `pjs_mem <label>`. */
void pocketjs_diag_reset_peaks(void);

/** Queue a sample from any thread (the pjs_mem console command); the frame
 * loop serves it on its own task at the next tick. */
void pocketjs_diag_request(const char *label);

/** Consume a queued request in the frame loop; returns RT_TRUE and fills
 * label when a sample is due. */
rt_bool_t pocketjs_diag_take_request(char *label, rt_size_t size);

#else

static inline esp_err_t pocketjs_diag_install(pocketjs_guest_t *guest) {
  (void)guest; return ESP_OK;
}
static inline void pocketjs_diag_hw(void) {}

static inline void pocketjs_diag_sample(const char *stage,
                                        pocketjs_guest_t *guest) {
  (void)stage;
  (void)guest;
}
static inline void pocketjs_diag_dump(void) {}
static inline void pocketjs_diag_reset_peaks(void) {}
static inline void pocketjs_diag_request(const char *label) { (void)label; }
static inline rt_bool_t pocketjs_diag_take_request(char *label,
                                                   rt_size_t size) {
  (void)label;
  (void)size;
  return RT_FALSE;
}

#endif /* POCKETJS_AIC_DIAG */

#endif /* POCKETJS_DIAG_H */
