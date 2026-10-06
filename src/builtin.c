/* 2026 by Piotr Rozentreter (Rozsoft) */

#include "py68k_builtin.h"
#include "py68k_exception.h"
#include "py68k_native.h"
#include "py68k_string_methods.h"
#include "py68k_type.h"
#if defined(PY68K_AMIGA)
#include "py68k_ext_load.h"
#endif

#include <stddef.h>
#include <string.h>

static int py68_builtin_name_equal(const Py68GlobalEntry *entry,
                                   const Py68U8 *name, Py68U16 name_length)
{
    return entry->name_length == name_length &&
           memcmp(entry->name, name, name_length) == 0;
}

static Py68Status py68_builtin_grow(Py68Runtime *runtime)
{
    Py68U16 capacity = runtime->builtin_capacity == 0 ? 8 :
                       (Py68U16)(runtime->builtin_capacity * 2);
    Py68GlobalEntry *entries;
    if (capacity < runtime->builtin_capacity) return PY68_STATUS_MEMORY_ERROR;
    entries = (Py68GlobalEntry *)py68_realloc(
        &runtime->allocator, PY68_MEM_RUNTIME, runtime->builtins,
        (Py68U32)runtime->builtin_capacity * sizeof(Py68GlobalEntry),
        (Py68U32)capacity * sizeof(Py68GlobalEntry));
    if (entries == NULL) return PY68_STATUS_MEMORY_ERROR;
    runtime->builtins = entries;
    runtime->builtin_capacity = capacity;
    return PY68_STATUS_OK;
}

Py68Status py68_builtin_set_copy(Py68Runtime *runtime, const Py68U8 *name,
                                 Py68U16 name_length, Py68Value value)
{
    Py68U16 index;
    if (runtime == NULL || name == NULL) return PY68_STATUS_INTERNAL_ERROR;
    for (index = 0; index < runtime->builtin_count; ++index) {
        if (py68_builtin_name_equal(&runtime->builtins[index], name,
                                    name_length)) {
            Py68Value old = runtime->builtins[index].value;
            py68_value_retain(value);
            runtime->builtins[index].value = value;
            py68_value_release(runtime, old);
            return PY68_STATUS_OK;
        }
    }
    if (runtime->builtin_count == runtime->builtin_capacity &&
        py68_builtin_grow(runtime) != PY68_STATUS_OK)
        return PY68_STATUS_MEMORY_ERROR;
    runtime->builtins[runtime->builtin_count].name = name;
    runtime->builtins[runtime->builtin_count].name_length = name_length;
    runtime->builtins[runtime->builtin_count].value = value;
    runtime->builtins[runtime->builtin_count].occupied = 1;
    py68_value_retain(value);
    ++runtime->builtin_count;
    return PY68_STATUS_OK;
}

Py68Status py68_builtin_get_copy(Py68Runtime *runtime, const Py68U8 *name,
                                 Py68U16 name_length, Py68Value *result)
{
    Py68U16 index;
    if (runtime == NULL || name == NULL || result == NULL)
        return PY68_STATUS_INTERNAL_ERROR;
    for (index = 0; index < runtime->builtin_count; ++index) {
        if (py68_builtin_name_equal(&runtime->builtins[index], name,
                                    name_length)) {
            *result = runtime->builtins[index].value;
            py68_value_retain(*result);
            return PY68_STATUS_OK;
        }
    }
    return PY68_STATUS_SOURCE_ERROR;
}

void py68_builtin_clear(Py68Runtime *runtime)
{
    Py68U16 index;
    for (index = 0; index < runtime->builtin_count; ++index)
        py68_value_release(runtime, runtime->builtins[index].value);
    py68_free(&runtime->allocator, PY68_MEM_RUNTIME, runtime->builtins,
              (Py68U32)runtime->builtin_capacity * sizeof(Py68GlobalEntry));
    runtime->builtins = NULL;
    runtime->builtin_count = 0;
    runtime->builtin_capacity = 0;
}

typedef struct Py68BuiltinDefinition {
    const char *name;
    Py68U16 minimum_arguments;
    Py68U16 maximum_arguments;
    Py68NativeCallback callback;
} Py68BuiltinDefinition;

static Py68U16 py68_static_strlen(const char *text)
{
    Py68U16 length = 0;
    while (text[length] != '\0') ++length;
    return length;
}

