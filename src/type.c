/* 2026 by Piotr Rozentreter (Rozsoft) */

#include "py68k_type.h"

#include "py68k_builtin.h"
#include "py68k_dict.h"
#include "py68k_error.h"
#include "py68k_exception.h"
#include "py68k_native.h"
#include "py68k_runtime.h"
#include "py68k_string.h"
#include "py68k_value.h"

#include <stddef.h>
#include <string.h>

static Py68Status py68_type_error(Py68Runtime *runtime, const char *message)
{
    Py68Location location;
    location.offset = 0;
    location.line = 0;
    location.column = 0;
    location.length = 0;
    py68_error_set(&runtime->error, PY68_ERROR_TYPE, location, NULL, message);
    return PY68_STATUS_RUNTIME_ERROR;
}

static Py68U16 py68_type_strlen(const char *text)
{
    Py68U16 length = 0;
    while (text[length] != '\0') ++length;
    return length;
}

static Py68Status py68_type_register(Py68Runtime *runtime, const char *name,
                                     Py68Type *type)
{
    Py68Status status;
    status = py68_builtin_set_copy(runtime, (const Py68U8 *)name,
                                   py68_type_strlen(name),
                                   py68_value_from_object(&type->base));
    return status;
}

static Py68Status py68_type_make_named(Py68Runtime *runtime, const char *name,
                                       Py68Type *base_type, Py68U16 flags,
                                       Py68U16 exception_kind,
                                       Py68Type **result)
{
    Py68String *string;
    Py68Type *type;
    Py68Status status;
    status = py68_string_new_copy(runtime, name, (Py68U32)py68_type_strlen(name),
                                  &string);
    if (status != PY68_STATUS_OK) return status;
    status = py68_type_new(runtime, string, base_type, NULL, &type);
    py68_object_release(runtime, &string->base);
    if (status != PY68_STATUS_OK) return status;
    type->flags = flags;
    type->exception_kind = exception_kind;
    *result = type;
    return PY68_STATUS_OK;
}

Py68Status py68_type_new(Py68Runtime *runtime, Py68String *name,
                         Py68Type *base_type, Py68Dict *dict,
                         Py68Type **result)
{
    Py68Type *type;
    Py68Dict *owned_dict = dict;
    int created_dict = 0;
    if (runtime == NULL || name == NULL || result == NULL)
        return PY68_STATUS_INTERNAL_ERROR;
    if (owned_dict == NULL) {
        if (py68_dict_new(runtime, &owned_dict) != PY68_STATUS_OK)
            return PY68_STATUS_MEMORY_ERROR;
        created_dict = 1;
    }
    type = (Py68Type *)py68_alloc(&runtime->allocator, PY68_MEM_RUNTIME,
                                  sizeof(Py68Type));
    if (type == NULL) {
        if (created_dict)
            py68_object_release(runtime, &owned_dict->base);
        return PY68_STATUS_MEMORY_ERROR;
    }
    type->base.type = PY68_OBJECT_TYPE;
    type->base.flags = 0;
    type->base.reference_count = 1;
    type->base.next_object = runtime->live_objects;
    runtime->live_objects = &type->base;
    type->name = name;
    py68_object_retain(&name->base);
    type->base_type = base_type;
    if (base_type != NULL)
        py68_object_retain(&base_type->base);
    type->dict = owned_dict;
    if (!created_dict)
        py68_object_retain(&owned_dict->base);
    type->exception_kind = 0;
    type->flags = 0;
    *result = type;
    return PY68_STATUS_OK;
}

