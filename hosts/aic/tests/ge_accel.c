/* Host-side regression for port/pocketjs_ge.c against a recorded mpp_ge.
 *
 * Asserts the argument mapping (formats, strides, crops, flip flags, fill
 * color expansion), the clean -> GE -> invalidate cache ordering, and that
 * every unsupported or failing request returns false without invalidating,
 * so the renderer's ordered software fallback still sees its own writes. */
#include <assert.h>
#include <stdarg.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include <rtthread.h>

#include "mpp_ge.h"
#include "pocketjs_ge.h"

/* -- rtthread surface ------------------------------------------------- */

void *rt_malloc(rt_size_t size) { return malloc(size); }
void rt_free(void *pointer) { free(pointer); }

static char kprintf_log[512];
void rt_kprintf(const char *format, ...) {
  va_list arguments;
  va_start(arguments, format);
  vsnprintf(kprintf_log + strlen(kprintf_log),
            sizeof(kprintf_log) - strlen(kprintf_log), format, arguments);
  va_end(arguments);
}

/* -- recorded mpp_ge --------------------------------------------------- */

static bool open_fails;
static int fill_result = 1;
static int blt_result = 1;
static int open_calls, fill_calls, blt_calls;
static struct ge_fillrect last_fill;
static struct ge_bitblt last_blt;

/* One ordered event stream: cache operations and engine submissions. */
static char events[128][16];
static unsigned long event_addresses[128];
static size_t event_sizes[128];
static int event_count;

void test_ge_cache_log(const char *operation, const void *address,
                       size_t size) {
  assert(event_count < 128);
  snprintf(events[event_count], sizeof(events[event_count]), "%s", operation);
  event_addresses[event_count] = (unsigned long)(uintptr_t)address;
  event_sizes[event_count] = size;
  event_count++;
}

static void record_event(const char *name, unsigned long address) {
  assert(event_count < 128);
  snprintf(events[event_count], sizeof(events[event_count]), "%s", name);
  event_addresses[event_count] = address;
  event_sizes[event_count] = 0;
  event_count++;
}

static void events_reset(void) { event_count = 0; }

static int event_index(const char *name) {
  for (int index = 0; index < event_count; index++) {
    if (strcmp(events[index], name) == 0) {
      return index;
    }
  }
  return -1;
}

/* An opaque handle; the port only ever passes it back to the stubs. */
static char engine;

struct mpp_ge *mpp_ge_open(void) {
  open_calls++;
  return open_fails ? NULL : (struct mpp_ge *)(void *)&engine;
}

void mpp_ge_close(struct mpp_ge *ge) { (void)ge; }

enum ge_mode mpp_ge_get_mode(struct mpp_ge *ge) {
  (void)ge;
  return GE_MODE_NORMAL;
}

int mpp_ge_fillrect(struct mpp_ge *ge, struct ge_fillrect *fill) {
  (void)ge;
  fill_calls++;
  last_fill = *fill;
  record_event("fill", last_fill.dst_buf.phy_addr[0]);
  return fill_result;
}

int mpp_ge_bitblt(struct mpp_ge *ge, struct ge_bitblt *blt) {
  (void)ge;
  blt_calls++;
  last_blt = *blt;
  record_event("blt", last_blt.dst_buf.phy_addr[0]);
  return blt_result;
}

/* -- assertions -------------------------------------------------------- */

static void assert_ctrl_disabled(const struct ge_ctrl *ctrl) {
  assert(ctrl->alpha_en == 0);
  assert(ctrl->ck_en == 0);
  assert(ctrl->dither_en == 0);
}

