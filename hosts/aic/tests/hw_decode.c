#include <assert.h>
#include <stdio.h>
#include "pocketjs_hw_decode.h"

int main(void) {
  /* Actual D12x device readbacks: RAM/flash 0x0c, MMIO 0x10. */
  assert(pocketjs_hw_sysmap_so(0x0c) == 0);
  assert(pocketjs_hw_sysmap_cacheable(0x0c) == 1);
  assert(pocketjs_hw_sysmap_bufferable(0x0c) == 1);
  assert(pocketjs_hw_sysmap_so(0x10) == 1);
  assert(pocketjs_hw_sysmap_cacheable(0x10) == 0);
  assert(pocketjs_hw_sysmap_bufferable(0x10) == 0);
  /* Distinguish the two independent attributes and ignore reserved bits. */
  assert(pocketjs_hw_sysmap_cacheable(0x04) == 0);
  assert(pocketjs_hw_sysmap_bufferable(0x04) == 1);
  assert(pocketjs_hw_sysmap_cacheable(0x08) == 1);
  assert(pocketjs_hw_sysmap_bufferable(0x08) == 0);
  assert(pocketjs_hw_sysmap_so(0x03) == 0);
  assert(pocketjs_hw_sysmap_cacheable(0x03) == 0);
  assert(pocketjs_hw_sysmap_bufferable(0x03) == 0);
  puts("AIC hardware attribute decoding passed");
  return 0;
}
