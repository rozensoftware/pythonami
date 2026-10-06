/* 2026 by Piotr Rozentreter (Rozsoft) */

#include "py68k_value.h"
#include "py68k_dict.h"
#include "py68k_float.h"
#include "py68k_list.h"
#include "py68k_object.h"
#include "py68k_runtime.h"
#include "py68k_set.h"
#include "py68k_string.h"
#include "py68k_tuple.h"

#include <string.h>

Py68Value py68_value_none(void)
{
    Py68Value value;
    value.type = PY68_VALUE_NONE;
    value.reserved = 0;
    value.as.integer = 0;
    return value;
}

Py68Value py68_value_unbound(void)
{
    Py68Value value = py68_value_none();
    value.type = PY68_VALUE_UNBOUND;
    return value;
}

Py68Value py68_value_bool(int truth)
{
    Py68Value value = py68_value_none();
    value.type = PY68_VALUE_BOOL;
    value.as.integer = truth != 0;
    return value;
}

Py68Value py68_value_int(Py68I32 integer)
{
    Py68Value value = py68_value_none();
    value.type = PY68_VALUE_INT;
    value.as.integer = integer;
    return value;
}

Py68Value py68_value_from_object(Py68Object *object)
{
    Py68Value value = py68_value_none();
    value.type = PY68_VALUE_OBJECT;
    value.as.object = object;
    return value;
}

Py68Value py68_value_float_bits(Py68U32 bits)
{
    Py68Value value = py68_value_none();
    value.type = PY68_VALUE_FLOAT;
    value.as.integer = (Py68I32)bits;
    return value;
}

Py68U32 py68_value_float_bits_get(Py68Value value)
{
    if (value.type == PY68_VALUE_FLOAT)
        return (Py68U32)value.as.integer;
    if (value.type == PY68_VALUE_INT || value.type == PY68_VALUE_BOOL)
        return py68_f32_from_i32(value.as.integer);
    return 0;
}

int py68_value_is_number(Py68Value value)
{
    return value.type == PY68_VALUE_INT || value.type == PY68_VALUE_BOOL ||
           value.type == PY68_VALUE_FLOAT;
}

int py68_error_is_catchable(Py68U16 kind)
{
    return kind == PY68_ERROR_TYPE || kind == PY68_ERROR_VALUE ||
           kind == PY68_ERROR_INDEX || kind == PY68_ERROR_KEY ||
           kind == PY68_ERROR_ZERO_DIVISION || kind == PY68_ERROR_OVERFLOW ||
           kind == PY68_ERROR_NAME || kind == PY68_ERROR_IO ||
           kind == PY68_ERROR_RECURSION || kind == PY68_ERROR_IMPORT ||
           kind == PY68_ERROR_STOP_ITERATION || kind == PY68_ERROR_EXCEPTION;
}

int py68_value_truthy(Py68Value value)
{
    if (value.type == PY68_VALUE_NONE) return 0;
    if (value.type == PY68_VALUE_BOOL || value.type == PY68_VALUE_INT)
        return value.as.integer != 0;
    if (value.type == PY68_VALUE_FLOAT)
        return !py68_f32_is_zero((Py68U32)value.as.integer);
    if (value.type == PY68_VALUE_OBJECT && value.as.object != NULL) {
        if (value.as.object->type == PY68_OBJECT_STRING)
            return ((Py68String *)value.as.object)->length != 0;
        if (value.as.object->type == PY68_OBJECT_LIST)
            return ((Py68List *)value.as.object)->count != 0;
        if (value.as.object->type == PY68_OBJECT_TUPLE)
            return ((Py68Tuple *)value.as.object)->count != 0;
        if (value.as.object->type == PY68_OBJECT_DICT)
            return ((Py68Dict *)value.as.object)->count != 0;
        if (value.as.object->type == PY68_OBJECT_SET)
            return ((Py68Set *)value.as.object)->count != 0;
    }
    return 1;
}

