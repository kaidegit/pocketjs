/* AIC GE (2D graphics engine) accelerator for the RGB565 renderer. */
#ifndef POCKETJS_AIC_GE_H
#define POCKETJS_AIC_GE_H

#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>

#include "pocketjs/render_types.h"

struct pocketjs_aic_ge;

/* Opens mpp_ge once. The result is always non-NULL on allocation success
 * even when the engine is unavailable; query the accelerator instead of the
 * handle. POCKETJS_GE=0 in the build environment compiles the engine use
 * out and this returns a permanently disabled object. */
struct pocketjs_aic_ge *pocketjs_aic_ge_open(void);

void pocketjs_aic_ge_close(struct pocketjs_aic_ge *ge);

/* The accelerator callbacks, or NULL when every operation must stay on the
 * CPU rasterizer. */
const pocketjs_rgb565_accelerator_t *
pocketjs_aic_ge_accelerator(const struct pocketjs_aic_ge *ge);

#endif /* POCKETJS_AIC_GE_H */