Py68Status py68_instance_new(Py68Runtime *runtime, Py68Type *type,
                             Py68Instance **result)
{
    Py68Instance *instance;
    Py68Dict *dict;
    if (runtime == NULL || type == NULL || result == NULL)
        return PY68_STATUS_INTERNAL_ERROR;
    if (py68_dict_new(runtime, &dict) != PY68_STATUS_OK)
        return PY68_STATUS_MEMORY_ERROR;
    instance = (Py68Instance *)py68_alloc(&runtime->allocator, PY68_MEM_RUNTIME,
                                          sizeof(Py68Instance));
    if (instance == NULL) {
        py68_object_release(runtime, &dict->base);
        return PY68_STATUS_MEMORY_ERROR;
    }
    instance->base.type = PY68_OBJECT_INSTANCE;
    instance->base.flags = 0;
    instance->base.reference_count = 1;
    instance->base.next_object = runtime->live_objects;
    runtime->live_objects = &instance->base;
    instance->type = type;
    py68_object_retain(&type->base);
    instance->dict = dict;
    *result = instance;
    return PY68_STATUS_OK;
}

int py68_type_is_subtype(Py68Type *derived, Py68Type *base)
{
    Py68Type *cursor;
    if (derived == NULL || base == NULL) return 0;
    cursor = derived;
    while (cursor != NULL) {
        if (cursor == base) return 1;
        cursor = cursor->base_type;
    }
    return 0;
}

Py68Status py68_type_lookup(Py68Runtime *runtime, Py68Type *type,
                            const Py68U8 *name, Py68U16 name_length,
                            Py68Value *result, int *found)
{
    Py68Type *cursor;
    Py68String *key;
    Py68Status status;
    if (runtime == NULL || type == NULL || name == NULL || result == NULL ||
        found == NULL)
        return PY68_STATUS_INTERNAL_ERROR;
    *found = 0;
    status = py68_string_new_copy(runtime, (const char *)name,
                                  (Py68U32)name_length, &key);
    if (status != PY68_STATUS_OK) return status;
    cursor = type;
    while (cursor != NULL) {
        if (cursor->dict != NULL) {
            status = py68_dict_get_copy(
                runtime, cursor->dict, py68_value_from_object(&key->base),
                result);
            if (status == PY68_STATUS_OK) {
                *found = 1;
                py68_object_release(runtime, &key->base);
                return PY68_STATUS_OK;
            }
            if (status != PY68_STATUS_SOURCE_ERROR) {
                py68_object_release(runtime, &key->base);
                return status;
            }
        }
        cursor = cursor->base_type;
    }
    py68_object_release(runtime, &key->base);
    return PY68_STATUS_OK;
}

Py68Status py68_build_class(Py68Runtime *runtime, Py68U16 argument_count,
                            Py68Value *arguments, Py68Value *result)
{
    Py68Dict *ns;
    Py68String *name;
    Py68Type *base;
    Py68Type *type;
    Py68Status status;
    if (runtime == NULL || arguments == NULL || result == NULL)
        return PY68_STATUS_INTERNAL_ERROR;
    if (argument_count != 2 && argument_count != 3)
        return py68_type_error(runtime, "__build_class__ expects 2 or 3 arguments");
    if (arguments[0].type != PY68_VALUE_OBJECT || arguments[0].as.object == NULL ||
        arguments[0].as.object->type != PY68_OBJECT_DICT)
        return py68_type_error(runtime, "__build_class__ namespace must be a dict");
    if (arguments[1].type != PY68_VALUE_OBJECT || arguments[1].as.object == NULL ||
        arguments[1].as.object->type != PY68_OBJECT_STRING)
        return py68_type_error(runtime, "__build_class__ name must be a string");
    ns = (Py68Dict *)arguments[0].as.object;
    name = (Py68String *)arguments[1].as.object;
    base = runtime->object_type;
    if (argument_count == 3) {
        if (arguments[2].type != PY68_VALUE_OBJECT ||
            arguments[2].as.object == NULL ||
            arguments[2].as.object->type != PY68_OBJECT_TYPE)
            return py68_type_error(runtime, "base must be a type");
        base = (Py68Type *)arguments[2].as.object;
    }
    if (base == NULL)
        return py68_type_error(runtime, "base type is not installed");
    status = py68_type_new(runtime, name, base, ns, &type);
    if (status != PY68_STATUS_OK) return status;
    if ((base->flags & PY68_TYPE_FLAG_EXCEPTION) != 0) {
        type->flags = (Py68U16)(type->flags | PY68_TYPE_FLAG_EXCEPTION);
        type->exception_kind = base->exception_kind;
    }
    *result = py68_value_from_object(&type->base);
    return PY68_STATUS_OK;
}

