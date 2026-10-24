/* PocketCOM `com.*` serial bridge: one UART ("uart1", PA.2/PA.3) exposed to
 * the guest through the globalThis.com namespace bridge/com.ts probes.
 *
 * Contract mapping (mirror of host/macos com_serial.rs semantics):
 *   serialList()                  -> JSON array string
 *   serialOpen(paramsJson)        -> {"handle":N} or {"error":{code,msg}}
 *   write(handle, uint8array)     -> bool (queued; false on short write)
 *   setSignals(handle, pinsJson)  -> bool (accepted, no modem lines wired)
 *   close(handle)                 -> bool
 *   poll()                        -> newline-delimited JSON events or null
 *                                    ({"t":"data","h":N,"b64":"..."})
 *
 * Data crosses into JS only when the guest calls poll() during a turn, so
 * no host callback ever re-enters QuickJS. The device ring (2048 bytes,
 * driver-owned) covers a frame of data up to ~921600 baud at 60 Hz ticks;
 * higher line rates can overflow between polls. Flow control and DTR/RTS
 * are accepted and ignored: demo68 wires none of those pins. */
#include <rtconfig.h>
#include <rtthread.h>
#include <rtdevice.h>

#include <stdio.h>
#include <string.h>

#include "pocketjs/guest.h"
#include "pocketjs/guest_quickjs.h"
#include "quickjs.h"

#include "pocketjs_com.h"

/* aic_hal_uart.h rejects bauds above this (AIC_UART_MAX_BAUDRATE); bauds
 * outside the driver's per-rate clock table fall back to the 48 MHz menu
 * clock, which still divides correctly for standard rates. */
#define POCKETJS_COM_BAUD_MAX 8000000U
#define POCKETJS_COM_BAUD_MIN 300U

/* Matches the driver ring size: one poll can never usefully read more new
 * bytes than the device buffered since the previous poll. */
#define POCKETJS_COM_RX_CHUNK 2048U

/* This product ships exactly one port: the UART1 device on PA.2/PA.3.
 * uart0 stays the finsh console and is deliberately not listed. */
static const char pocketjs_com_port_name[] = "uart1";
static const char pocketjs_com_port_list[] =
    "[{\"path\":\"uart1\",\"description\":\"AIC D12x UART1 (PA.2/PA.3)\","
    "\"manufacturer\":\"ArtInChip\"}]";

static struct {
  rt_device_t dev;
  int32_t handle;
} pocketjs_com_state;

static JSValue pocketjs_com_error(JSContext *ctx, const char *code,
                                  const char *msg) {
  char buf[192];
  int len = snprintf(buf, sizeof(buf),
                     "{\"error\":{\"code\":\"%s\",\"msg\":\"%s\"}}", code, msg);
  if (len < 0 || (size_t)len >= sizeof(buf)) {
    len = (int)sizeof(buf) - 1;
  }
  return JS_NewStringLen(ctx, buf, (size_t)len);
}

static size_t pocketjs_com_b64(const uint8_t *input, size_t size, char *out) {
  static const char alphabet[] =
      "ABCDEFGHIJKLMNOPQRSTUVWXYZabcdefghijklmnopqrstuvwxyz0123456789+/";
  size_t written = 0U;
  size_t index = 0U;
  while (index + 3U <= size) {
    uint32_t group = ((uint32_t)input[index] << 16) |
                     ((uint32_t)input[index + 1U] << 8) |
                     (uint32_t)input[index + 2U];
    out[written++] = alphabet[(group >> 18) & 0x3FU];
    out[written++] = alphabet[(group >> 12) & 0x3FU];
    out[written++] = alphabet[(group >> 6) & 0x3FU];
    out[written++] = alphabet[group & 0x3FU];
    index += 3U;
  }
  if (index < size) {
    uint32_t group = (uint32_t)input[index] << 16;
    if (index + 1U < size) {
      group |= (uint32_t)input[index + 1U] << 8;
    }
    out[written++] = alphabet[(group >> 18) & 0x3FU];
    out[written++] = alphabet[(group >> 12) & 0x3FU];
    out[written++] = index + 1U < size ? alphabet[(group >> 6) & 0x3FU] : '=';
    out[written++] = '=';
  }
  return written;
}

