#ifndef POCKETJS_AIC_TOUCH_H
#define POCKETJS_AIC_TOUCH_H

#include <rtthread.h>
#include <stdint.h>

/* Active touch contacts sampled between frame turns. The frame loop owns the
 * sample step; the rx_indicate callback only counts pending reports, so no
 * ISR-side work beyond the semaphore happens. */

typedef struct {
  uint8_t id;
  uint16_t x;
  uint16_t y;
} pocketjs_aic_touch_contact_t;

struct pocketjs_aic_touch;

/** Open the panel touch device and learn its coordinate range. */
struct pocketjs_aic_touch *pocketjs_aic_touch_open(const char *device_name);

/** The coordinate range the controller reports (the GT911 answers 1024x600);
 * contacts keep panel coordinates and the frame loop scales them. */
void pocketjs_aic_touch_range(const struct pocketjs_aic_touch *touch,
                              uint32_t *out_range_x, uint32_t *out_range_y);

/** Drain pending reports and return the active snapshot (panel coordinates).
 * Stationary contacts remain down without a new report; UP removes an id.
 * A tap drained within one call is published for one frame before release. */
rt_size_t pocketjs_aic_touch_sample(struct pocketjs_aic_touch *touch,
                                    pocketjs_aic_touch_contact_t *out_contacts,
                                    rt_size_t capacity);

/** Cumulative DOWN/MOVE/UP reports, independent of the snapshot frame count. */
uint32_t pocketjs_aic_touch_report_count(const struct pocketjs_aic_touch *touch);

void pocketjs_aic_touch_close(struct pocketjs_aic_touch *touch);

#endif /* POCKETJS_AIC_TOUCH_H */
