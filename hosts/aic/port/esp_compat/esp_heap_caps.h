/* heap_caps surface over the RT-Thread heap. The luban-lite build enables
 * RT_USING_MEMHEAP_AS_HEAP, so rt_malloc already draws from the PSRAM software
 * pool; the capability arguments decide nothing beyond fall-back behaviour. */
#ifndef POCKETJS_AIC_ESP_HEAP_CAPS_H
#define POCKETJS_AIC_ESP_HEAP_CAPS_H

#include <stdlib.h>

#define MALLOC_CAP_SPIRAM (1 << 0)
#define MALLOC_CAP_INTERNAL (1 << 1)
#define MALLOC_CAP_8BIT (1 << 2)
#define MALLOC_CAP_DMA (1 << 3)

void *heap_caps_malloc(size_t size, unsigned caps);
void *heap_caps_calloc(size_t count, size_t size, unsigned caps);
void *heap_caps_aligned_alloc(size_t alignment, size_t size, unsigned caps);
void heap_caps_free(void *ptr);

#endif /* POCKETJS_AIC_ESP_HEAP_CAPS_H */