static void test_fill_mapping(void) {
  static uint16_t destination[16 * 8];
  struct pocketjs_aic_ge *ge = pocketjs_aic_ge_open();
  assert(ge != NULL);
  const pocketjs_rgb565_accelerator_t *accelerator =
      pocketjs_aic_ge_accelerator(ge);
  assert(accelerator != NULL);
  assert(accelerator->fill_rgb565 != NULL);
  assert(accelerator->srm_psm5650_rgb565 != NULL);
  /* GE has no alpha-only source format; the A8 blend must stay on the CPU. */
  assert(accelerator->blend_a8_rgb565 == NULL);
  assert(strstr(kprintf_log, "normal mode ready") != NULL);

  events_reset();
  pocketjs_rgb565_rect_t rect = {.x = 3, .y = 1, .width = 5, .height = 2};
  assert(accelerator->fill_rgb565(accelerator->user_data, destination,
                                  16 * 8, 16, 8, rect, 0xf800));
  assert(fill_calls == 1);
  assert(last_fill.type == GE_NO_GRADIENT);
  /* 0xF800 packs to opaque pure red: 0xFF << 24 | 0xFF << 16. */
  assert(last_fill.start_color == 0xffff0000U);
  assert(last_fill.end_color == 0);
  assert_ctrl_disabled(&last_fill.ctrl);
  assert(last_fill.dst_buf.buf_type == MPP_PHY_ADDR);
  assert(last_fill.dst_buf.format == MPP_FMT_RGB_565);
  assert(last_fill.dst_buf.phy_addr[0] ==
         (unsigned int)(uintptr_t)destination);
  assert(last_fill.dst_buf.stride[0] == 16 * 2U);
  assert(last_fill.dst_buf.size.width == 16);
  assert(last_fill.dst_buf.size.height == 8);
  assert(last_fill.dst_buf.crop_en == 1);
  assert(last_fill.dst_buf.crop.x == 3);
  assert(last_fill.dst_buf.crop.y == 1);
  assert(last_fill.dst_buf.crop.width == 5);
  assert(last_fill.dst_buf.crop.height == 2);

  /* Two rows: clean both, run the engine, invalidate both, in that order. */
  assert(event_count == 5);
  assert(strcmp(events[0], "clean") == 0);
  assert(strcmp(events[1], "clean") == 0);
  assert(strcmp(events[2], "fill") == 0);
  assert(strcmp(events[3], "invalid") == 0);
  assert(strcmp(events[4], "invalid") == 0);
  assert(event_sizes[0] == 5 * 2U);
  assert(event_addresses[0] ==
         (unsigned long)(uintptr_t)(destination + 1 * 16 + 3));
  assert(event_addresses[1] ==
         (unsigned long)(uintptr_t)(destination + 2 * 16 + 3));

  /* 565 channel replication round-trips through the ARGB8888 fill color. */
  events_reset();
  assert(accelerator->fill_rgb565(accelerator->user_data, destination,
                                  16 * 8, 16, 8, rect, 0x07e0));
  assert(last_fill.start_color == 0xff00ff00U);
  events_reset();
  assert(accelerator->fill_rgb565(accelerator->user_data, destination,
                                  16 * 8, 16, 8, rect, 0x001f));
  assert(last_fill.start_color == 0xff0000ffU);

  /* Out-of-range geometry falls back without touching the engine. */
  events_reset();
  pocketjs_rgb565_rect_t outside = {.x = 14, .y = 0, .width = 5, .height = 2};
  assert(!accelerator->fill_rgb565(accelerator->user_data, destination,
                                   16 * 8, 16, 8, outside, 0xf800));
  pocketjs_rgb565_rect_t empty = {.x = 0, .y = 0, .width = 0, .height = 2};
  assert(!accelerator->fill_rgb565(accelerator->user_data, destination,
                                   16 * 8, 16, 8, empty, 0xf800));
  assert(!accelerator->fill_rgb565(accelerator->user_data, destination,
                                   16, 16, 8, rect, 0xf800));
  assert(event_count == 0);
  assert(fill_calls == 3);

  /* A failed submission returns false and skips the invalidate, so the CPU
   * replay starts from its own cache state. */
  events_reset();
  fill_result = -1;
  assert(!accelerator->fill_rgb565(accelerator->user_data, destination,
                                   16 * 8, 16, 8, rect, 0xf800));
  assert(event_index("invalid") == -1);
  fill_result = 1;

  pocketjs_aic_ge_close(ge);
}

