/* 2026 by Piotr Rozentreter (Rozsoft) */

#include "py68k_attr.h"
#include "py68k_dict.h"
#include "py68k_exception.h"
#include "py68k_file.h"
#include "py68k_function.h"
#include "py68k_list.h"
#include "py68k_module.h"
#include "py68k_native.h"
#include "py68k_runtime.h"
#include "py68k_set.h"
#include "py68k_string.h"
#include "py68k_string_methods.h"
#include "py68k_time.h"
#include "py68k_type.h"

#include <stddef.h>
#include <string.h>

Py68Status py68_bound_method_new_callable(Py68Runtime *runtime, Py68Value self,
                                          Py68Object *callable,
                                          Py68BoundMethod **result)
{
    Py68BoundMethod *method;
    if (callable == NULL) return PY68_STATUS_INTERNAL_ERROR;
    method = (Py68BoundMethod *)py68_alloc(
        &runtime->allocator, PY68_MEM_FUNCTION, sizeof(Py68BoundMethod));
    if (method == NULL) return PY68_STATUS_MEMORY_ERROR;
    method->base.type = PY68_OBJECT_BOUND_METHOD;
    method->base.flags = 0;
    method->base.reference_count = 1;
    method->base.next_object = runtime->live_objects;
    runtime->live_objects = &method->base;
    method->self = self;
    py68_value_retain(self);
    method->callable = callable;
    py68_object_retain(callable);
    *result = method;
    return PY68_STATUS_OK;
}

Py68Status py68_bound_method_new(Py68Runtime *runtime, Py68Value self,
                                 Py68NativeFunction *function,
                                 Py68BoundMethod **result)
{
    if (function == NULL) return PY68_STATUS_INTERNAL_ERROR;
    return py68_bound_method_new_callable(runtime, self, &function->base,
                                          result);
}

static Py68Status py68_attr_error(Py68Runtime *runtime, Py68ErrorKind kind,
                                  const char *message)
{
    Py68Location location;
    location.offset = 0;
    location.line = 0;
    location.column = 0;
    location.length = 0;
    py68_error_set(&runtime->error, kind, location, NULL, message);
    return PY68_STATUS_RUNTIME_ERROR;
}

static Py68Status py68_attr_list_append(Py68Runtime *runtime,
                                        Py68U16 argument_count,
                                        Py68Value *arguments,
                                        Py68Value *result)
{
    if (argument_count != 2 || arguments[0].type != PY68_VALUE_OBJECT ||
        arguments[0].as.object == NULL ||
        arguments[0].as.object->type != PY68_OBJECT_LIST)
        return py68_attr_error(runtime, PY68_ERROR_TYPE,
                               "append expects a list");
    if (py68_list_append_copy(runtime, (Py68List *)arguments[0].as.object,
                              arguments[1]) != PY68_STATUS_OK)
        return py68_attr_error(runtime, PY68_ERROR_VALUE,
                               "cyclic containers are not supported");
    *result = py68_value_none();
    return PY68_STATUS_OK;
}

static Py68Status py68_attr_list_pop(Py68Runtime *runtime,
                                     Py68U16 argument_count,
                                     Py68Value *arguments, Py68Value *result)
{
    Py68List *list;
    if (argument_count != 1 || arguments[0].type != PY68_VALUE_OBJECT ||
        arguments[0].as.object == NULL ||
        arguments[0].as.object->type != PY68_OBJECT_LIST)
        return py68_attr_error(runtime, PY68_ERROR_TYPE, "pop expects a list");
    list = (Py68List *)arguments[0].as.object;
    if (list->count == 0)
        return py68_attr_error(runtime, PY68_ERROR_INDEX, "pop from empty list");
    *result = list->items[list->count - 1];
    --list->count;
    return PY68_STATUS_OK;
}

