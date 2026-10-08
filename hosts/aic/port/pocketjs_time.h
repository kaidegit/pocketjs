#ifndef POCKETJS_AIC_TIME_H
#define POCKETJS_AIC_TIME_H

#include <rtthread.h>

/* Deadlines are less than half the 32-bit tick range ahead. Luban starts at
 * 0xffff0000, so the first wrap occurs about 65 seconds after boot. */
static inline rt_bool_t pocketjs_aic_tick_reached(rt_tick_t now,
                                                 rt_tick_t deadline) {
  return (rt_tick_t)(now - deadline) < 0x80000000U;
}

static inline rt_tick_t pocketjs_aic_ticks_until(rt_tick_t now,
                                                rt_tick_t deadline) {
  const rt_tick_t remaining = deadline - now;
  return remaining < 0x80000000U ? remaining : 0U;
}

#endif /* POCKETJS_AIC_TIME_H */
