/* 2026 by Piotr Rozentreter (Rozsoft) */

#include "py68k_exception.h"
#include "py68k_native.h"
#include "py68k_runtime.h"
#include "py68k_type.h"

#include <string.h>

const char *py68_error_kind_name(Py68U16 kind)
{
    switch (kind) {
    case PY68_ERROR_TOKEN: return "TokenError";
    case PY68_ERROR_SYNTAX: return "SyntaxError";
    case PY68_ERROR_NAME: return "NameError";
    case PY68_ERROR_TYPE: return "TypeError";
    case PY68_ERROR_VALUE: return "ValueError";
    case PY68_ERROR_INDEX: return "IndexError";
    case PY68_ERROR_KEY: return "KeyError";
    case PY68_ERROR_ZERO_DIVISION: return "ZeroDivisionError";
    case PY68_ERROR_OVERFLOW: return "OverflowError";
    case PY68_ERROR_RECURSION: return "RecursionError";
    case PY68_ERROR_MEMORY: return "MemoryError";
    case PY68_ERROR_IO: return "OSError";
    case PY68_ERROR_IMPORT: return "ImportError";
    case PY68_ERROR_BYTECODE: return "BytecodeError";
    case PY68_ERROR_INTERNAL: return "InternalError";
    case PY68_ERROR_INTERRUPT: return "KeyboardInterrupt";
    case PY68_ERROR_STOP_ITERATION: return "StopIteration";
    case PY68_ERROR_EXCEPTION: return "Exception";
    default: return "Exception";
    }
}

Py68U16 py68_error_kind_from_name(const char *name, Py68U16 length)
{
    if (length == 9 && memcmp(name, "TypeError", 9) == 0)
        return PY68_ERROR_TYPE;
    if (length == 10 && memcmp(name, "ValueError", 10) == 0)
        return PY68_ERROR_VALUE;
    if (length == 10 && memcmp(name, "IndexError", 10) == 0)
        return PY68_ERROR_INDEX;
    if (length == 8 && memcmp(name, "KeyError", 8) == 0)
        return PY68_ERROR_KEY;
    if (length == 17 && memcmp(name, "ZeroDivisionError", 17) == 0)
        return PY68_ERROR_ZERO_DIVISION;
    if (length == 13 && memcmp(name, "OverflowError", 13) == 0)
        return PY68_ERROR_OVERFLOW;
    if (length == 9 && memcmp(name, "NameError", 9) == 0)
        return PY68_ERROR_NAME;
    /* Python 3 primary name; IOError remains an accepted alias (D-0044). */
    if (length == 7 && memcmp(name, "OSError", 7) == 0)
        return PY68_ERROR_IO;
    if (length == 7 && memcmp(name, "IOError", 7) == 0)
        return PY68_ERROR_IO;
    if (length == 14 && memcmp(name, "RecursionError", 14) == 0)
        return PY68_ERROR_RECURSION;
    if (length == 11 && memcmp(name, "ImportError", 11) == 0)
        return PY68_ERROR_IMPORT;
    if (length == 13 && memcmp(name, "StopIteration", 13) == 0)
        return PY68_ERROR_STOP_ITERATION;
    return PY68_ERROR_INTERNAL;
}

Py68Status py68_exception_new_typed(Py68Runtime *runtime, Py68Type *type,
                                    Py68U16 kind, const char *message,
                                    Py68Exception **result)
{
    Py68Exception *exception = (Py68Exception *)py68_alloc(
        &runtime->allocator, PY68_MEM_RUNTIME, sizeof(Py68Exception));
    if (exception == NULL) return PY68_STATUS_MEMORY_ERROR;
    exception->base.type = PY68_OBJECT_EXCEPTION;
    exception->base.flags = 0;
    exception->base.reference_count = 1;
    exception->base.next_object = runtime->live_objects;
    runtime->live_objects = &exception->base;
    exception->type = type;
    if (type != NULL)
        py68_object_retain(&type->base);
    exception->kind = kind;
    exception->message[0] = '\0';
    if (message != NULL) {
        strncpy(exception->message, message, sizeof(exception->message) - 1);
        exception->message[sizeof(exception->message) - 1] = '\0';
    }
    *result = exception;
    return PY68_STATUS_OK;
}

Py68Status py68_exception_new(Py68Runtime *runtime, Py68U16 kind,
                              const char *message, Py68Exception **result)
{
    Py68Type *type = py68_type_for_error_kind(runtime, kind);
    return py68_exception_new_typed(runtime, type, kind, message, result);
}

Py68Status py68_exception_from_error(Py68Runtime *runtime,
                                     Py68Exception **result)
{
    return py68_exception_new(runtime, runtime->error.kind,
                              runtime->error.message, result);
}

int py68_exception_matches(Py68Runtime *runtime, Py68Exception *exception,
                           Py68Value matcher)
{
    Py68NativeFunction *function;
    Py68Type *matcher_type;
    Py68Type *exc_type;
    if (exception == NULL) return 0;
    if (matcher.type != PY68_VALUE_OBJECT || matcher.as.object == NULL)
        return 0;
    if (matcher.as.object->type == PY68_OBJECT_TYPE) {
        matcher_type = (Py68Type *)matcher.as.object;
        if ((matcher_type->flags & PY68_TYPE_FLAG_EXCEPTION) == 0) return 0;
        exc_type = exception->type;
        if (exc_type == NULL && runtime != NULL)
            exc_type = py68_type_for_error_kind(runtime, exception->kind);
        if (exc_type != NULL)
            return py68_type_is_subtype(exc_type, matcher_type);
        if (matcher_type->exception_kind != 0)
            return matcher_type->exception_kind == exception->kind;
        return 0;
    }
    if (matcher.as.object->type == PY68_OBJECT_NATIVE_FUNCTION) {
        function = (Py68NativeFunction *)matcher.as.object;
        return function->base.flags == exception->kind;
    }
    if (matcher.as.object->type == PY68_OBJECT_EXCEPTION)
        return ((Py68Exception *)matcher.as.object)->kind == exception->kind;
    return 0;
}