static Py68Status py68_attr_dict_get(Py68Runtime *runtime,
                                     Py68U16 argument_count,
                                     Py68Value *arguments, Py68Value *result)
{
    Py68Status status;
    if (argument_count < 2 || argument_count > 3 ||
        arguments[0].type != PY68_VALUE_OBJECT ||
        arguments[0].as.object == NULL ||
        arguments[0].as.object->type != PY68_OBJECT_DICT)
        return py68_attr_error(runtime, PY68_ERROR_TYPE, "get expects a dict");
    status = py68_dict_get_copy(runtime, (Py68Dict *)arguments[0].as.object,
                                arguments[1], result);
    if (status == PY68_STATUS_OK) return PY68_STATUS_OK;
    if (argument_count == 3) {
        *result = arguments[2];
        py68_value_retain(*result);
        return PY68_STATUS_OK;
    }
    *result = py68_value_none();
    return PY68_STATUS_OK;
}

static Py68Status py68_attr_dict_pop(Py68Runtime *runtime,
                                     Py68U16 argument_count,
                                     Py68Value *arguments, Py68Value *result)
{
    if (argument_count != 2 || arguments[0].type != PY68_VALUE_OBJECT ||
        arguments[0].as.object == NULL ||
        arguments[0].as.object->type != PY68_OBJECT_DICT)
        return py68_attr_error(runtime, PY68_ERROR_TYPE, "pop expects a dict");
    if (py68_dict_pop(runtime, (Py68Dict *)arguments[0].as.object,
                      arguments[1], result) != PY68_STATUS_OK)
        return py68_attr_error(runtime, PY68_ERROR_KEY, "key not found");
    return PY68_STATUS_OK;
}

static Py68Status py68_attr_dict_keys(Py68Runtime *runtime,
                                      Py68U16 argument_count,
                                      Py68Value *arguments, Py68Value *result)
{
    Py68List *list;
    if (argument_count != 1 || arguments[0].as.object->type != PY68_OBJECT_DICT)
        return py68_attr_error(runtime, PY68_ERROR_TYPE, "keys expects a dict");
    if (py68_dict_keys(runtime, (Py68Dict *)arguments[0].as.object, &list) !=
        PY68_STATUS_OK)
        return PY68_STATUS_MEMORY_ERROR;
    *result = py68_value_from_object(&list->base);
    return PY68_STATUS_OK;
}

static Py68Status py68_attr_dict_values(Py68Runtime *runtime,
                                        Py68U16 argument_count,
                                        Py68Value *arguments,
                                        Py68Value *result)
{
    Py68List *list;
    if (argument_count != 1 || arguments[0].as.object->type != PY68_OBJECT_DICT)
        return py68_attr_error(runtime, PY68_ERROR_TYPE,
                               "values expects a dict");
    if (py68_dict_values(runtime, (Py68Dict *)arguments[0].as.object, &list) !=
        PY68_STATUS_OK)
        return PY68_STATUS_MEMORY_ERROR;
    *result = py68_value_from_object(&list->base);
    return PY68_STATUS_OK;
}

static Py68Status py68_attr_dict_items(Py68Runtime *runtime,
                                       Py68U16 argument_count,
                                       Py68Value *arguments, Py68Value *result)
{
    Py68List *list;
    if (argument_count != 1 || arguments[0].as.object->type != PY68_OBJECT_DICT)
        return py68_attr_error(runtime, PY68_ERROR_TYPE,
                               "items expects a dict");
    if (py68_dict_items(runtime, (Py68Dict *)arguments[0].as.object, &list) !=
        PY68_STATUS_OK)
        return PY68_STATUS_MEMORY_ERROR;
    *result = py68_value_from_object(&list->base);
    return PY68_STATUS_OK;
}

static Py68Status py68_attr_set_add(Py68Runtime *runtime,
                                    Py68U16 argument_count,
                                    Py68Value *arguments, Py68Value *result)
{
    if (argument_count != 2 || arguments[0].as.object->type != PY68_OBJECT_SET)
        return py68_attr_error(runtime, PY68_ERROR_TYPE, "add expects a set");
    if (py68_set_add(runtime, (Py68Set *)arguments[0].as.object,
                     arguments[1]) != PY68_STATUS_OK)
        return py68_attr_error(runtime, PY68_ERROR_TYPE, "unhashable type");
    *result = py68_value_none();
    return PY68_STATUS_OK;
}