Py68Type *py68_type_for_error_kind(Py68Runtime *runtime, Py68U16 kind)
{
    if (runtime == NULL || kind >= 32) return NULL;
    return runtime->exception_by_kind[kind];
}

Py68Type *py68_runtime_object_type(Py68Runtime *runtime)
{
    return runtime != NULL ? runtime->object_type : NULL;
}

Py68Type *py68_runtime_type_type(Py68Runtime *runtime)
{
    return runtime != NULL ? runtime->type_type : NULL;
}

Py68Status py68_type_call_as_exception(Py68Runtime *runtime, Py68Type *type,
                                       Py68U16 argument_count,
                                       Py68Value *arguments, Py68Value *result)
{
    Py68Exception *exception;
    Py68U16 kind;
    const char *message = "";
    Py68Status status;
    if (runtime == NULL || type == NULL || result == NULL)
        return PY68_STATUS_INTERNAL_ERROR;
    if ((type->flags & PY68_TYPE_FLAG_EXCEPTION) == 0)
        return py68_type_error(runtime, "type is not callable");
    if (argument_count > 1)
        return py68_type_error(runtime, "exception takes at most 1 argument");
    kind = type->exception_kind;
    if (kind == 0)
        kind = PY68_ERROR_EXCEPTION;
    if (argument_count == 1) {
        if (arguments[0].type == PY68_VALUE_OBJECT &&
            arguments[0].as.object != NULL &&
            arguments[0].as.object->type == PY68_OBJECT_STRING)
            message = ((Py68String *)arguments[0].as.object)->data;
        else if (arguments[0].type == PY68_VALUE_INT)
            return py68_type_error(runtime, "exception message must be a string");
    }
    status = py68_exception_new_typed(runtime, type, kind, message, &exception);
    if (status != PY68_STATUS_OK) return status;
    *result = py68_value_from_object(&exception->base);
    return PY68_STATUS_OK;
}

static Py68Status py68_builtin_isinstance(Py68Runtime *runtime,
                                          Py68U16 argument_count,
                                          Py68Value *arguments,
                                          Py68Value *result)
{
    Py68Type *type_arg;
    int is_instance = 0;
    (void)argument_count;
    if (arguments[1].type != PY68_VALUE_OBJECT || arguments[1].as.object == NULL ||
        arguments[1].as.object->type != PY68_OBJECT_TYPE)
        return py68_type_error(runtime, "isinstance() arg 2 must be a type");
    type_arg = (Py68Type *)arguments[1].as.object;
    if (arguments[0].type == PY68_VALUE_OBJECT && arguments[0].as.object != NULL) {
        if (arguments[0].as.object->type == PY68_OBJECT_INSTANCE) {
            Py68Instance *instance = (Py68Instance *)arguments[0].as.object;
            is_instance = py68_type_is_subtype(instance->type, type_arg);
        } else if (arguments[0].as.object->type == PY68_OBJECT_EXCEPTION) {
            Py68Exception *exception = (Py68Exception *)arguments[0].as.object;
            Py68Type *exc_type = exception->type;
            if (exc_type == NULL)
                exc_type = py68_type_for_error_kind(runtime, exception->kind);
            if (exc_type != NULL)
                is_instance = py68_type_is_subtype(exc_type, type_arg);
            else if ((type_arg->flags & PY68_TYPE_FLAG_EXCEPTION) != 0 &&
                     type_arg->exception_kind != 0)
                is_instance = exception->kind == type_arg->exception_kind;
        }
    }
    *result = py68_value_bool(is_instance);
    return PY68_STATUS_OK;
}

