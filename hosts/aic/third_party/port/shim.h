/* Force-included before the vendored QuickJS sources on RT-Thread: the newlib
 * headers miss two names the restricted-POSIX (WASI) profile references. */
#ifndef POCKETJS_QJS_SHIM_H
#define POCKETJS_QJS_SHIM_H

#include <stdlib.h>

typedef void (*sighandler_t)(int);

extern char **environ;

#endif /* POCKETJS_QJS_SHIM_H */