static Py68Status py68_attr_set_remove(Py68Runtime *runtime,
                                       Py68U16 argument_count,
                                       Py68Value *arguments, Py68Value *result)
{
    if (argument_count != 2 || arguments[0].as.object->type != PY68_OBJECT_SET)
        return py68_attr_error(runtime, PY68_ERROR_TYPE,
                               "remove expects a set");
    if (py68_set_remove(runtime, (Py68Set *)arguments[0].as.object,
                        arguments[1]) != PY68_STATUS_OK)
        return py68_attr_error(runtime, PY68_ERROR_KEY, "set remove missing");
    *result = py68_value_none();
    return PY68_STATUS_OK;
}

static Py68Status py68_attr_set_discard(Py68Runtime *runtime,
                                        Py68U16 argument_count,
                                        Py68Value *arguments,
                                        Py68Value *result)
{
    if (argument_count != 2 || arguments[0].as.object->type != PY68_OBJECT_SET)
        return py68_attr_error(runtime, PY68_ERROR_TYPE,
                               "discard expects a set");
    if (py68_set_discard(runtime, (Py68Set *)arguments[0].as.object,
                         arguments[1]) != PY68_STATUS_OK)
        return py68_attr_error(runtime, PY68_ERROR_TYPE, "unhashable type");
    *result = py68_value_none();
    return PY68_STATUS_OK;
}

static Py68Status py68_attr_file_enter(Py68Runtime *runtime,
                                       Py68U16 argument_count,
                                       Py68Value *arguments, Py68Value *result)
{
    if (argument_count != 1 || arguments[0].as.object->type != PY68_OBJECT_FILE)
        return py68_attr_error(runtime, PY68_ERROR_TYPE,
                               "__enter__ expects a file");
    *result = arguments[0];
    py68_value_retain(*result);
    return PY68_STATUS_OK;
}

static Py68Status py68_attr_file_exit(Py68Runtime *runtime,
                                      Py68U16 argument_count,
                                      Py68Value *arguments, Py68Value *result)
{
    if (argument_count < 1 || arguments[0].as.object->type != PY68_OBJECT_FILE)
        return py68_attr_error(runtime, PY68_ERROR_TYPE,
                               "__exit__ expects a file");
    py68_file_close(runtime, (Py68File *)arguments[0].as.object);
    *result = py68_value_none();
    return PY68_STATUS_OK;
}

static Py68Status py68_attr_bind(Py68Runtime *runtime, Py68Value self,
                                 const char *name, Py68U16 min_args,
                                 Py68U16 max_args, Py68NativeCallback callback,
                                 Py68Value *result)
{
    Py68NativeFunction *function;
    Py68BoundMethod *method;
    Py68Status status = py68_native_new(runtime, name, min_args, max_args,
                                        callback, &function);
    if (status != PY68_STATUS_OK) return status;
    status = py68_bound_method_new(runtime, self, function, &method);
    py68_object_release(runtime, &function->base);
    if (status != PY68_STATUS_OK) return status;
    *result = py68_value_from_object(&method->base);
    return PY68_STATUS_OK;
}

static int py68_attr_name_is(const Py68U8 *name, Py68U16 length,
                             const char *expected)
{
    Py68U16 index = 0;
    while (expected[index] != '\0') ++index;
    return length == index && memcmp(name, expected, length) == 0;
}