static Py68Status py68_builtin_issubclass(Py68Runtime *runtime,
                                          Py68U16 argument_count,
                                          Py68Value *arguments,
                                          Py68Value *result)
{
    Py68Type *derived;
    Py68Type *base;
    (void)argument_count;
    if (arguments[0].type != PY68_VALUE_OBJECT || arguments[0].as.object == NULL ||
        arguments[0].as.object->type != PY68_OBJECT_TYPE ||
        arguments[1].type != PY68_VALUE_OBJECT || arguments[1].as.object == NULL ||
        arguments[1].as.object->type != PY68_OBJECT_TYPE)
        return py68_type_error(runtime, "issubclass() args must be types");
    derived = (Py68Type *)arguments[0].as.object;
    base = (Py68Type *)arguments[1].as.object;
    *result = py68_value_bool(py68_type_is_subtype(derived, base));
    return PY68_STATUS_OK;
}

Py68Status py68_type_of_value(Py68Runtime *runtime, Py68Value value,
                              Py68Value *result)
{
    Py68Object *object;
    if (runtime == NULL || result == NULL) return PY68_STATUS_INTERNAL_ERROR;
    if (value.type != PY68_VALUE_OBJECT || value.as.object == NULL)
        return py68_type_error(
            runtime,
            "type() only supports user instances in Language Level 0.8");
    object = value.as.object;
    if (object->type == PY68_OBJECT_INSTANCE) {
        Py68Instance *instance = (Py68Instance *)object;
        *result = py68_value_from_object(&instance->type->base);
        py68_value_retain(*result);
        return PY68_STATUS_OK;
    }
    if (object->type == PY68_OBJECT_TYPE) {
        if (runtime->type_type == NULL)
            return py68_type_error(runtime, "type type is not installed");
        *result = py68_value_from_object(&runtime->type_type->base);
        py68_value_retain(*result);
        return PY68_STATUS_OK;
    }
    if (object->type == PY68_OBJECT_EXCEPTION) {
        Py68Exception *exception = (Py68Exception *)object;
        Py68Type *exc_type = exception->type;
        if (exc_type == NULL)
            exc_type = py68_type_for_error_kind(runtime, exception->kind);
        if (exc_type == NULL)
            return py68_type_error(
                runtime,
                "type() only supports user instances in Language Level 0.8");
        *result = py68_value_from_object(&exc_type->base);
        py68_value_retain(*result);
        return PY68_STATUS_OK;
    }
    return py68_type_error(
        runtime, "type() only supports user instances in Language Level 0.8");
}

static Py68Status py68_install_exception_type(Py68Runtime *runtime,
                                              const char *name, Py68U16 kind,
                                              Py68Type *base, Py68Type **out)
{
    Py68Type *type;
    Py68Status status;
    status = py68_type_make_named(runtime, name, base, PY68_TYPE_FLAG_EXCEPTION,
                                  kind, &type);
    if (status != PY68_STATUS_OK) return status;
    status = py68_type_register(runtime, name, type);
    if (status != PY68_STATUS_OK) {
        py68_object_release(runtime, &type->base);
        return status;
    }
    if (kind < 32)
        runtime->exception_by_kind[kind] = type;
    if (out != NULL)
        *out = type;
    py68_object_release(runtime, &type->base);
    return PY68_STATUS_OK;
}

static Py68Status py68_types_register_native(Py68Runtime *runtime,
                                             const char *name,
                                             Py68U16 minimum_arguments,
                                             Py68U16 maximum_arguments,
                                             Py68NativeCallback callback)
{
    Py68NativeFunction *function;
    Py68Status status;
    status = py68_native_new(runtime, name, minimum_arguments, maximum_arguments,
                             callback, &function);
    if (status != PY68_STATUS_OK) return status;
    status = py68_builtin_set_copy(runtime, (const Py68U8 *)name,
                                   py68_type_strlen(name),
                                   py68_value_from_object(&function->base));
    py68_object_release(runtime, &function->base);
    return status;
}