static JSValue js_com_serial_list(JSContext *ctx, JSValueConst this_value,
                                  int argc, JSValueConst *argv, int magic,
                                  void *opaque) {
  (void)this_value;
  (void)argc;
  (void)argv;
  (void)magic;
  (void)opaque;
  return JS_NewString(ctx, pocketjs_com_port_list);
}

/* Drop one owned reference and park the slot so the shared done: cleanup
 * never double-frees after an early return. */
static void pocketjs_com_drop(JSContext *ctx, JSValue *value) {
  JS_FreeValue(ctx, *value);
  *value = JS_UNDEFINED;
}

static JSValue js_com_serial_open(JSContext *ctx, JSValueConst this_value,
                                  int argc, JSValueConst *argv, int magic,
                                  void *opaque) {
  (void)this_value;
  (void)magic;
  (void)opaque;
  rt_device_t dev = RT_NULL;
  struct serial_configure config;
  char *params = RT_NULL;
  JSValue parsed = JS_UNDEFINED;
  JSValue field = JS_UNDEFINED;
  JSValue result = JS_UNDEFINED;
  int baud = 0;
  int data_bits = 8;
  int parity = PARITY_NONE;
  int stop_bits = STOP_BITS_1;

  if (pocketjs_com_state.dev != RT_NULL) {
    return pocketjs_com_error(ctx, "io-error", "uart1 is already open");
  }
  if (argc < 1 || !JS_IsString(argv[0])) {
    return pocketjs_com_error(ctx, "invalid-param",
                              "open params must be a json string");
  }
  size_t params_size = 0U;
  params = (char *)JS_ToCStringLen(ctx, &params_size, argv[0]);
  if (params == RT_NULL) {
    return JS_EXCEPTION;
  }
  parsed = JS_ParseJSON(ctx, params, params_size, "com.serialOpen");
  JS_FreeCString(ctx, params);
  if (JS_IsException(parsed)) {
    return pocketjs_com_error(ctx, "invalid-param",
                              "open params are not valid json");
  }

  /* path: this host exposes exactly one device; anything else reads as
   * not-found rather than invalid-param, matching the desktop mapping of
   * unknown port names. */
  field = JS_GetPropertyStr(ctx, parsed, "path");
  if (!JS_IsString(field)) {
    pocketjs_com_drop(ctx, &field);
    result = pocketjs_com_error(ctx, "invalid-param", "path is required");
    goto done;
  }
  pocketjs_com_drop(ctx, &field);

  field = JS_GetPropertyStr(ctx, parsed, "baudRate");
  if (!JS_IsNumber(field) || JS_ToInt32(ctx, &baud, field) != 0) {
    pocketjs_com_drop(ctx, &field);
    result = pocketjs_com_error(ctx, "invalid-param", "baudRate is required");
    goto done;
  }
  pocketjs_com_drop(ctx, &field);
  if (baud < (int)POCKETJS_COM_BAUD_MIN || baud > (int)POCKETJS_COM_BAUD_MAX) {
    result = pocketjs_com_error(ctx, "invalid-param",
                                "baudRate must be 300..8000000");
    goto done;
  }

  field = JS_GetPropertyStr(ctx, parsed, "dataBits");
  if (!JS_IsUndefined(field)) {
    int bits = 0;
    if (!JS_IsNumber(field) || JS_ToInt32(ctx, &bits, field) != 0 ||
        bits < 5 || bits > 8) {
      pocketjs_com_drop(ctx, &field);
      result = pocketjs_com_error(ctx, "invalid-param", "dataBits must be 5..8");
      goto done;
    }
    data_bits = bits;
  }
  pocketjs_com_drop(ctx, &field);

  field = JS_GetPropertyStr(ctx, parsed, "parity");
  if (!JS_IsUndefined(field)) {
    if (!JS_IsString(field)) {
      pocketjs_com_drop(ctx, &field);
      result = pocketjs_com_error(ctx, "invalid-param", "parity must be a string");
      goto done;
    }
    const char *value = JS_ToCString(ctx, field);
    if (value == RT_NULL) {
      pocketjs_com_drop(ctx, &field);
      result = JS_EXCEPTION;
      goto done;
    }
    if (!strcmp(value, "odd")) {
      parity = PARITY_ODD;
    } else if (!strcmp(value, "even")) {
      parity = PARITY_EVEN;
    } else if (strcmp(value, "none") != 0) {
      /* mark/space exist in the contract but not in this UART's config
       * path; refuse instead of silently downgrading the line format. */
      JS_FreeCString(ctx, value);
      pocketjs_com_drop(ctx, &field);
      result = pocketjs_com_error(ctx, "invalid-param",
                                  "parity must be none|odd|even");
      goto done;
    }
    JS_FreeCString(ctx, value);
  }
  pocketjs_com_drop(ctx, &field);

  field = JS_GetPropertyStr(ctx, parsed, "stopBits");
  if (!JS_IsUndefined(field)) {
    if (JS_IsNumber(field)) {
      int bits = 0;
      if (JS_ToInt32(ctx, &bits, field) != 0 || (bits != 1 && bits != 2)) {
        pocketjs_com_drop(ctx, &field);
        result = pocketjs_com_error(ctx, "invalid-param",
                                    "stopBits must be 1 or 2");
        goto done;
      }
      stop_bits = bits == 1 ? STOP_BITS_1 : STOP_BITS_2;
    } else if (JS_IsString(field)) {
      const char *value = JS_ToCString(ctx, field);
      if (value == RT_NULL) {
        pocketjs_com_drop(ctx, &field);
        result = JS_EXCEPTION;
        goto done;
      }
      if (!strcmp(value, "1")) {
        stop_bits = STOP_BITS_1;
      } else if (!strcmp(value, "2")) {
        stop_bits = STOP_BITS_2;
      } else {
        JS_FreeCString(ctx, value);
        pocketjs_com_drop(ctx, &field);
        result = pocketjs_com_error(ctx, "invalid-param",
                                    "stopBits must be 1 or 2 (\"1.5\" unsupported)");
        goto done;
      }
      JS_FreeCString(ctx, value);
    } else {
      pocketjs_com_drop(ctx, &field);
      result = pocketjs_com_error(ctx, "invalid-param", "stopBits must be 1 or 2");
      goto done;
    }
  }
  pocketjs_com_drop(ctx, &field);

  field = JS_GetPropertyStr(ctx, parsed, "flowControl");
  if (!JS_IsUndefined(field)) {
    const char *value = JS_IsString(field) ? JS_ToCString(ctx, field) : RT_NULL;
    int supported = value != RT_NULL && !strcmp(value, "none");
    if (value != RT_NULL) {
      JS_FreeCString(ctx, value);
    }
    pocketjs_com_drop(ctx, &field);
    if (!supported) {
      result = pocketjs_com_error(ctx, "invalid-param",
                                  "flowControl is not supported on this host");
      goto done;
    }
  } else {
    pocketjs_com_drop(ctx, &field);
  }

  dev = rt_device_find(pocketjs_com_port_name);
  if (dev == RT_NULL || dev->type != RT_Device_Class_Char) {
    result = pocketjs_com_error(ctx, "not-found", "uart1 is not registered");
    goto done;
  }

  /* GET -> mutate line format -> CONFIG preserves the driver-owned fields
   * (bufsz, uart_index, uart_freq, function); the framework applies the
   * merged config when the device opens. */
  if (rt_device_control(dev, RT_SERIAL_GET_CONFIG, &config) != RT_EOK) {
    result = pocketjs_com_error(ctx, "io-error", "reading uart1 config failed");
    goto done;
  }
  config.baud_rate = (uint32_t)baud;
  config.data_bits = data_bits;
  config.parity = parity;
  config.stop_bits = stop_bits;
  if (rt_device_control(dev, RT_DEVICE_CTRL_CONFIG, &config) != RT_EOK) {
    result = pocketjs_com_error(ctx, "io-error", "configuring uart1 failed");
    goto done;
  }
  if (rt_device_open(dev, RT_DEVICE_OFLAG_RDWR | RT_DEVICE_FLAG_INT_RX) != RT_EOK) {
    result = pocketjs_com_error(ctx, "io-error", "opening uart1 failed");
    goto done;
  }

  pocketjs_com_state.dev = dev;
  pocketjs_com_state.handle = 1;
  result = JS_NewString(ctx, "{\"handle\":1}");
done:
  JS_FreeValue(ctx, parsed);
  JS_FreeValue(ctx, field);
  return result;
}

