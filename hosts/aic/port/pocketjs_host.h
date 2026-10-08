#ifndef POCKETJS_AIC_HOST_H
#define POCKETJS_AIC_HOST_H

#include <rtthread.h>
#include <stdint.h>

/** Boot one PocketJS guest from a .pocket file on a mounted filesystem and
 * run its frame loop on a dedicated task until the guest stops the JS turn
 * (interrupt or error). Returns 0 once the task is started. */
int pocketjs_aic_run(const char *package_path);

/** Boot one PocketJS guest from a buffer linked into the firmware image —
 * generated/pocket_bin.c, emitted by tools/aic.ts package. The buffer is
 * read-only (flash-backed rodata) and must stay valid for the run. */
int pocketjs_aic_run_embedded(const uint8_t *bytes, size_t size);

#endif /* POCKETJS_AIC_HOST_H */
