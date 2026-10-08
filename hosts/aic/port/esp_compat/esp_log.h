/* ESP_LOGx mapped to the RT-Thread console. */
#ifndef POCKETJS_AIC_ESP_LOG_H
#define POCKETJS_AIC_ESP_LOG_H

#include <rtthread.h>

/* The tags are `static const char *` variables in the components, so the tag
 * rides as a %s argument; the format string itself is always a literal. */
#define ESP_LOG_FORMAT(tag, fmt, ...) \
    rt_kprintf("[%s] " fmt "\n", tag, ##__VA_ARGS__)

#define ESP_LOGE(tag, fmt, ...) ESP_LOG_FORMAT(tag, fmt, ##__VA_ARGS__)
#define ESP_LOGW(tag, fmt, ...) ESP_LOG_FORMAT(tag, fmt, ##__VA_ARGS__)
#define ESP_LOGI(tag, fmt, ...) ESP_LOG_FORMAT(tag, fmt, ##__VA_ARGS__)
#define ESP_LOGD(tag, fmt, ...) ESP_LOG_FORMAT(tag, fmt, ##__VA_ARGS__)
#define ESP_LOGV(tag, fmt, ...) ESP_LOG_FORMAT(tag, fmt, ##__VA_ARGS__)

#endif /* POCKETJS_AIC_ESP_LOG_H */