static JSValue js_com_set_signals(JSContext *ctx, JSValueConst this_value,
                                  int argc, JSValueConst *argv, int magic,
                                  void *opaque) {
  (void)this_value;
  (void)argc;
  (void)argv;
  (void)magic;
  (void)opaque;
  /* DTR/RTS have no wired modem lines here; report success so the guest's
   * signal toggles stay no-ops rather than connection errors. */
  return JS_NewBool(ctx, pocketjs_com_state.dev != RT_NULL &&
                              pocketjs_com_state.handle == 1);
}

static JSValue js_com_write(JSContext *ctx, JSValueConst this_value,
                            int argc, JSValueConst *argv, int magic,
                            void *opaque) {
  (void)this_value;
  (void)magic;
  (void)opaque;
  int32_t handle = 0;
  if (argc < 2 || JS_ToInt32(ctx, &handle, argv[0]) != 0 ||
      pocketjs_com_state.dev == RT_NULL || handle != pocketjs_com_state.handle) {
    return JS_FALSE;
  }
  size_t size = 0U;
  const uint8_t *bytes = JS_GetUint8Array(ctx, &size, argv[1]);
  if (bytes == RT_NULL) {
    return JS_FALSE;
  }
  rt_size_t written = rt_device_write(pocketjs_com_state.dev, 0, bytes,
                                      (rt_size_t)size);
  return JS_NewBool(ctx, (size_t)written == size);
}