Py68Status py68_builtins_install(Py68Runtime *runtime)
{
    static const Py68BuiltinDefinition common[] = {
        { "print", 0, 65535, py68_builtin_print },
        { "input", 0, 1, py68_builtin_input },
        { "len", 1, 1, py68_builtin_len },
        { "range", 1, 3, py68_builtin_range },
        { "list_pop", 1, 1, py68_builtin_list_pop },
        { "list_append", 2, 2, py68_builtin_list_append },
        { "list", 0, 1, py68_builtin_list },
        { "tuple", 0, 1, py68_builtin_tuple },
        { "dict", 0, 1, py68_builtin_dict },
        { "set", 0, 1, py68_builtin_set },
        { "int", 1, 1, py68_builtin_int },
        { "float", 1, 1, py68_builtin_float },
        { "str", 1, 1, py68_builtin_str },
        { "bool", 1, 1, py68_builtin_bool },
        { "abs", 1, 1, py68_builtin_abs },
        { "min", 2, 2, py68_builtin_min },
        { "max", 2, 2, py68_builtin_max },
        { "sum", 1, 2, py68_builtin_sum },
        { "iter", 1, 1, py68_builtin_iter },
        { "next", 1, 2, py68_builtin_next },
        { "sorted", 1, 1, py68_builtin_sorted },
        { "ord", 1, 1, py68_builtin_ord },
        { "chr", 1, 1, py68_builtin_chr },
        { "repr", 1, 1, py68_builtin_repr },
        { "ascii", 1, 1, py68_builtin_ascii },
        { "all", 1, 1, py68_builtin_all },
        { "any", 1, 1, py68_builtin_any },
        { "format", 1, 2, py68_builtin_format },
        { "maketrans", 1, 3, py68_builtin_maketrans },
        { "exit", 0, 1, py68_builtin_exit },
        { "fopen", 2, 2, py68_builtin_fopen },
        { "fclose", 1, 1, py68_builtin_fclose },
        { "fread", 2, 2, py68_builtin_fread },
        { "freadline", 1, 1, py68_builtin_freadline },
        { "fwrite", 2, 2, py68_builtin_fwrite },
        { "exists", 1, 1, py68_builtin_exists },
        { "remove", 1, 1, py68_builtin_remove },
        { "rename", 2, 2, py68_builtin_rename }
        ,{ "time", 0, 0, py68_builtin_time }
        ,{ "sleep", 1, 1, py68_builtin_sleep }
        ,{ "ctime", 0, 1, py68_builtin_ctime }
        ,{ "localtime", 0, 1, py68_builtin_localtime }
        ,{ "strftime", 1, 2, py68_builtin_strftime }
        ,{ "perf_counter", 0, 0, py68_builtin_perf_counter }
        ,{ "time_tick", 0, 0, py68_builtin_time_tick }
        ,{ "check_break", 0, 0, py68_builtin_check_break }
        ,{ "yield_cpu", 0, 0, py68_builtin_yield_cpu }
        ,{ "set_poll_interval", 1, 1, py68_builtin_set_poll_interval }
        ,{ "get_poll_interval", 0, 0, py68_builtin_get_poll_interval }
    };
#if defined(PY68K_AMIGA)
    static const Py68BuiltinDefinition platform_vars[] = {
        { "assign_get", 1, 1, py68_builtin_assign_get },
        { "assign_add", 2, 2, py68_builtin_assign_add },
        { "assign_remove", 1, 1, py68_builtin_assign_remove },
        { "load_library", 1, 1, py68_builtin_load_library }
    };
#else
    /* Host keeps getenv_* and also assign_* aliases so Amiga-favor
       examples/test_features.py runs under make language-test. */
    static const Py68BuiltinDefinition platform_vars[] = {
        { "getenv", 1, 1, py68_builtin_getenv },
        { "setenv", 2, 2, py68_builtin_setenv },
        { "unsetenv", 1, 1, py68_builtin_unsetenv },
        { "assign_get", 1, 1, py68_builtin_assign_get },
        { "assign_add", 2, 2, py68_builtin_assign_add },
        { "assign_remove", 1, 1, py68_builtin_assign_remove }
    };
#endif
    /* Builtins that walk their first argument. A generator there is drained
       into a list by OP_CALL before the callback runs (D-0045). */
    static const Py68NativeCallback iterable_consumers[] = {
        py68_builtin_list, py68_builtin_tuple, py68_builtin_set,
        py68_builtin_sorted, py68_builtin_sum, py68_builtin_all,
        py68_builtin_any
    };
    Py68U16 index;
    Py68U16 consumer;
    Py68Status status;
    Py68NativeFunction *function;

    for (index = 0; index < sizeof(common) / sizeof(common[0]); ++index) {
        status = py68_native_new(runtime, common[index].name,
                                 common[index].minimum_arguments,
                                 common[index].maximum_arguments,
                                 common[index].callback, &function);
        if (status != PY68_STATUS_OK) return status;
        for (consumer = 0;
             consumer < sizeof(iterable_consumers) /
                        sizeof(iterable_consumers[0]);
             ++consumer) {
            if (common[index].callback == iterable_consumers[consumer])
                function->consumes_iterable = PY68_NATIVE_ITERABLE_ARG;
        }
        status = py68_builtin_set_copy(
            runtime, (const Py68U8 *)common[index].name,
            py68_static_strlen(common[index].name),
            py68_value_from_object(&function->base));
        py68_object_release(runtime, &function->base);
        if (status != PY68_STATUS_OK) return status;
    }
    for (index = 0; index < sizeof(platform_vars) / sizeof(platform_vars[0]);
         ++index) {
        status = py68_native_new(runtime, platform_vars[index].name,
                                 platform_vars[index].minimum_arguments,
                                 platform_vars[index].maximum_arguments,
                                 platform_vars[index].callback, &function);
        if (status != PY68_STATUS_OK) return status;
        status = py68_builtin_set_copy(
            runtime, (const Py68U8 *)platform_vars[index].name,
            py68_static_strlen(platform_vars[index].name),
            py68_value_from_object(&function->base));
        py68_object_release(runtime, &function->base);
        if (status != PY68_STATUS_OK) return status;
    }
    return py68_types_install(runtime);
}
