/* AIC GE (2D graphics engine) accelerator behind the RGB565 renderer's
 * accelerator callbacks.
 *
 * fill_rgb565 maps to one GE fillrect: the constant color travels as the
 * ARGB8888 source the fill path always uses, and the hardware converts it to
 * the RGB565 destination. srm_psm5650_to_rgb565 maps to one GE bitblt whose
 * source is declared MPP_FMT_BGR_565, because PSP PSM 5650 stores R and B
 * opposite to the framebuffer's RGB565 and the format conversion swaps them.
 * blend_a8_rgb565 stays unmapped: GE has no alpha-only source format, so
 * glyph masks and alpha quads keep the CPU rasterizer and the accelerator
 * leaves that callback NULL.
 *
 * Every callback upholds the synchronous PpaOps contract. GE normal mode
 * blocks in the ioctl until the finish IRQ, destination cache lines are
 * cleaned before the DMA (CPU writes sharing a line reach RAM first) and
 * invalidated after it (later CPU ops read what GE wrote). Any GE failure or
 * unsupported request returns false, and the renderer repeats the operation
 * in software; fill and blit are whole-rectangle writes, so the replay after
 * a partial GE failure still produces the same pixels. */
#include <rtconfig.h>
#include <rtthread.h>

#include "pocketjs_ge.h"

#ifdef POCKETJS_AIC_GE_DISABLE

/* POCKETJS_GE=0 in the build environment: the host links a permanently
 * disabled object so the accelerator table stays NULL and every pixel comes
 * from the CPU rasterizer, matching the pre-GE firmware. */

struct pocketjs_aic_ge {
  char unused;
};

struct pocketjs_aic_ge *pocketjs_aic_ge_open(void) {
  struct pocketjs_aic_ge *ge = rt_malloc(sizeof(*ge));
  if (ge != RT_NULL) {
    rt_memset(ge, 0, sizeof(*ge));
    rt_kprintf("[PocketJS] ge: compiled out (POCKETJS_GE=0)\n");
  }
  return ge;
}

void pocketjs_aic_ge_close(struct pocketjs_aic_ge *ge) {
  if (ge != RT_NULL) {
    rt_free(ge);
  }
}

const pocketjs_rgb565_accelerator_t *
pocketjs_aic_ge_accelerator(const struct pocketjs_aic_ge *ge) {
  (void)ge;
  return RT_NULL;
}

#else /* POCKETJS_AIC_GE_DISABLE */

#include <mpp_ge.h>

struct pocketjs_aic_ge {
  struct mpp_ge *ge;
  pocketjs_rgb565_accelerator_t accelerator;
};

/* Expands an RGB565 pixel to the ARGB8888 word the fill path feeds to GE.
 * Replicating the low bits keeps the high 5/6 bits intact, so the engine's
 * ARGB8888 -> RGB565 truncation reproduces the exact requested color. */
static unsigned int pocketjs_ge_argb8888(uint16_t color) {
  unsigned int r = ((unsigned int)color >> 11) & 0x1fU;
  unsigned int g = ((unsigned int)color >> 5) & 0x3fU;
  unsigned int b = (unsigned int)color & 0x1fU;
  r = (r << 3) | (r >> 2);
  g = (g << 2) | (g >> 4);
  b = (b << 3) | (b >> 2);
  return 0xffU << 24 | r << 16 | g << 8 | b;
}

static bool pocketjs_ge_rect_inside(pocketjs_rgb565_rect_t rect, uint32_t width,
                                    uint32_t height) {
  return rect.width > 0U && rect.height > 0U && rect.x < width &&
         rect.y < height && rect.width <= width - rect.x &&
         rect.height <= height - rect.y;
}

/* Cache maintenance for one rectangle of a stride-tight u16 surface. Rows
 * are flushed one by one so a narrow rectangle never pushes, or drops, the
 * untouched pixels between its first and last row. The aicos prototypes vary
 * across SDK platforms; the (unsigned long *) cast compiles against both. */
