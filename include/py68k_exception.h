/* 2026 by Piotr Rozentreter (Rozsoft) */

#ifndef PY68K_EXCEPTION_H
#define PY68K_EXCEPTION_H

#include "py68k_error.h"
#include "py68k_object.h"
#include "py68k_status.h"

struct Py68Type;

struct Py68Exception {
    Py68Object base;
    struct Py68Type *type; /* retained; may be NULL for legacy paths */
    Py68U16 kind;
    char message[192];
};
typedef struct Py68Exception Py68Exception;

Py68Status py68_exception_new(struct Py68Runtime *runtime, Py68U16 kind,
                              const char *message, Py68Exception **result);
Py68Status py68_exception_new_typed(struct Py68Runtime *runtime,
                                    struct Py68Type *type, Py68U16 kind,
                                    const char *message,
                                    Py68Exception **result);
Py68Status py68_exception_from_error(struct Py68Runtime *runtime,
                                     Py68Exception **result);
int py68_exception_matches(struct Py68Runtime *runtime,
                           Py68Exception *exception, Py68Value matcher);
const char *py68_error_kind_name(Py68U16 kind);
Py68U16 py68_error_kind_from_name(const char *name, Py68U16 length);

#endif