Py68Status py68_attr_load(Py68Runtime *runtime, Py68Value object,
                          const Py68U8 *name, Py68U16 name_length,
                          Py68Value *result)
{
    Py68U16 type;
    if (object.type != PY68_VALUE_OBJECT || object.as.object == NULL)
        return py68_attr_error(runtime, PY68_ERROR_TYPE,
                               "attribute access requires an object");
    type = object.as.object->type;
    if (type == PY68_OBJECT_MODULE)
        return py68_module_get(runtime, (Py68Module *)object.as.object, name,
                               name_length, result) == PY68_STATUS_OK
               ? PY68_STATUS_OK
               : py68_attr_error(runtime, PY68_ERROR_NAME,
                                 "module has no such attribute");
    if (type == PY68_OBJECT_STRING) {
        if (py68_attr_name_is(name, name_length, "upper"))
            return py68_attr_bind(runtime, object, "upper", 1, 1,
                                  py68_attr_string_upper, result);
        if (py68_attr_name_is(name, name_length, "lower"))
            return py68_attr_bind(runtime, object, "lower", 1, 1,
                                  py68_attr_string_lower, result);
        if (py68_attr_name_is(name, name_length, "casefold"))
            return py68_attr_bind(runtime, object, "casefold", 1, 1,
                                  py68_attr_string_casefold, result);
        if (py68_attr_name_is(name, name_length, "capitalize"))
            return py68_attr_bind(runtime, object, "capitalize", 1, 1,
                                  py68_attr_string_capitalize, result);
        if (py68_attr_name_is(name, name_length, "swapcase"))
            return py68_attr_bind(runtime, object, "swapcase", 1, 1,
                                  py68_attr_string_swapcase, result);
        if (py68_attr_name_is(name, name_length, "title"))
            return py68_attr_bind(runtime, object, "title", 1, 1,
                                  py68_attr_string_title, result);
        if (py68_attr_name_is(name, name_length, "find"))
            return py68_attr_bind(runtime, object, "find", 2, 4,
                                  py68_attr_string_find, result);
        if (py68_attr_name_is(name, name_length, "rfind"))
            return py68_attr_bind(runtime, object, "rfind", 2, 4,
                                  py68_attr_string_rfind, result);
        if (py68_attr_name_is(name, name_length, "index"))
            return py68_attr_bind(runtime, object, "index", 2, 4,
                                  py68_attr_string_index, result);
        if (py68_attr_name_is(name, name_length, "rindex"))
            return py68_attr_bind(runtime, object, "rindex", 2, 4,
                                  py68_attr_string_rindex, result);
        if (py68_attr_name_is(name, name_length, "count"))
            return py68_attr_bind(runtime, object, "count", 2, 4,
                                  py68_attr_string_count, result);
        if (py68_attr_name_is(name, name_length, "startswith"))
            return py68_attr_bind(runtime, object, "startswith", 2, 4,
                                  py68_attr_string_startswith, result);
        if (py68_attr_name_is(name, name_length, "endswith"))
            return py68_attr_bind(runtime, object, "endswith", 2, 4,
                                  py68_attr_string_endswith, result);
        if (py68_attr_name_is(name, name_length, "strip"))
            return py68_attr_bind(runtime, object, "strip", 1, 2,
                                  py68_attr_string_strip, result);
        if (py68_attr_name_is(name, name_length, "lstrip"))
            return py68_attr_bind(runtime, object, "lstrip", 1, 2,
                                  py68_attr_string_lstrip, result);
        if (py68_attr_name_is(name, name_length, "rstrip"))
            return py68_attr_bind(runtime, object, "rstrip", 1, 2,
                                  py68_attr_string_rstrip, result);
        if (py68_attr_name_is(name, name_length, "removeprefix"))
            return py68_attr_bind(runtime, object, "removeprefix", 2, 2,
                                  py68_attr_string_removeprefix, result);
        if (py68_attr_name_is(name, name_length, "removesuffix"))
            return py68_attr_bind(runtime, object, "removesuffix", 2, 2,
                                  py68_attr_string_removesuffix, result);
        if (py68_attr_name_is(name, name_length, "split"))
            return py68_attr_bind(runtime, object, "split", 1, 3,
                                  py68_attr_string_split, result);
        if (py68_attr_name_is(name, name_length, "rsplit"))
            return py68_attr_bind(runtime, object, "rsplit", 1, 3,
                                  py68_attr_string_rsplit, result);
        if (py68_attr_name_is(name, name_length, "splitlines"))
            return py68_attr_bind(runtime, object, "splitlines", 1, 2,
                                  py68_attr_string_splitlines, result);
        if (py68_attr_name_is(name, name_length, "partition"))
            return py68_attr_bind(runtime, object, "partition", 2, 2,
                                  py68_attr_string_partition, result);
        if (py68_attr_name_is(name, name_length, "rpartition"))
            return py68_attr_bind(runtime, object, "rpartition", 2, 2,
                                  py68_attr_string_rpartition, result);
        if (py68_attr_name_is(name, name_length, "join"))
            return py68_attr_bind(runtime, object, "join", 2, 2,
                                  py68_attr_string_join, result);
        if (py68_attr_name_is(name, name_length, "replace"))
            return py68_attr_bind(runtime, object, "replace", 3, 4,
                                  py68_attr_string_replace, result);
        if (py68_attr_name_is(name, name_length, "center"))
            return py68_attr_bind(runtime, object, "center", 2, 3,
                                  py68_attr_string_center, result);
        if (py68_attr_name_is(name, name_length, "ljust"))
            return py68_attr_bind(runtime, object, "ljust", 2, 3,
                                  py68_attr_string_ljust, result);
        if (py68_attr_name_is(name, name_length, "rjust"))
            return py68_attr_bind(runtime, object, "rjust", 2, 3,
                                  py68_attr_string_rjust, result);
        if (py68_attr_name_is(name, name_length, "zfill"))
            return py68_attr_bind(runtime, object, "zfill", 2, 2,
                                  py68_attr_string_zfill, result);
        if (py68_attr_name_is(name, name_length, "expandtabs"))
            return py68_attr_bind(runtime, object, "expandtabs", 1, 2,
                                  py68_attr_string_expandtabs, result);
        if (py68_attr_name_is(name, name_length, "translate"))
            return py68_attr_bind(runtime, object, "translate", 2, 2,
                                  py68_attr_string_translate, result);
        if (py68_attr_name_is(name, name_length, "isalnum"))
            return py68_attr_bind(runtime, object, "isalnum", 1, 1,
                                  py68_attr_string_isalnum, result);
        if (py68_attr_name_is(name, name_length, "isalpha"))
            return py68_attr_bind(runtime, object, "isalpha", 1, 1,
                                  py68_attr_string_isalpha, result);
        if (py68_attr_name_is(name, name_length, "isascii"))
            return py68_attr_bind(runtime, object, "isascii", 1, 1,
                                  py68_attr_string_isascii, result);
        if (py68_attr_name_is(name, name_length, "isdecimal"))
            return py68_attr_bind(runtime, object, "isdecimal", 1, 1,
                                  py68_attr_string_isdecimal, result);
        if (py68_attr_name_is(name, name_length, "isdigit"))
            return py68_attr_bind(runtime, object, "isdigit", 1, 1,
                                  py68_attr_string_isdigit, result);
        if (py68_attr_name_is(name, name_length, "isidentifier"))
            return py68_attr_bind(runtime, object, "isidentifier", 1, 1,
                                  py68_attr_string_isidentifier, result);
        if (py68_attr_name_is(name, name_length, "islower"))
            return py68_attr_bind(runtime, object, "islower", 1, 1,
                                  py68_attr_string_islower, result);
        if (py68_attr_name_is(name, name_length, "isnumeric"))
            return py68_attr_bind(runtime, object, "isnumeric", 1, 1,
                                  py68_attr_string_isnumeric, result);
        if (py68_attr_name_is(name, name_length, "isprintable"))
            return py68_attr_bind(runtime, object, "isprintable", 1, 1,
                                  py68_attr_string_isprintable, result);
        if (py68_attr_name_is(name, name_length, "isspace"))
            return py68_attr_bind(runtime, object, "isspace", 1, 1,
                                  py68_attr_string_isspace, result);
        if (py68_attr_name_is(name, name_length, "istitle"))
            return py68_attr_bind(runtime, object, "istitle", 1, 1,
                                  py68_attr_string_istitle, result);
        if (py68_attr_name_is(name, name_length, "isupper"))
            return py68_attr_bind(runtime, object, "isupper", 1, 1,
                                  py68_attr_string_isupper, result);
    } else if (type == PY68_OBJECT_LIST) {
        if (py68_attr_name_is(name, name_length, "append"))
            return py68_attr_bind(runtime, object, "append", 2, 2,
                                  py68_attr_list_append, result);
        if (py68_attr_name_is(name, name_length, "pop"))
            return py68_attr_bind(runtime, object, "pop", 1, 1,
                                  py68_attr_list_pop, result);
    } else if (type == PY68_OBJECT_DICT) {
        if (py68_attr_name_is(name, name_length, "get"))
            return py68_attr_bind(runtime, object, "get", 2, 3,
                                  py68_attr_dict_get, result);
        if (py68_attr_name_is(name, name_length, "pop"))
            return py68_attr_bind(runtime, object, "pop", 2, 2,
                                  py68_attr_dict_pop, result);
        if (py68_attr_name_is(name, name_length, "keys"))
            return py68_attr_bind(runtime, object, "keys", 1, 1,
                                  py68_attr_dict_keys, result);
        if (py68_attr_name_is(name, name_length, "values"))
            return py68_attr_bind(runtime, object, "values", 1, 1,
                                  py68_attr_dict_values, result);
        if (py68_attr_name_is(name, name_length, "items"))
            return py68_attr_bind(runtime, object, "items", 1, 1,
                                  py68_attr_dict_items, result);
    } else if (type == PY68_OBJECT_SET) {
        if (py68_attr_name_is(name, name_length, "add"))
            return py68_attr_bind(runtime, object, "add", 2, 2,
                                  py68_attr_set_add, result);
        if (py68_attr_name_is(name, name_length, "remove"))
            return py68_attr_bind(runtime, object, "remove", 2, 2,
                                  py68_attr_set_remove, result);
        if (py68_attr_name_is(name, name_length, "discard"))
            return py68_attr_bind(runtime, object, "discard", 2, 2,
                                  py68_attr_set_discard, result);
    } else if (type == PY68_OBJECT_FILE) {
        if (py68_attr_name_is(name, name_length, "__enter__"))
            return py68_attr_bind(runtime, object, "__enter__", 1, 1,
                                  py68_attr_file_enter, result);
        if (py68_attr_name_is(name, name_length, "__exit__"))
            return py68_attr_bind(runtime, object, "__exit__", 1, 4,
                                  py68_attr_file_exit, result);
    } else if (type == PY68_OBJECT_STRUCT_TIME) {
        static const char *names[9] = { "tm_year", "tm_mon", "tm_mday",
            "tm_hour", "tm_min", "tm_sec", "tm_wday", "tm_yday",
            "tm_isdst" };
        Py68U16 index;
        for (index = 0; index < 9; ++index) {
            if (py68_attr_name_is(name, name_length, names[index])) {
                *result = py68_value_int(
                    ((Py68StructTime *)object.as.object)->fields[index]);
                return PY68_STATUS_OK;
            }
        }
    } else if (type == PY68_OBJECT_INSTANCE) {
        Py68Instance *instance = (Py68Instance *)object.as.object;
        Py68String *key;
        Py68Value found_value;
        int found = 0;
        Py68Status status;
        status = py68_string_new_copy(runtime, (const char *)name,
                                      (Py68U32)name_length, &key);
        if (status != PY68_STATUS_OK) return status;
        if (instance->dict != NULL) {
            status = py68_dict_get_copy(
                runtime, instance->dict, py68_value_from_object(&key->base),
                &found_value);
            if (status == PY68_STATUS_OK) {
                found = 1;
                *result = found_value;
            } else if (status != PY68_STATUS_SOURCE_ERROR) {
                py68_object_release(runtime, &key->base);
                return status;
            }
        }
        py68_object_release(runtime, &key->base);
        if (found) return PY68_STATUS_OK;
        status = py68_type_lookup(runtime, instance->type, name, name_length,
                                  &found_value, &found);
        if (status != PY68_STATUS_OK) return status;
        if (!found)
            return py68_attr_error(runtime, PY68_ERROR_TYPE,
                                   "unknown attribute");
        if (found_value.type == PY68_VALUE_OBJECT &&
            found_value.as.object != NULL &&
            found_value.as.object->type == PY68_OBJECT_FUNCTION) {
            Py68BoundMethod *method;
            status = py68_bound_method_new_callable(
                runtime, object, found_value.as.object, &method);
            py68_value_release(runtime, found_value);
            if (status != PY68_STATUS_OK) return status;
            *result = py68_value_from_object(&method->base);
            return PY68_STATUS_OK;
        }
        *result = found_value;
        return PY68_STATUS_OK;
    } else if (type == PY68_OBJECT_TYPE) {
        Py68Type *type_obj = (Py68Type *)object.as.object;
        Py68Value found_value;
        int found = 0;
        Py68Status status = py68_type_lookup(runtime, type_obj, name,
                                             name_length, &found_value, &found);
        if (status != PY68_STATUS_OK) return status;
        if (!found)
            return py68_attr_error(runtime, PY68_ERROR_TYPE,
                                   "unknown attribute");
        *result = found_value;
        return PY68_STATUS_OK;
    } else if (type == PY68_OBJECT_EXCEPTION) {
        Py68Exception *exception = (Py68Exception *)object.as.object;
        if (py68_attr_name_is(name, name_length, "message")) {
            Py68String *message;
            Py68Status status = py68_string_new_copy(
                runtime, exception->message,
                (Py68U32)strlen(exception->message), &message);
            if (status != PY68_STATUS_OK) return status;
            *result = py68_value_from_object(&message->base);
            return PY68_STATUS_OK;
        }
    }
    return py68_attr_error(runtime, PY68_ERROR_TYPE, "unknown attribute");
}

