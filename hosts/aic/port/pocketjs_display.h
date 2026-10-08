#ifndef POCKETJS_AIC_DISPLAY_H
#define POCKETJS_AIC_DISPLAY_H

#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>

struct pocketjs_aic_display;

/** Panel facts the frame loop needs; both buffers share stride and size. */
typedef struct {
  uint16_t width;
  uint16_t height;
  uint32_t stride_bytes;
  uint8_t *buffers[2];
  uint32_t buffer_size;
} pocketjs_aic_display_info_t;

/** Open the aicfb device and require a double-buffered RGB565 panel. */
struct pocketjs_aic_display *pocketjs_aic_display_open(void);

/** Facts captured at open; the display must stay open for the pointer to stay
 * valid. Returns false when the panel is single-buffered. */
bool pocketjs_aic_display_info(const struct pocketjs_aic_display *display,
                               pocketjs_aic_display_info_t *out_info);

/** Flush one already-rendered region: clean the CPU-written bytes out of the
 * cache and let the display engine scan the other buffer. Callers render
 * damage into buffers[index ^ 1] before calling this. */
void pocketjs_aic_display_present(struct pocketjs_aic_display *display,
                                  uint32_t index);

/** Block until the display engine finished scanning the current buffer. */
void pocketjs_aic_display_wait_vsync(struct pocketjs_aic_display *display);

void pocketjs_aic_display_close(struct pocketjs_aic_display *display);

#endif /* POCKETJS_AIC_DISPLAY_H */
