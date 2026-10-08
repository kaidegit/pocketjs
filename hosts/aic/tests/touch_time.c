#include <assert.h>
#include <stdarg.h>
#include <stdio.h>
#include <stdlib.h>
#include <rtdevice.h>
#include <drivers/touch.h>
#include "pocketjs_time.h"
#include "pocketjs_touch.h"

struct test_sem { unsigned pending; };
struct test_device { rt_rx_indicate_t rx; };
static struct test_device device;
static struct rt_touch_data queue[32][5];
static unsigned head, tail, allocations;
static int32_t panel_range_x = 1024, panel_range_y = 600;

void *rt_malloc(rt_size_t size) { allocations++; return malloc(size); }
void rt_free(void *pointer) { free(pointer); }
void rt_kprintf(const char *format, ...) {
  va_list arguments;
  va_start(arguments, format);
  vprintf(format, arguments);
  va_end(arguments);
}
rt_sem_t rt_sem_create(const char *name, unsigned value, unsigned flag) {
  (void)name; (void)flag;
  rt_sem_t sem = rt_malloc(sizeof(*sem));
  sem->pending = value;
  return sem;
}
rt_err_t rt_sem_release(rt_sem_t sem) { sem->pending++; return RT_EOK; }
rt_err_t rt_sem_trytake(rt_sem_t sem) {
  if (!sem->pending) return -1;
  sem->pending--;
  return RT_EOK;
}
rt_err_t rt_sem_delete(rt_sem_t sem) { rt_free(sem); return RT_EOK; }
rt_device_t rt_device_find(const char *name) { (void)name; return &device; }
rt_err_t rt_device_open(rt_device_t dev, unsigned flag) { (void)dev; (void)flag; return RT_EOK; }
rt_err_t rt_device_close(rt_device_t dev) { (void)dev; return RT_EOK; }
rt_err_t rt_device_control(rt_device_t dev, int command, void *argument) {
  (void)dev;
  assert(command == RT_TOUCH_CTRL_GET_INFO);
  *(struct rt_touch_info *)argument = (struct rt_touch_info){
    .point_num = 5, .range_x = panel_range_x, .range_y = panel_range_y};
  return RT_EOK;
}
rt_err_t rt_device_set_rx_indicate(rt_device_t dev, rt_rx_indicate_t callback) {
  dev->rx = callback;
  return RT_EOK;
}
rt_size_t rt_device_read(rt_device_t dev, int position, void *buffer, rt_size_t size) {
  (void)dev; (void)position;
  assert(size == 5 && head < tail);
  memcpy(buffer, queue[head++ % 32], sizeof(queue[0]));
  return 5;
}
static void report(unsigned id, unsigned event, unsigned x, unsigned y) {
  assert(tail - head < 32 && id < 5);
  struct rt_touch_data *data = queue[tail++ % 32];
  memset(data, 0, sizeof(queue[0]));
  data[id] = (struct rt_touch_data){.event = event, .track_id = id, .x_coordinate = x, .y_coordinate = y};
  assert(device.rx(&device, 1) == RT_EOK);
}