static JSValue js_com_close(JSContext *ctx, JSValueConst this_value,
                            int argc, JSValueConst *argv, int magic,
                            void *opaque) {
  (void)this_value;
  (void)magic;
  (void)opaque;
  int32_t handle = 0;
  if (argc < 1 || JS_ToInt32(ctx, &handle, argv[0]) != 0 ||
      pocketjs_com_state.dev == RT_NULL || handle != pocketjs_com_state.handle) {
    return JS_FALSE;
  }
  if (rt_device_close(pocketjs_com_state.dev) != RT_EOK) {
    return JS_FALSE;
  }
  pocketjs_com_state.dev = RT_NULL;
  pocketjs_com_state.handle = 0;
  return JS_TRUE;
}

static JSValue js_com_poll(JSContext *ctx, JSValueConst this_value,
                           int argc, JSValueConst *argv, int magic,
                           void *opaque) {
  (void)this_value;
  (void)argc;
  (void)argv;
  (void)magic;
  (void)opaque;
  uint8_t buffer[POCKETJS_COM_RX_CHUNK];
  rt_size_t total = 0U;

  if (pocketjs_com_state.dev == RT_NULL) {
    return JS_NULL;
  }
  while (total < (rt_size_t)sizeof(buffer)) {
    rt_size_t chunk = rt_device_read(pocketjs_com_state.dev, 0, buffer + total,
                                     (rt_size_t)sizeof(buffer) - total);
    if (chunk == 0U) {
      break;
    }
    total += chunk;
  }
  if (total == 0U) {
    return JS_NULL;
  }

  /* One data event per poll: b64 expands 4/3 and the JSON envelope adds a
   * fixed prefix, so the buffer holds the worst case with slack. */
  char *line = (char *)js_malloc(ctx, (size_t)sizeof(buffer) * 4U / 3U + 48U);
  if (line == RT_NULL) {
    return JS_ThrowOutOfMemory(ctx);
  }
  int head = snprintf(line, 40, "{\"t\":\"data\",\"h\":%d,\"b64\":\"",
                      (int)pocketjs_com_state.handle);
  size_t encoded = pocketjs_com_b64(buffer, total, line + head);
  size_t cursor = (size_t)head + encoded;
  line[cursor++] = '"';
  line[cursor++] = '}';
  line[cursor++] = '\n';
  JSValue event = JS_NewStringLen(ctx, line, cursor);
  js_free(ctx, line);
  return event;
}

