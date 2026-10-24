/* PocketCOM `com.*` serial bridge for the ArtInChip port.
 *
 * Mounts the globalThis.com namespace the PocketCOM guest probes (bridge/
 * com.ts) with the serial-only surface this product ships: one UART
 * ("uart1" on PA.2/PA.3), poll-driven events, no net/cfg/mcp ops. Events
 * cross into JS only through com.poll() at tick boundaries; the receive
 * path never calls back into the guest. Compile out with POCKETJS_COM=0. */
#ifndef POCKETJS_AIC_COM_H
#define POCKETJS_AIC_COM_H

#include "esp_err.h"
#include "pocketjs/guest.h"

#ifdef __cplusplus
extern "C" {
#endif

/** Install globalThis.com on the guest context. One bridge per process;
 * a second install or an existing "com" global returns ESP_ERR_INVALID_STATE. */
esp_err_t pocketjs_aic_com_install(pocketjs_guest_t *guest);

/** Close the UART if the guest left it open (host task teardown path). */
void pocketjs_aic_com_shutdown(void);

#ifdef __cplusplus
}
#endif

#endif /* POCKETJS_AIC_COM_H */