static void test_touch(void) {
  struct pocketjs_aic_touch *touch = pocketjs_aic_touch_open("gt911");
  assert(touch);
  uint32_t x, y;
  pocketjs_aic_touch_range(touch, &x, &y);
  assert(x == 1024 && y == 600);
  pocketjs_aic_touch_contact_t contacts[8];
  const unsigned opened_allocations = allocations;
  report(4, RT_TOUCH_EVENT_DOWN, 512, 300);
  assert(pocketjs_aic_touch_sample(touch, contacts, 8) == 1);
  assert(contacts[0].id == 4 && contacts[0].x == 512 && contacts[0].y == 300);
  assert(contacts[0].x * 480U / x == 240 && contacts[0].y * 272U / y == 136);
  for (unsigned frame = 0; frame < 60000; frame++) {
    assert(pocketjs_aic_touch_sample(touch, contacts, 8) == 1);
  }
  assert(pocketjs_aic_touch_report_count(touch) == 1);
  assert(allocations == opened_allocations);
  report(4, RT_TOUCH_EVENT_MOVE, 600, 320);
  report(4, RT_TOUCH_EVENT_MOVE, 700, 350);
  assert(pocketjs_aic_touch_sample(touch, contacts, 8) == 1);
  assert(contacts[0].x == 700 && contacts[0].y == 350);
  report(4, RT_TOUCH_EVENT_UP, 700, 350);
  report(4, RT_TOUCH_EVENT_DOWN, 200, 100);
  assert(pocketjs_aic_touch_sample(touch, contacts, 8) == 0);
  assert(pocketjs_aic_touch_sample(touch, contacts, 8) == 1);
  assert(contacts[0].x == 200 && contacts[0].y == 100);
  report(4, RT_TOUCH_EVENT_UP, 200, 100);
  assert(pocketjs_aic_touch_sample(touch, contacts, 8) == 0);

  /* A short tap drained together is delivered once, with a lift frame even
   * if the hardware has already queued another down using the same id. */
  report(0, RT_TOUCH_EVENT_DOWN, 20, 30);
  report(0, RT_TOUCH_EVENT_UP, 20, 30);
  report(0, RT_TOUCH_EVENT_DOWN, 100, 110);
  assert(pocketjs_aic_touch_sample(touch, contacts, 8) == 1);
  assert(contacts[0].x == 20);
  assert(pocketjs_aic_touch_sample(touch, contacts, 8) == 0);
  assert(pocketjs_aic_touch_sample(touch, contacts, 8) == 1);
  assert(contacts[0].x == 100);
  report(4, RT_TOUCH_EVENT_DOWN, 900, 500);
  assert(pocketjs_aic_touch_sample(touch, contacts, 8) == 2);
  assert(contacts[0].id != contacts[1].id);
  assert(pocketjs_aic_touch_sample(touch, contacts, 1) == 1);
  report(0, RT_TOUCH_EVENT_UP, 100, 110);
  assert(pocketjs_aic_touch_sample(touch, contacts, 8) == 1);
  assert(contacts[0].id == 4);
  report(4, RT_TOUCH_EVENT_UP, 900, 500);
  assert(pocketjs_aic_touch_sample(touch, contacts, 8) == 0);
  assert(allocations == opened_allocations);
  pocketjs_aic_touch_close(touch);

  /* GT911's resident config can already use viewport coordinates. GET_INFO
   * must describe these reports; a 1024x600 default would miss this button. */
  panel_range_x = 480;
  panel_range_y = 272;
  touch = pocketjs_aic_touch_open("gt911");
  assert(touch);
  pocketjs_aic_touch_range(touch, &x, &y);
  assert(x == 480 && y == 272);
  report(0, RT_TOUCH_EVENT_DOWN, 70, 245);
  assert(pocketjs_aic_touch_sample(touch, contacts, 8) == 1);
  assert(contacts[0].x * 480U / x == 70 && contacts[0].y * 272U / y == 245);
  report(0, RT_TOUCH_EVENT_UP, 70, 245);
  assert(pocketjs_aic_touch_sample(touch, contacts, 8) == 0);
  pocketjs_aic_touch_close(touch);
}

static void test_time(void) {
  const rt_tick_t boot = 0xffff0000U;
  assert(!pocketjs_aic_tick_reached(boot, 0)); /* Old calibration dead zone. */
  assert(pocketjs_aic_tick_reached(boot, boot));
  assert(pocketjs_aic_ticks_until(0xfffffff8U, 8U) == 16);
  assert(pocketjs_aic_ticks_until(8U, 0xfffffff8U) == 0);
  assert(pocketjs_aic_tick_reached(8U, 0xfffffff8U));
  /* Reproduce the old pacing expression's long sleep after tick wrap. */
  volatile rt_tick_t old_deadline = 0xfffffff8U, now = 8U;
  rt_tick_t old_ms = old_deadline > now ? (old_deadline - now) * 1000U / 1000U : 0U;
  assert(old_ms == 4294951U);
  printf("old wrap sleep=%u ms, fixed=%u ticks\n", old_ms,
         pocketjs_aic_ticks_until(now, old_deadline));
  for (uint32_t elapsed = 0; elapsed < 120000; elapsed++) {
    rt_tick_t tick = boot + elapsed, deadline = tick + 16;
    assert(pocketjs_aic_ticks_until(tick, deadline) == 16);
    assert(pocketjs_aic_ticks_until(tick + 10, deadline) == 6);
    assert(pocketjs_aic_ticks_until(tick + 20, deadline) == 0);
    assert(pocketjs_aic_tick_reached(tick + 20, deadline));
  }
}

int main(void) {
  test_time();
  test_touch();
  puts("AIC touch snapshots and tick wrap regressions passed");
  return 0;
}
