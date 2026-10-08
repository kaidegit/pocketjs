/* Panel touch device binding.
 *
 * The GT911 (or any luban touch panel driver) registers an RT-Thread touch
 * device that reports struct rt_touch_data through rt_device_read; the
 * rx_indicate callback marks reports pending. Contacts keep the panel's own
 * coordinate range returned by GET_INFO, so the frame loop scales
 * them to the viewport before handing them to the guest. */
#include <rtconfig.h>
#include <rtthread.h>
#include <rtdevice.h>

#include <drivers/touch.h>

#include "pocketjs_touch.h"

#define POCKETJS_TOUCH_MAX_POINTS 5U

struct pocketjs_aic_touch {
  rt_device_t device;
  rt_sem_t reports;
  struct rt_touch_info info;
  struct rt_touch_data data[POCKETJS_TOUCH_MAX_POINTS];
  pocketjs_aic_touch_contact_t contacts[POCKETJS_TOUCH_MAX_POINTS];
  rt_bool_t active[POCKETJS_TOUCH_MAX_POINTS];
  rt_bool_t published[POCKETJS_TOUCH_MAX_POINTS];
  rt_bool_t released[POCKETJS_TOUCH_MAX_POINTS];
  uint32_t report_count;
};

/* One panel touch device per firmware; the rx_indicate callback receives no
 * context, so the handle lives here instead of the device's user_data, which
 * the drivers keep for their own state. */
static struct pocketjs_aic_touch *g_panel_touch;

static rt_err_t pocketjs_touch_rx_indicate(rt_device_t device, rt_size_t size) {
  (void)device;
  (void)size;
  if (g_panel_touch != RT_NULL && g_panel_touch->reports != RT_NULL) {
    rt_sem_release(g_panel_touch->reports);
  }
  return RT_EOK;
}

struct pocketjs_aic_touch *pocketjs_aic_touch_open(const char *device_name) {
  struct pocketjs_aic_touch *touch = rt_malloc(sizeof(*touch));
  if (touch == RT_NULL) {
    return RT_NULL;
  }
  rt_memset(touch, 0, sizeof(*touch));

  touch->device = rt_device_find(device_name);
  if (touch->device == RT_NULL) {
    rt_kprintf("[PocketJS] touch: device %s not found\n", device_name);
    rt_free(touch);
    return RT_NULL;
  }
  if (rt_device_open(touch->device, RT_DEVICE_FLAG_INT_RX) != RT_EOK) {
    rt_kprintf("[PocketJS] touch: open %s failed\n", device_name);
    rt_free(touch);
    return RT_NULL;
  }
  if (rt_device_control(touch->device, RT_TOUCH_CTRL_GET_INFO, &touch->info) != RT_EOK) {
    rt_kprintf("[PocketJS] touch: GET_INFO failed\n");
    rt_device_close(touch->device);
    rt_free(touch);
    return RT_NULL;
  }
  touch->reports = rt_sem_create("pjk_touch", 0, RT_IPC_FLAG_FIFO);
  if (touch->reports == RT_NULL) {
    rt_device_close(touch->device);
    rt_free(touch);
    return RT_NULL;
  }
  g_panel_touch = touch;
  rt_device_set_rx_indicate(touch->device, pocketjs_touch_rx_indicate);
  return touch;
}

void pocketjs_aic_touch_range(const struct pocketjs_aic_touch *touch,
                              uint32_t *out_range_x, uint32_t *out_range_y) {
  if (touch == RT_NULL) {
    *out_range_x = 1U;
    *out_range_y = 1U;
    return;
  }
  *out_range_x = touch->info.range_x > 0 ? (uint32_t)touch->info.range_x : 1U;
  *out_range_y = touch->info.range_y > 0 ? (uint32_t)touch->info.range_y : 1U;
}

rt_size_t pocketjs_aic_touch_sample(struct pocketjs_aic_touch *touch,
                                    pocketjs_aic_touch_contact_t *out_contacts,
                                    rt_size_t capacity) {
  if (touch == RT_NULL || out_contacts == RT_NULL || capacity == 0U) {
    return 0U;
  }
  /* The guest consumes a snapshot, not the driver's DOWN/MOVE/UP stream.
   * Retain a stationary finger across frames without an IRQ. A down and up
   * drained together must still appear for one frame before being lifted. */
  rt_bool_t defer_reads = RT_FALSE;
  for (rt_size_t slot = 0; slot < POCKETJS_TOUCH_MAX_POINTS; slot++) {
    if (touch->released[slot]) {
      touch->active[slot] = RT_FALSE;
      touch->released[slot] = RT_FALSE;
      defer_reads = RT_TRUE;
    }
  }
  /* Each read is two I2C transactions; drain at most a few reports per frame
   * so an interrupt storm cannot stall the frame loop. */
  for (int reads = 0; !defer_reads && reads < 2 &&
       rt_sem_trytake(touch->reports) == RT_EOK;
       reads++) {
    int num = rt_device_read(touch->device, 0, touch->data, POCKETJS_TOUCH_MAX_POINTS);
    for (int index = 0; index < num && (rt_size_t)index < (rt_size_t)POCKETJS_TOUCH_MAX_POINTS; index++) {
      if (touch->data[index].event != RT_TOUCH_EVENT_DOWN &&
          touch->data[index].event != RT_TOUCH_EVENT_MOVE &&
          touch->data[index].event != RT_TOUCH_EVENT_UP) {
        continue;
      }
      touch->report_count++;
      rt_size_t slot = 0U;
      for (; slot < POCKETJS_TOUCH_MAX_POINTS; slot++) {
        if (touch->active[slot] &&
            touch->contacts[slot].id == touch->data[index].track_id) {
          break;
        }
      }
      if (touch->data[index].event == RT_TOUCH_EVENT_UP) {
        /* Publish the lift before draining a new down with a reused id. */
        defer_reads = RT_TRUE;
        if (slot < POCKETJS_TOUCH_MAX_POINTS) {
          if (touch->published[slot]) {
            touch->active[slot] = RT_FALSE;
          } else {
            touch->released[slot] = RT_TRUE;
          }
        }
        continue;
      }
      if (slot == POCKETJS_TOUCH_MAX_POINTS) {
        for (slot = 0U; slot < POCKETJS_TOUCH_MAX_POINTS; slot++) {
          if (!touch->active[slot]) {
            touch->active[slot] = RT_TRUE;
            touch->published[slot] = RT_FALSE;
            break;
          }
        }
      }
      if (slot < POCKETJS_TOUCH_MAX_POINTS) {
        touch->contacts[slot].id = touch->data[index].track_id;
        touch->contacts[slot].x = touch->data[index].x_coordinate;
        touch->contacts[slot].y = touch->data[index].y_coordinate;
      }
    }
  }
  rt_size_t count = 0U;
  for (rt_size_t slot = 0; slot < POCKETJS_TOUCH_MAX_POINTS && count < capacity;
       slot++) {
    if (touch->active[slot]) {
      out_contacts[count++] = touch->contacts[slot];
      touch->published[slot] = RT_TRUE;
    }
  }
  return count;
}

uint32_t pocketjs_aic_touch_report_count(const struct pocketjs_aic_touch *touch) {
  return touch != RT_NULL ? touch->report_count : 0U;
}

void pocketjs_aic_touch_close(struct pocketjs_aic_touch *touch) {
  if (touch == RT_NULL) {
    return;
  }
  g_panel_touch = RT_NULL;
  rt_device_set_rx_indicate(touch->device, RT_NULL);
  rt_device_close(touch->device);
  rt_sem_delete(touch->reports);
  rt_free(touch);
}
