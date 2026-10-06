/* 2026 by Piotr Rozentreter (Rozsoft) */

#include "py68k_attr.h"
#include "py68k_builtin.h"
#include "py68k_native.h"
#include "py68k_runtime.h"
#include "py68k_string.h"

#include <stdio.h>
#include <string.h>

static int expect_string(Py68Value value, const char *expected)
{
    Py68String *string;
    if (value.type != PY68_VALUE_OBJECT || value.as.object == NULL ||
        value.as.object->type != PY68_OBJECT_STRING)
        return 0;
    string = (Py68String *)value.as.object;
    return string->length == (Py68U32)strlen(expected) &&
           memcmp(string->data, expected, string->length) == 0;
}

int main(void)
{
    Py68Runtime runtime;
    Py68String *text;
    Py68String *spec;
    Py68String *one;
    Py68String *hello;
    Py68Value method;
    Py68Value args[2];
    Py68Value result;
    Py68BoundMethod *bound;
    int passed = 1;

    passed &= py68_runtime_initialize(&runtime) == PY68_STATUS_OK;
    passed &= py68_string_new_copy(&runtime, "AbC", 3, &text) == PY68_STATUS_OK;

    passed &= py68_attr_load(&runtime, py68_value_from_object(&text->base),
                             (const Py68U8 *)"upper", 5, &method) ==
              PY68_STATUS_OK;
    bound = (Py68BoundMethod *)method.as.object;
    args[0] = bound->self;
    passed &= py68_native_call((Py68NativeFunction *)bound->callable, &runtime, 1, args, &result) ==
              PY68_STATUS_OK;
    passed &= expect_string(result, "ABC");
    py68_value_release(&runtime, result);
    py68_value_release(&runtime, method);

    passed &= py68_attr_load(&runtime, py68_value_from_object(&text->base),
                             (const Py68U8 *)"find", 4, &method) ==
              PY68_STATUS_OK;
    bound = (Py68BoundMethod *)method.as.object;
    {
        Py68String *sub;
        passed &=
            py68_string_new_copy(&runtime, "b", 1, &sub) == PY68_STATUS_OK;
        args[0] = bound->self;
        args[1] = py68_value_from_object(&sub->base);
        passed &=
            py68_native_call((Py68NativeFunction *)bound->callable, &runtime, 2, args, &result) ==
            PY68_STATUS_OK;
        passed &= result.type == PY68_VALUE_INT && result.as.integer == 1;
        py68_value_release(&runtime, args[1]);
    }
    py68_value_release(&runtime, method);

    passed &= py68_string_new_copy(&runtime, "A", 1, &one) == PY68_STATUS_OK;
    args[0] = py68_value_from_object(&one->base);
    passed &= py68_builtin_ord(&runtime, 1, args, &result) == PY68_STATUS_OK;
    passed &= result.type == PY68_VALUE_INT && result.as.integer == 65;
    py68_value_release(&runtime, args[0]);

    args[0] = py68_value_int(65);
    passed &= py68_builtin_chr(&runtime, 1, args, &result) == PY68_STATUS_OK;
    passed &= expect_string(result, "A");
    py68_value_release(&runtime, result);

    passed &=
        py68_string_new_copy(&runtime, "A\nB", 3, &hello) == PY68_STATUS_OK;
    args[0] = py68_value_from_object(&hello->base);
    passed &= py68_builtin_repr(&runtime, 1, args, &result) == PY68_STATUS_OK;
    passed &= expect_string(result, "'A\\nB'");
    py68_value_release(&runtime, result);
    py68_value_release(&runtime, args[0]);

    args[0] = py68_value_int(42);
    passed &= py68_string_new_copy(&runtime, "04d", 3, &spec) == PY68_STATUS_OK;
    args[1] = py68_value_from_object(&spec->base);
    passed &= py68_builtin_format(&runtime, 2, args, &result) == PY68_STATUS_OK;
    passed &= expect_string(result, "0042");
    py68_value_release(&runtime, result);
    py68_value_release(&runtime, args[1]);

    py68_object_release(&runtime, &text->base);
    py68_runtime_shutdown(&runtime);
    passed &= runtime.allocator.stats.current_bytes == 0;
    if (passed) {
        puts("PASS: string methods tests");
        return 0;
    }
    return 1;
}
