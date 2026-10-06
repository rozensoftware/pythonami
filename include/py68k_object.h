/* 2026 by Piotr Rozentreter (Rozsoft) */

#ifndef PY68K_OBJECT_H
#define PY68K_OBJECT_H

#include "py68k_memory.h"
#include "py68k_value.h"

struct Py68Runtime;

typedef enum Py68ObjectType {
    PY68_OBJECT_STRING = 1,
    PY68_OBJECT_LIST,
    PY68_OBJECT_CODE,
    PY68_OBJECT_FUNCTION,
    PY68_OBJECT_NATIVE_FUNCTION,
    PY68_OBJECT_RANGE,
    PY68_OBJECT_FILE,
    PY68_OBJECT_TUPLE,
    PY68_OBJECT_DICT,
    PY68_OBJECT_SET,
    PY68_OBJECT_BOUND_METHOD,
    PY68_OBJECT_EXCEPTION,
    PY68_OBJECT_MODULE,
    PY68_OBJECT_STRUCT_TIME,
    PY68_OBJECT_GENERATOR,
    PY68_OBJECT_TYPE,
    PY68_OBJECT_INSTANCE
} Py68ObjectType;

struct Py68Object {
    Py68U16 type;
    Py68U16 flags;
    Py68U32 reference_count;
    struct Py68Object *next_object;
};

void py68_object_retain(Py68Object *object);
void py68_object_release(struct Py68Runtime *runtime, Py68Object *object);

#endif
