#ifndef POCKETJS_TEST_RTDEVICE_H
#define POCKETJS_TEST_RTDEVICE_H
#include <rtthread.h>
typedef struct test_device *rt_device_t;
typedef rt_err_t (*rt_rx_indicate_t)(rt_device_t device, rt_size_t size);
#define RT_DEVICE_FLAG_INT_RX 0x100
rt_device_t rt_device_find(const char *name);
rt_err_t rt_device_open(rt_device_t device, unsigned flag);
rt_err_t rt_device_close(rt_device_t device);
rt_err_t rt_device_control(rt_device_t device, int command, void *argument);
rt_err_t rt_device_set_rx_indicate(rt_device_t device, rt_rx_indicate_t callback);
rt_size_t rt_device_read(rt_device_t device, int position, void *buffer, rt_size_t size);
#endif