Py68Status py68_types_install(Py68Runtime *runtime)
{
    Py68Type *object_type;
    Py68Type *type_type;
    Py68Type *base_exception;
    Py68Type *exception_type;
    Py68Type *os_error;
    Py68Status status;
    Py68U16 index;
    static const struct {
        const char *name;
        Py68U16 kind;
    } exceptions[] = {
        { "TypeError", PY68_ERROR_TYPE },
        { "ValueError", PY68_ERROR_VALUE },
        { "IndexError", PY68_ERROR_INDEX },
        { "KeyError", PY68_ERROR_KEY },
        { "ZeroDivisionError", PY68_ERROR_ZERO_DIVISION },
        { "OverflowError", PY68_ERROR_OVERFLOW },
        { "NameError", PY68_ERROR_NAME },
        { "OSError", PY68_ERROR_IO },
        { "RecursionError", PY68_ERROR_RECURSION },
        { "ImportError", PY68_ERROR_IMPORT },
        { "StopIteration", PY68_ERROR_STOP_ITERATION }
    };

    if (runtime == NULL) return PY68_STATUS_INTERNAL_ERROR;
    for (index = 0; index < 32; ++index)
        runtime->exception_by_kind[index] = NULL;

    status = py68_type_make_named(runtime, "object", NULL, 0, 0, &object_type);
    if (status != PY68_STATUS_OK) return status;
    runtime->object_type = object_type;
    status = py68_type_register(runtime, "object", object_type);
    if (status != PY68_STATUS_OK) return status;

    status = py68_type_make_named(runtime, "type", object_type, 0, 0, &type_type);
    if (status != PY68_STATUS_OK) return status;
    runtime->type_type = type_type;
    status = py68_type_register(runtime, "type", type_type);
    if (status != PY68_STATUS_OK) return status;

    status = py68_type_make_named(runtime, "BaseException", NULL,
                                  PY68_TYPE_FLAG_EXCEPTION, 0, &base_exception);
    if (status != PY68_STATUS_OK) return status;
    runtime->base_exception_type = base_exception;
    status = py68_type_register(runtime, "BaseException", base_exception);
    if (status != PY68_STATUS_OK) return status;

    status = py68_type_make_named(runtime, "Exception", base_exception,
                                  PY68_TYPE_FLAG_EXCEPTION, 0, &exception_type);
    if (status != PY68_STATUS_OK) return status;
    runtime->exception_type = exception_type;
    status = py68_type_register(runtime, "Exception", exception_type);
    if (status != PY68_STATUS_OK) return status;
    runtime->exception_by_kind[PY68_ERROR_EXCEPTION] = exception_type;

    for (index = 0; index < (Py68U16)(sizeof(exceptions) / sizeof(exceptions[0]));
         ++index) {
        status = py68_install_exception_type(runtime, exceptions[index].name,
                                             exceptions[index].kind,
                                             exception_type, NULL);
        if (status != PY68_STATUS_OK) return status;
    }

    os_error = runtime->exception_by_kind[PY68_ERROR_IO];
    if (os_error == NULL) return PY68_STATUS_INTERNAL_ERROR;
    status = py68_type_register(runtime, "IOError", os_error);
    if (status != PY68_STATUS_OK) return status;

    status = py68_types_register_native(runtime, "isinstance", 2, 2,
                                        py68_builtin_isinstance);
    if (status != PY68_STATUS_OK) return status;
    status = py68_types_register_native(runtime, "issubclass", 2, 2,
                                        py68_builtin_issubclass);
    if (status != PY68_STATUS_OK) return status;
    /* Builtin "type" is the type object; type(obj) is handled in OP_CALL. */
    status = py68_types_register_native(runtime, "__build_class__", 2, 3,
                                        py68_build_class);
    if (status != PY68_STATUS_OK) return status;

    /* Drop the install-local retains; builtins (and base links) hold them. */
    py68_object_release(runtime, &object_type->base);
    py68_object_release(runtime, &type_type->base);
    py68_object_release(runtime, &base_exception->base);
    py68_object_release(runtime, &exception_type->base);
    return PY68_STATUS_OK;
}
