/* 2026 by Piotr Rozentreter (Rozsoft) */

#ifndef PY68K_ERROR_H
#define PY68K_ERROR_H

#include "py68k_location.h"
#include "py68k_types.h"

typedef enum Py68ErrorKind {
    PY68_ERROR_NONE = 0,
    PY68_ERROR_TOKEN,
    PY68_ERROR_SYNTAX,
    PY68_ERROR_NAME,
    PY68_ERROR_TYPE,
    PY68_ERROR_VALUE,
    PY68_ERROR_INDEX,
    PY68_ERROR_ZERO_DIVISION,
    PY68_ERROR_OVERFLOW,
    PY68_ERROR_RECURSION,
    PY68_ERROR_MEMORY,
    PY68_ERROR_IO,
    PY68_ERROR_KEY,
    PY68_ERROR_IMPORT,
    PY68_ERROR_BYTECODE,
    PY68_ERROR_INTERNAL,
    /* Appended last: existing kind numbers must stay stable. */
    PY68_ERROR_INTERRUPT,
    PY68_ERROR_STOP_ITERATION,
    /* Catchable stand-in kind for user-defined exception instances that have
       a real type pointer but no builtin PY68_ERROR_* mapping (Level 0.8). */
    PY68_ERROR_EXCEPTION
} Py68ErrorKind;

typedef struct Py68Error {
    Py68U16 kind;
    Py68U16 active;
    Py68Location location;
    const char *filename;
    char message[192];
} Py68Error;

void py68_error_clear(Py68Error *error);
void py68_error_set(Py68Error *error, Py68ErrorKind kind,
                    Py68Location location, const char *filename,
                    const char *message);

#endif