Py68Status py68_attr_store(Py68Runtime *runtime, Py68Value object,
                           const Py68U8 *name, Py68U16 name_length,
                           Py68Value value)
{
    if (object.type != PY68_VALUE_OBJECT || object.as.object == NULL)
        return py68_attr_error(runtime, PY68_ERROR_TYPE,
                               "attribute assignment requires an object");
    if (object.as.object->type == PY68_OBJECT_MODULE)
        return py68_module_set(runtime, (Py68Module *)object.as.object, name,
                               name_length, value);
    if (object.as.object->type == PY68_OBJECT_INSTANCE ||
        object.as.object->type == PY68_OBJECT_TYPE) {
        Py68Dict *dict;
        Py68String *key;
        Py68Status status;
        if (object.as.object->type == PY68_OBJECT_INSTANCE)
            dict = ((Py68Instance *)object.as.object)->dict;
        else
            dict = ((Py68Type *)object.as.object)->dict;
        if (dict == NULL)
            return py68_attr_error(runtime, PY68_ERROR_TYPE,
                                   "object has no attribute dictionary");
        status = py68_string_new_copy(runtime, (const char *)name,
                                      (Py68U32)name_length, &key);
        if (status != PY68_STATUS_OK) return status;
        status = py68_dict_set_copy(runtime, dict,
                                    py68_value_from_object(&key->base), value);
        py68_object_release(runtime, &key->base);
        return status;
    }
    return py68_attr_error(runtime, PY68_ERROR_TYPE,
                           "attribute assignment requires a module or instance");
}