int py68_value_hashable(Py68Value value)
{
    Py68U32 index;
    if (value.type == PY68_VALUE_NONE || value.type == PY68_VALUE_BOOL ||
        value.type == PY68_VALUE_INT || value.type == PY68_VALUE_FLOAT)
        return 1;
    if (value.type == PY68_VALUE_OBJECT && value.as.object != NULL) {
        if (value.as.object->type == PY68_OBJECT_STRING) return 1;
        if (value.as.object->type == PY68_OBJECT_TUPLE) {
            Py68Tuple *tuple = (Py68Tuple *)value.as.object;
            for (index = 0; index < tuple->count; ++index)
                if (!py68_value_hashable(tuple->items[index])) return 0;
            return 1;
        }
    }
    return 0;
}

int py68_value_hash(Py68Runtime *runtime, Py68Value value, Py68U32 *hash_out)
{
    if (!py68_value_hashable(value)) return 0;
    if (value.type == PY68_VALUE_NONE) {
        *hash_out = 0;
        return 1;
    }
    if (value.type == PY68_VALUE_BOOL || value.type == PY68_VALUE_INT) {
        *hash_out = (Py68U32)value.as.integer;
        return 1;
    }
    if (value.type == PY68_VALUE_FLOAT) {
        *hash_out = (Py68U32)value.as.integer;
        return 1;
    }
    if (value.type == PY68_VALUE_OBJECT && value.as.object != NULL &&
        value.as.object->type == PY68_OBJECT_STRING) {
        *hash_out = ((Py68String *)value.as.object)->hash;
        return 1;
    }
    if (value.type == PY68_VALUE_OBJECT && value.as.object != NULL &&
        value.as.object->type == PY68_OBJECT_TUPLE)
        return py68_tuple_hash(runtime, (Py68Tuple *)value.as.object, hash_out);
    return 0;
}

static int py68_list_equal(Py68Runtime *runtime, Py68List *left, Py68List *right)
{
    Py68U32 index;
    if (left == right) return 1;
    if (left->count != right->count) return 0;
    for (index = 0; index < left->count; ++index)
        if (!py68_value_equal(runtime, left->items[index], right->items[index]))
            return 0;
    return 1;
}

int py68_value_equal(Py68Runtime *runtime, Py68Value left, Py68Value right)
{
    if (left.type == PY68_VALUE_NONE || right.type == PY68_VALUE_NONE)
        return left.type == PY68_VALUE_NONE && right.type == PY68_VALUE_NONE;
    if (py68_value_is_number(left) && py68_value_is_number(right)) {
        if (left.type == PY68_VALUE_FLOAT || right.type == PY68_VALUE_FLOAT)
            return py68_f32_equal(py68_value_float_bits_get(left),
                                  py68_value_float_bits_get(right));
        return left.as.integer == right.as.integer;
    }
    if (left.type != PY68_VALUE_OBJECT || right.type != PY68_VALUE_OBJECT ||
        left.as.object == NULL || right.as.object == NULL)
        return 0;
    if (left.as.object == right.as.object) return 1;
    if (left.as.object->type != right.as.object->type) return 0;
    if (left.as.object->type == PY68_OBJECT_STRING) {
        Py68String *a = (Py68String *)left.as.object;
        Py68String *b = (Py68String *)right.as.object;
        return a->length == b->length && memcmp(a->data, b->data, a->length) == 0;
    }
    if (left.as.object->type == PY68_OBJECT_LIST)
        return py68_list_equal(runtime, (Py68List *)left.as.object,
                               (Py68List *)right.as.object);
    if (left.as.object->type == PY68_OBJECT_TUPLE)
        return py68_tuple_equal(runtime, (Py68Tuple *)left.as.object,
                                (Py68Tuple *)right.as.object);
    if (left.as.object->type == PY68_OBJECT_DICT)
        return py68_dict_equal(runtime, (Py68Dict *)left.as.object,
                               (Py68Dict *)right.as.object);
    if (left.as.object->type == PY68_OBJECT_SET)
        return py68_set_equal(runtime, (Py68Set *)left.as.object,
                              (Py68Set *)right.as.object);
    return 0;
}

