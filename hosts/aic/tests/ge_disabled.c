/* Host-side regression for port/pocketjs_ge.c built with
 * -DPOCKETJS_AIC_GE_DISABLE (the POCKETJS_GE=0 build): the object stays
 * allocatable but never exposes an accelerator, so the renderer keeps every
 * pixel on the CPU. */
#include <assert.h>
#include <stdarg.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include <rtthread.h>

#include "pocketjs_ge.h"

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

int main(void) {
  struct pocketjs_aic_ge *ge = pocketjs_aic_ge_open();
  assert(ge != NULL);
  assert(pocketjs_aic_ge_accelerator(ge) == NULL);
  assert(strstr(kprintf_log, "compiled out") != NULL);
  pocketjs_aic_ge_close(ge);
  pocketjs_aic_ge_close(NULL);
  printf("AIC GE compiled-out regressions passed\n");
  return 0;
}