static void test_srm_mapping(void) {
  static uint16_t destination[16 * 8];
  static uint8_t texture[8 * 4 * 2];
  struct pocketjs_aic_ge *ge = pocketjs_aic_ge_open();
  const pocketjs_rgb565_accelerator_t *accelerator =
      pocketjs_aic_ge_accelerator(ge);
  assert(accelerator != NULL);

  events_reset();
  pocketjs_rgb565_rect_t source_rect = {.x = 1, .y = 2, .width = 5,
                                        .height = 2};
  pocketjs_rgb565_rect_t destination_rect = {.x = 4, .y = 3, .width = 5,
                                             .height = 2};
  assert(accelerator->srm_psm5650_rgb565(accelerator->user_data, destination,
                                         16 * 8, 16, 8, texture,
                                         sizeof(texture), 8, 4, source_rect,
                                         destination_rect, 0, true, false));
  assert(blt_calls == 1);
  assert_ctrl_disabled(&last_blt.ctrl);
  /* PSM 5650 stores R and B opposite to the framebuffer; the source is
   * declared BGR_565 so the conversion swaps the channels. */
  assert(last_blt.src_buf.format == MPP_FMT_BGR_565);
  assert(last_blt.src_buf.buf_type == MPP_PHY_ADDR);
  assert(last_blt.src_buf.phy_addr[0] == (unsigned int)(uintptr_t)texture);
  assert(last_blt.src_buf.stride[0] == 8 * 2U);
  assert(last_blt.src_buf.size.width == 8);
  assert(last_blt.src_buf.size.height == 4);
  assert(last_blt.src_buf.crop.x == 1);
  assert(last_blt.src_buf.crop.y == 2);
  assert(last_blt.src_buf.crop.width == 5);
  assert(last_blt.src_buf.crop.height == 2);
  assert(last_blt.dst_buf.format == MPP_FMT_RGB_565);
  assert(last_blt.dst_buf.phy_addr[0] ==
         (unsigned int)(uintptr_t)destination);
  assert(last_blt.dst_buf.stride[0] == 16 * 2U);
  assert(last_blt.dst_buf.crop.x == 4);
  assert(last_blt.dst_buf.crop.y == 3);
  assert(last_blt.ctrl.flags == MPP_FLIP_H);

  /* Source clean (one span), destination clean per row, blit, invalidate. */
  assert(event_count == 6);
  assert(strcmp(events[0], "clean") == 0);
  assert(event_addresses[0] ==
         (unsigned long)(uintptr_t)(texture + (2 * 8 + 1) * 2));
  /* Rows y=2..3 of an 8-wide texture: span ends at (3*8+1+5)*2 bytes. */
  assert(event_sizes[0] == (3 * 8 + 6) * 2U - (2 * 8 + 1) * 2U);
  assert(strcmp(events[1], "clean") == 0);
  assert(strcmp(events[2], "clean") == 0);
  assert(strcmp(events[3], "blt") == 0);
  assert(strcmp(events[4], "invalid") == 0);
  assert(strcmp(events[5], "invalid") == 0);

  pocketjs_rgb565_rect_t vertical_mirror_rect = destination_rect;
  events_reset();
  assert(accelerator->srm_psm5650_rgb565(accelerator->user_data, destination,
                                         16 * 8, 16, 8, texture,
                                         sizeof(texture), 8, 4, source_rect,
                                         vertical_mirror_rect, 0, false,
                                         true));
  assert(last_blt.ctrl.flags == MPP_FLIP_V);

  /* Rotation and rescaling stay on the renderer's CPU path. */
  events_reset();
  assert(!accelerator->srm_psm5650_rgb565(
      accelerator->user_data, destination, 16 * 8, 16, 8, texture,
      sizeof(texture), 8, 4, source_rect, destination_rect, 1, false,
      false));
  pocketjs_rgb565_rect_t scaled = {.x = 4, .y = 3, .width = 6, .height = 2};
  assert(!accelerator->srm_psm5650_rgb565(
      accelerator->user_data, destination, 16 * 8, 16, 8, texture,
      sizeof(texture), 8, 4, source_rect, scaled, 0, false, false));
  assert(!accelerator->srm_psm5650_rgb565(
      accelerator->user_data, destination, 16 * 8, 16, 8, texture,
      sizeof(texture) - 1, 8, 4, source_rect, destination_rect, 0, false,
      false));
  assert(event_count == 0);
  assert(blt_calls == 2);

  events_reset();
  blt_result = -1;
  assert(!accelerator->srm_psm5650_rgb565(accelerator->user_data, destination,
                                          16 * 8, 16, 8, texture,
                                          sizeof(texture), 8, 4, source_rect,
                                          destination_rect, 0, false,
                                          false));
  assert(event_index("invalid") == -1);
  blt_result = 1;

  pocketjs_aic_ge_close(ge);
}

static void test_open_failure_and_toggle(void) {
  open_fails = true;
  kprintf_log[0] = '\0';
  struct pocketjs_aic_ge *ge = pocketjs_aic_ge_open();
  assert(ge != NULL);
  assert(pocketjs_aic_ge_accelerator(ge) == NULL);
  assert(strstr(kprintf_log, "mpp_ge_open failed") != NULL);
  pocketjs_aic_ge_close(ge);
  open_fails = false;

  assert(pocketjs_aic_ge_accelerator(NULL) == NULL);
  pocketjs_aic_ge_close(NULL);
  assert(open_calls >= 2);
}

int main(void) {
  test_fill_mapping();
  test_srm_mapping();
  test_open_failure_and_toggle();
  printf("AIC GE accelerator regressions passed\n");
  return 0;
}
