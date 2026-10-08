/* aicfb presentation for the PocketJS frame loop.
 *
 * The display engine scans one of the two framebuffers it owns; a frame
 * becomes visible through AICFB_PAN_DISPLAY and AICFB_WAIT_FOR_VSYNC retires
 * the previous scan. The host renders damage into the non-scanned buffer, so
 * the CPU never touches pixels the display engine is reading. */
#include <rtconfig.h>
#include <rtthread.h>

#include <aic_core.h>
#include <artinchip_fb.h>
#include <mpp_fb.h>

#include "pocketjs_display.h"

struct pocketjs_aic_display {
  struct mpp_fb *fb;
  struct aicfb_screeninfo info;
  uint32_t buffer_size;
};

struct pocketjs_aic_display *pocketjs_aic_display_open(void) {
  struct pocketjs_aic_display *display = rt_malloc(sizeof(*display));
  if (display == RT_NULL) {
    rt_kprintf("[PocketJS] display: out of memory\n");
    return RT_NULL;
  }
  rt_memset(display, 0, sizeof(*display));

  display->fb = mpp_fb_open();
  if (display->fb == RT_NULL) {
    rt_kprintf("[PocketJS] display: mpp_fb_open failed\n");
    rt_free(display);
    return RT_NULL;
  }
  if (mpp_fb_ioctl(display->fb, AICFB_GET_SCREENINFO, &display->info) < 0) {
    rt_kprintf("[PocketJS] display: GET_SCREENINFO failed\n");
    mpp_fb_close(display->fb);
    rt_free(display);
    return RT_NULL;
  }
  if (display->info.format != MPP_FMT_RGB_565 || display->info.bits_per_pixel != 16U) {
    rt_kprintf("[PocketJS] display: format %#x is not RGB565\n",
               (unsigned)display->info.format);
    mpp_fb_close(display->fb);
    rt_free(display);
    return RT_NULL;
  }
  /* PAN_DISPLAY flips between the buffer at framebuffer[0] and the one one
   * fb_size later; smem_len reports that fb_size. The second buffer exists
   * because the defconfig turns AICFB_PAN_DISPLAY on, which makes drv_fb
   * allocate disp_buf_num = 2 framebuffers. */
  display->buffer_size = (uint32_t)display->info.height * display->info.stride;
  if (display->info.smem_len < display->buffer_size) {
    rt_kprintf("[PocketJS] display: framebuffer smaller than %ux%u stride\n",
               (unsigned)display->info.width, (unsigned)display->info.height);
    mpp_fb_close(display->fb);
    rt_free(display);
    return RT_NULL;
  }
  return display;
}

bool pocketjs_aic_display_info(const struct pocketjs_aic_display *display,
                               pocketjs_aic_display_info_t *out_info) {
  if (display == RT_NULL || out_info == RT_NULL) {
    return false;
  }
  out_info->width = (uint16_t)display->info.width;
  out_info->height = (uint16_t)display->info.height;
  out_info->stride_bytes = display->info.stride;
  out_info->buffer_size = display->buffer_size;
  out_info->buffers[0] = (uint8_t *)display->info.framebuffer;
  out_info->buffers[1] = (uint8_t *)display->info.framebuffer + display->buffer_size;
  return display->buffer_size > 0U;
}

void pocketjs_aic_display_present(struct pocketjs_aic_display *display,
                                  uint32_t index) {
  unsigned int buf_id = index & 1U;
  uint8_t *buffer = (index & 1U) ? display->info.framebuffer + display->buffer_size
                                 : display->info.framebuffer;
  aicos_dcache_clean_invalid_range((unsigned long *)buffer, display->buffer_size);
  (void)mpp_fb_ioctl(display->fb, AICFB_PAN_DISPLAY, &buf_id);
}

void pocketjs_aic_display_wait_vsync(struct pocketjs_aic_display *display) {
  (void)mpp_fb_ioctl(display->fb, AICFB_WAIT_FOR_VSYNC, RT_NULL);
}

void pocketjs_aic_display_close(struct pocketjs_aic_display *display) {
  if (display == RT_NULL) {
    return;
  }
  if (display->fb != RT_NULL) {
    mpp_fb_close(display->fb);
  }
  rt_free(display);
}