static void pocketjs_ge_clean_rows(const uint16_t *destination, uint32_t width,
                                   pocketjs_rgb565_rect_t rect) {
  const uint8_t *base = (const uint8_t *)destination;
  for (uint32_t y = rect.y; y < rect.y + rect.height; y++) {
    const uint8_t *row = base + ((size_t)y * width + rect.x) * 2U;
    aicos_dcache_clean_range((unsigned long *)(uintptr_t)row, rect.width * 2U);
  }
}

static void pocketjs_ge_invalidate_rows(const uint16_t *destination,
                                        uint32_t width,
                                        pocketjs_rgb565_rect_t rect) {
  const uint8_t *base = (const uint8_t *)destination;
  for (uint32_t y = rect.y; y < rect.y + rect.height; y++) {
    const uint8_t *row = base + ((size_t)y * width + rect.x) * 2U;
    aicos_dcache_invalid_range((unsigned long *)(uintptr_t)row,
                               rect.width * 2U);
  }
}

static bool pocketjs_ge_fill(void *user_data, uint16_t *destination,
                            size_t destination_pixels, uint32_t width,
                            uint32_t height, pocketjs_rgb565_rect_t rect,
                            uint16_t color) {
  struct pocketjs_aic_ge *state = user_data;
  if (state == RT_NULL || state->ge == RT_NULL) {
    return false;
  }
  if (width == 0U || height == 0U ||
      (size_t)width * height != destination_pixels ||
      !pocketjs_ge_rect_inside(rect, width, height)) {
    return false;
  }

  struct ge_fillrect fill;
  rt_memset(&fill, 0, sizeof(fill));
  fill.type = GE_NO_GRADIENT;
  fill.start_color = pocketjs_ge_argb8888(color);
  fill.dst_buf.buf_type = MPP_PHY_ADDR;
  fill.dst_buf.phy_addr[0] = (unsigned int)(uintptr_t)destination;
  fill.dst_buf.stride[0] = width * 2U;
  fill.dst_buf.size.width = (int)width;
  fill.dst_buf.size.height = (int)height;
  fill.dst_buf.crop_en = 1;
  fill.dst_buf.crop.x = (int)rect.x;
  fill.dst_buf.crop.y = (int)rect.y;
  fill.dst_buf.crop.width = (int)rect.width;
  fill.dst_buf.crop.height = (int)rect.height;
  fill.dst_buf.format = MPP_FMT_RGB_565;
  /* ctrl stays zeroed: no alpha blending, no dither, plain overwrite. */

  pocketjs_ge_clean_rows(destination, width, rect);
  if (mpp_ge_fillrect(state->ge, &fill) < 0) {
    return false;
  }
  pocketjs_ge_invalidate_rows(destination, width, rect);
  return true;
}