static int py68_string_compare(Py68String *left, Py68String *right)
{
    Py68U32 min_length;
    int cmp;
    min_length = left->length < right->length ? left->length : right->length;
    if (min_length > 0) {
        cmp = memcmp(left->data, right->data, min_length);
        if (cmp < 0) return -1;
        if (cmp > 0) return 1;
    }
    if (left->length < right->length) return -1;
    if (left->length > right->length) return 1;
    return 0;
}

static int py68_sequence_compare(Py68Runtime *runtime, Py68Value *left_items,
                                 Py68U32 left_count, Py68Value *right_items,
                                 Py68U32 right_count, int *cmp_out)
{
    Py68U32 index;
    Py68U32 shared;
    shared = left_count < right_count ? left_count : right_count;
    for (index = 0; index < shared; ++index) {
        if (!py68_value_compare(runtime, left_items[index], right_items[index],
                                cmp_out))
            return 0;
        if (*cmp_out != 0) return 1;
    }
    if (left_count < right_count) *cmp_out = -1;
    else if (left_count > right_count) *cmp_out = 1;
    else *cmp_out = 0;
    return 1;
}

int py68_value_compare(Py68Runtime *runtime, Py68Value left, Py68Value right,
                       int *cmp_out)
{
    if (cmp_out == NULL) return 0;
    if (py68_value_is_number(left) && py68_value_is_number(right)) {
        if (left.type == PY68_VALUE_FLOAT || right.type == PY68_VALUE_FLOAT) {
            int cmp = py68_f32_compare(py68_value_float_bits_get(left),
                                       py68_value_float_bits_get(right));
            if (cmp == 2) return 0;
            *cmp_out = cmp;
            return 1;
        }
        if (left.as.integer < right.as.integer) *cmp_out = -1;
        else if (left.as.integer > right.as.integer) *cmp_out = 1;
        else *cmp_out = 0;
        return 1;
    }
    if (left.type != PY68_VALUE_OBJECT || right.type != PY68_VALUE_OBJECT ||
        left.as.object == NULL || right.as.object == NULL)
        return 0;
    if (left.as.object->type != right.as.object->type) return 0;
    if (left.as.object->type == PY68_OBJECT_STRING) {
        *cmp_out = py68_string_compare((Py68String *)left.as.object,
                                       (Py68String *)right.as.object);
        return 1;
    }
    if (left.as.object->type == PY68_OBJECT_LIST)
        return py68_sequence_compare(
            runtime, ((Py68List *)left.as.object)->items,
            ((Py68List *)left.as.object)->count,
            ((Py68List *)right.as.object)->items,
            ((Py68List *)right.as.object)->count, cmp_out);
    if (left.as.object->type == PY68_OBJECT_TUPLE)
        return py68_sequence_compare(
            runtime, ((Py68Tuple *)left.as.object)->items,
            ((Py68Tuple *)left.as.object)->count,
            ((Py68Tuple *)right.as.object)->items,
            ((Py68Tuple *)right.as.object)->count, cmp_out);
    return 0;
}

int py68_value_identical(Py68Value left, Py68Value right)
{
    if (left.type != right.type) return 0;
    if (left.type == PY68_VALUE_NONE) return 1;
    if (left.type == PY68_VALUE_BOOL || left.type == PY68_VALUE_INT ||
        left.type == PY68_VALUE_FLOAT)
        return left.as.integer == right.as.integer;
    if (left.type == PY68_VALUE_OBJECT)
        return left.as.object == right.as.object;
    return 0;
}

void py68_value_retain(Py68Value value)
{
    if (value.type == PY68_VALUE_OBJECT) py68_object_retain(value.as.object);
}

void py68_value_release(Py68Runtime *runtime, Py68Value value)
{
    if (value.type == PY68_VALUE_OBJECT) {
        py68_object_release(runtime, value.as.object);
    }
}
