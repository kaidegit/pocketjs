/* E907 R1S1 user manual, section 4.1, figure 4.1 (printed page 49).
 * The SDK core_rv32.h SYSMAP masks use different positions; do not use
 * those masks to interpret the actual D12x register readback. */
#ifndef POCKETJS_HW_DECODE_H
#define POCKETJS_HW_DECODE_H

#include <stdint.h>

static inline unsigned pocketjs_hw_sysmap_so(uint32_t attr) {
  return (attr >> 4) & 1U;
}
static inline unsigned pocketjs_hw_sysmap_cacheable(uint32_t attr) {
  return (attr >> 3) & 1U;
}
static inline unsigned pocketjs_hw_sysmap_bufferable(uint32_t attr) {
  return (attr >> 2) & 1U;
}

#endif