static esp_err_t pocketjs_com_install(JSContext *context, void *user_data) {
  (void)user_data;
  JSValue global = JS_UNDEFINED;
  JSValue com = JS_UNDEFINED;
  JSAtom com_atom = JS_NewAtom(context, "com");
  esp_err_t result = ESP_FAIL;
  if (com_atom == JS_ATOM_NULL) {
    return ESP_ERR_NO_MEM;
  }
  global = JS_GetGlobalObject(context);
  if (JS_IsException(global) || JS_HasProperty(context, global, com_atom)) {
    /* A stock surface named com would mean a conflicting host contract. */
    result = ESP_ERR_INVALID_STATE;
    goto done;
  }
  com = JS_NewObject(context);
  if (JS_IsException(com)) {
    result = ESP_ERR_NO_MEM;
    goto done;
  }
  JS_SetPropertyStr(context, com, "serialList",
                    JS_NewCClosure(context, js_com_serial_list, "serialList",
                                   NULL, 0, 0, NULL));
  JS_SetPropertyStr(context, com, "serialOpen",
                    JS_NewCClosure(context, js_com_serial_open, "serialOpen",
                                   NULL, 1, 0, NULL));
  JS_SetPropertyStr(context, com, "setSignals",
                    JS_NewCClosure(context, js_com_set_signals, "setSignals",
                                   NULL, 2, 0, NULL));
  JS_SetPropertyStr(context, com, "write",
                    JS_NewCClosure(context, js_com_write, "write",
                                   NULL, 2, 0, NULL));
  JS_SetPropertyStr(context, com, "close",
                    JS_NewCClosure(context, js_com_close, "close",
                                   NULL, 1, 0, NULL));
  JS_SetPropertyStr(context, com, "poll",
                    JS_NewCClosure(context, js_com_poll, "poll",
                                   NULL, 0, 0, NULL));
  if (JS_HasException(context)) {
    result = ESP_ERR_INVALID_STATE;
    goto done;
  }
  if (JS_DefinePropertyValue(context, global, com_atom, com,
                             JS_PROP_C_W_E) < 0) {
    com = JS_UNDEFINED; /* Consumed on the failure path too. */
    result = ESP_ERR_INVALID_STATE;
    goto done;
  }
  com = JS_UNDEFINED;
  result = ESP_OK;
done:
  JS_FreeAtom(context, com_atom);
  JS_FreeValue(context, com);
  JS_FreeValue(context, global);
  return result;
}

esp_err_t pocketjs_aic_com_install(pocketjs_guest_t *guest) {
  if (guest == RT_NULL) {
    return ESP_ERR_INVALID_ARG;
  }
  return pocketjs_guest_quickjs_install_once(guest, "pocketcom.com",
                                             pocketjs_com_install, RT_NULL);
}

void pocketjs_aic_com_shutdown(void) {
  if (pocketjs_com_state.dev != RT_NULL) {
    rt_device_close(pocketjs_com_state.dev);
    pocketjs_com_state.dev = RT_NULL;
    pocketjs_com_state.handle = 0;
  }
}