static bool pocketjs_ge_srm(void *user_data, uint16_t *destination,
                            size_t destination_pixels, uint32_t width,
                            uint32_t height, const uint8_t *source,
                            size_t source_bytes, uint32_t source_width,
                            uint32_t source_height,
                            pocketjs_rgb565_rect_t source_rect,
                            pocketjs_rgb565_rect_t destination_rect,
                            uint32_t rotation, bool mirror_x, bool mirror_y) {
  struct pocketjs_aic_ge *state = user_data;
  if (state == RT_NULL || state->ge == RT_NULL || rotation != 0U) {
    return false;
  }
  if (source_width == 0U || source_height == 0U ||
      source_bytes < (size_t)source_width * source_height * 2U ||
      !pocketjs_ge_rect_inside(source_rect, source_width, source_height)) {
    return false;
  }
  if (width == 0U || height == 0U ||
      (size_t)width * height != destination_pixels ||
      !pocketjs_ge_rect_inside(destination_rect, width, height)) {
    return false;
  }
  /* The renderer routes 1:1 and linear-filtered quads here; sizes that differ
   * would engage the GE scaler and change the sampling, so keep those on the
   * CPU path with the renderer's own sampler. */
  if (source_rect.width != destination_rect.width ||
      source_rect.height != destination_rect.height) {
    return false;
  }

  struct ge_bitblt blt;
  rt_memset(&blt, 0, sizeof(blt));
  blt.src_buf.buf_type = MPP_PHY_ADDR;
  blt.src_buf.phy_addr[0] = (unsigned int)(uintptr_t)source;
  blt.src_buf.stride[0] = source_width * 2U;
  blt.src_buf.size.width = (int)source_width;
  blt.src_buf.size.height = (int)source_height;
  blt.src_buf.crop_en = 1;
  blt.src_buf.crop.x = (int)source_rect.x;
  blt.src_buf.crop.y = (int)source_rect.y;
  blt.src_buf.crop.width = (int)source_rect.width;
  blt.src_buf.crop.height = (int)source_rect.height;
  blt.src_buf.format = MPP_FMT_BGR_565;
  blt.dst_buf.buf_type = MPP_PHY_ADDR;
  blt.dst_buf.phy_addr[0] = (unsigned int)(uintptr_t)destination;
  blt.dst_buf.stride[0] = width * 2U;
  blt.dst_buf.size.width = (int)width;
  blt.dst_buf.size.height = (int)height;
  blt.dst_buf.crop_en = 1;
  blt.dst_buf.crop.x = (int)destination_rect.x;
  blt.dst_buf.crop.y = (int)destination_rect.y;
  blt.dst_buf.crop.width = (int)destination_rect.width;
  blt.dst_buf.crop.height = (int)destination_rect.height;
  blt.dst_buf.format = MPP_FMT_RGB_565;
  blt.ctrl.flags = (mirror_x ? MPP_FLIP_H : 0U) | (mirror_y ? MPP_FLIP_V : 0U);

  /* The texture bytes were written by the CPU (PAK upload); the crop's row
   * span covers every line GE reads. */
  const uint8_t *first_row =
      source + ((size_t)source_rect.y * source_width + source_rect.x) * 2U;
  size_t source_span =
      ((size_t)(source_rect.y + source_rect.height - 1U) * source_width +
       source_rect.x + source_rect.width) * 2U -
      (size_t)(first_row - source);
  aicos_dcache_clean_range((unsigned long *)(uintptr_t)first_row, source_span);

  pocketjs_ge_clean_rows(destination, width, destination_rect);
  if (mpp_ge_bitblt(state->ge, &blt) < 0) {
    return false;
  }
  pocketjs_ge_invalidate_rows(destination, width, destination_rect);
  return true;
}

struct pocketjs_aic_ge *pocketjs_aic_ge_open(void) {
  struct pocketjs_aic_ge *ge = rt_malloc(sizeof(*ge));
  if (ge == RT_NULL) {
    return RT_NULL;
  }
  rt_memset(ge, 0, sizeof(*ge));
  ge->ge = mpp_ge_open();
  if (ge->ge == RT_NULL) {
    rt_kprintf("[PocketJS] ge: mpp_ge_open failed; CPU rendering only\n");
    return ge;
  }
  ge->accelerator.struct_size = sizeof(ge->accelerator);
  ge->accelerator.user_data = ge;
  ge->accelerator.fill_rgb565 = pocketjs_ge_fill;
  ge->accelerator.srm_psm5650_rgb565 = pocketjs_ge_srm;
  rt_kprintf("[PocketJS] ge: %s mode ready (fill + psm5650 blit; A8 blend on CPU)\n",
             mpp_ge_get_mode(ge->ge) == GE_MODE_CMDQ ? "cmdq" : "normal");
  return ge;
}

void pocketjs_aic_ge_close(struct pocketjs_aic_ge *ge) {
  if (ge == RT_NULL) {
    return;
  }
  if (ge->ge != RT_NULL) {
    mpp_ge_close(ge->ge);
  }
  rt_free(ge);
}

const pocketjs_rgb565_accelerator_t *
pocketjs_aic_ge_accelerator(const struct pocketjs_aic_ge *ge) {
  if (ge == RT_NULL || ge->ge == RT_NULL) {
    return RT_NULL;
  }
  return &ge->accelerator;
}

#endif /* POCKETJS_AIC_GE_DISABLE */
