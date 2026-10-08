/* Minimal esp_err surface for the portable PocketJS components on RT-Thread.
 * The components treat it as an int error code; the names are part of the
 * reused API, so this shim keeps the esp-idf sources compiling unmodified. */
#ifndef POCKETJS_AIC_ESP_ERR_H
#define POCKETJS_AIC_ESP_ERR_H

typedef int esp_err_t;

#define ESP_OK 0
#define ESP_FAIL -1
#define ESP_ERR_NO_MEM 0x101
#define ESP_ERR_INVALID_ARG 0x102
#define ESP_ERR_INVALID_STATE 0x103
#define ESP_ERR_INVALID_SIZE 0x104
#define ESP_ERR_INVALID_VERSION 0x105
#define ESP_ERR_INVALID_RESPONSE 0x106
#define ESP_ERR_NOT_FOUND 0x107
#define ESP_ERR_INVALID_CRC 0x109
#define ESP_ERR_TIMEOUT 0x108

#endif /* POCKETJS_AIC_ESP_ERR_H */
