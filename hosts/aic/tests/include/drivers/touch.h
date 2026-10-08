#ifndef POCKETJS_TEST_TOUCH_H
#define POCKETJS_TEST_TOUCH_H
#include <stdint.h>
/* Match RT-Thread's event encoding and sparse per-track report slots. */
#define RT_TOUCH_EVENT_NONE 0
#define RT_TOUCH_EVENT_UP 1
#define RT_TOUCH_EVENT_DOWN 2
#define RT_TOUCH_EVENT_MOVE 3
#define RT_TOUCH_CTRL_GET_INFO 3
struct rt_touch_info {
  uint8_t type, vendor, point_num;
  int32_t range_x, range_y;
};
struct rt_touch_data {
  uint8_t event, track_id, width;
  uint16_t x_coordinate, y_coordinate;
  uint32_t timestamp;
};
#endif
