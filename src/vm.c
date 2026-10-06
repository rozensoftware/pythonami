/* 2026 by Piotr Rozentreter (Rozsoft) */

#include "py68k_vm.h"
#include "py68k_verify.h"
#include "py68k_native.h"
#include "py68k_global.h"
#include "py68k_function.h"
#include "py68k_frame.h"
#include "py68k_builtin.h"
#include "py68k_string.h"
#include "py68k_list.h"
#include "py68k_range.h"
#include "py68k_tuple.h"
#include "py68k_dict.h"
#include "py68k_set.h"
#include "py68k_attr.h"
#include "py68k_exception.h"
#include "py68k_float.h"
#include "py68k_generator.h"
#include "py68k_import.h"
#include "py68k_module.h"
#include "py68k_type.h"

#include <stddef.h>
#include <string.h>

static void py68_vm_error(Py68Runtime *runtime, Py68ErrorKind kind,
                          const char *message);
static void py68_vm_clear_stack(Py68Runtime *runtime);
static Py68Status py68_vm_run(Py68Runtime *runtime, Py68Code *code,
                              int top_level);

static void py68_vm_error(Py68Runtime *runtime, Py68ErrorKind kind,
                          const char *message)
{
    Py68Location location;
    location.offset = 0; location.line = 0; location.column = 0; location.length = 0;
    py68_error_set(&runtime->error, kind, location, NULL, message);
}

static void py68_vm_append_traceback_text(Py68Runtime *runtime,
                                          const char *text)
{
    Py68U32 index;
    for (index = 0; text[index] != '\0' &&
         runtime->traceback_length < sizeof(runtime->traceback) - 1; ++index)
        runtime->traceback[runtime->traceback_length++] = text[index];
}

static void py68_vm_append_traceback_u32(Py68Runtime *runtime, Py68U32 value)
{
    char digits[10];
    Py68U32 count = 0;
    do {
        digits[count++] = (char)('0' + (value % 10));
        value /= 10;
    } while (value != 0 && count < sizeof(digits));
    while (count != 0 &&
           runtime->traceback_length < sizeof(runtime->traceback) - 1)
        runtime->traceback[runtime->traceback_length++] = digits[--count];
}

static void py68_vm_append_frame_line(Py68Runtime *runtime, Py68Code *code,
                                      Py68U32 ip)
{
    Py68U16 line = 0;
    Py68U16 name_index;
    py68_vm_append_traceback_text(runtime, "  at ");
    if (code != NULL && code->name_length != 0 && code->source_data != NULL) {
        for (name_index = 0; name_index < code->name_length &&
             runtime->traceback_length < sizeof(runtime->traceback) - 1;
             ++name_index)
            runtime->traceback[runtime->traceback_length++] =
                (char)code->source_data[code->name_offset + name_index];
    } else {
        py68_vm_append_traceback_text(runtime, "<module>");
    }
    py68_vm_append_traceback_text(runtime, " (");
    if (code != NULL && code->line_map != NULL &&
        ip < code->bytecode_length)
        line = code->line_map[ip];
    if (line != 0) {
        py68_vm_append_traceback_text(runtime, "line ");
        py68_vm_append_traceback_u32(runtime, line);
    } else {
        py68_vm_append_traceback_text(runtime, "line ?");
    }
    py68_vm_append_traceback_text(runtime, ")\n");
}

static void py68_vm_capture_traceback(Py68Runtime *runtime,
                                      Py68Code *current_code, Py68U32 ip)
{
    Py68U16 frame_index;
    runtime->traceback_length = 0;
    if (current_code != NULL)
        py68_vm_append_frame_line(runtime, current_code, ip);
    if (runtime->frame_count == 0) return;
    for (frame_index = runtime->frame_count; frame_index > 0; --frame_index) {
        Py68Frame *frame = &runtime->frames[frame_index - 1];
        if (frame->return_code == NULL) continue;
        py68_vm_append_frame_line(runtime, frame->return_code,
                                  frame->return_ip == 0 ? 0
                                                        : frame->return_ip - 1);
    }
    if (runtime->error.location.line == 0 && current_code != NULL &&
        current_code->line_map != NULL && ip < current_code->bytecode_length) {
        runtime->error.location.line = current_code->line_map[ip];
    }
}

static void py68_vm_fail(Py68Runtime *runtime, Py68Code *current_code,
                         Py68U32 ip)
{
    if (runtime->error.kind != PY68_ERROR_NONE)
        py68_vm_capture_traceback(runtime, current_code, ip);
    py68_vm_clear_stack(runtime);
    py68_frame_unwind(runtime);
}

static Py68Status py68_vm_grow(Py68Runtime *runtime)
{
    Py68U16 capacity = runtime->value_stack_capacity == 0 ? 32 :
                       (Py68U16)(runtime->value_stack_capacity * 2);
    Py68Value *values;
    if (capacity < runtime->value_stack_capacity) return PY68_STATUS_MEMORY_ERROR;
    values = (Py68Value *)py68_realloc(&runtime->allocator, PY68_MEM_STACK,
        runtime->value_stack,
        (Py68U32)runtime->value_stack_capacity * sizeof(Py68Value),
        (Py68U32)capacity * sizeof(Py68Value));
    if (values == NULL) return PY68_STATUS_MEMORY_ERROR;
    runtime->value_stack = values;
    runtime->value_stack_capacity = capacity;
    return PY68_STATUS_OK;
}

static Py68Status py68_vm_push(Py68Runtime *runtime, Py68Value value)
{
    Py68Status status;
    if (runtime->value_stack_count == runtime->value_stack_capacity) {
        status = py68_vm_grow(runtime);
        if (status != PY68_STATUS_OK) return status;
    }
    runtime->value_stack[runtime->value_stack_count++] = value;
    return PY68_STATUS_OK;
}

Py68Status py68_vm_push_owned(Py68Runtime *runtime, Py68Value value)
{
    return py68_vm_push(runtime, value);
}

/* Resume `generator` in a fresh activation frame whose operands start at the
   current stack top. Growth happens up front so restoring the saved operands
   cannot fail halfway through (D-0045). */
static Py68Status py68_vm_resume_generator(Py68Runtime *runtime,
                                           Py68Generator *generator,
                                           Py68Code *return_code,
                                           Py68U32 return_ip,
                                           Py68U16 resume_kind)
{
    Py68U16 stack_base = runtime->value_stack_count;
    Py68U16 index;
    Py68Status status;
    if (generator->state == PY68_GENERATOR_RUNNING) {
        py68_vm_error(runtime, PY68_ERROR_VALUE,
                      "generator is already executing");
        return PY68_STATUS_RUNTIME_ERROR;
    }
    if ((Py68U32)stack_base + (Py68U32)generator->stack_count > 65535UL) {
        py68_vm_error(runtime, PY68_ERROR_MEMORY, "value stack is too deep");
        return PY68_STATUS_RUNTIME_ERROR;
    }
    while ((Py68U32)runtime->value_stack_capacity <
           (Py68U32)stack_base + (Py68U32)generator->stack_count) {
        status = py68_vm_grow(runtime);
        if (status != PY68_STATUS_OK) return status;
    }
    status = py68_frame_push_generator(runtime, generator, return_code,
                                       return_ip, stack_base, resume_kind);
    if (status != PY68_STATUS_OK) return status;
    for (index = 0; index < generator->stack_count; ++index)
        runtime->value_stack[stack_base + index] = generator->stack[index];
    runtime->value_stack_count =
        (Py68U16)(stack_base + generator->stack_count);
    generator->stack_count = 0;
    generator->state = PY68_GENERATOR_RUNNING;
    return PY68_STATUS_OK;
}

static Py68Status py68_vm_pop(Py68Runtime *runtime, Py68Value *value)
{
    if (runtime->value_stack_count == 0) {
        py68_vm_error(runtime, PY68_ERROR_BYTECODE, "value stack underflow");
        return PY68_STATUS_RUNTIME_ERROR;
    }
    *value = runtime->value_stack[--runtime->value_stack_count];
    return PY68_STATUS_OK;
}

static Py68Status py68_vm_unpack(Py68Runtime *runtime, Py68U16 count)
{
    Py68Value sequence;
    Py68Value item;
    Py68U32 actual = 0;
    Py68U16 index;
    Py68U16 pushed = 0;
    Py68Status status;
    int is_list = 0;
    int is_tuple = 0;
    int is_string = 0;
    Py68List *list = NULL;
    Py68Tuple *tuple = NULL;
    Py68String *string = NULL;
    Py68String *character;

    status = py68_vm_pop(runtime, &sequence);
    if (status != PY68_STATUS_OK) return status;
    if (sequence.type == PY68_VALUE_OBJECT && sequence.as.object != NULL) {
        if (sequence.as.object->type == PY68_OBJECT_LIST) {
            list = (Py68List *)sequence.as.object;
            actual = list->count;
            is_list = 1;
        } else if (sequence.as.object->type == PY68_OBJECT_TUPLE) {
            tuple = (Py68Tuple *)sequence.as.object;
            actual = tuple->count;
            is_tuple = 1;
        } else if (sequence.as.object->type == PY68_OBJECT_STRING) {
            string = (Py68String *)sequence.as.object;
            actual = string->length;
            is_string = 1;
        }
    }
    if (!is_list && !is_tuple && !is_string) {
        py68_value_release(runtime, sequence);
        py68_vm_error(runtime, PY68_ERROR_TYPE, "cannot unpack non-sequence");
        return PY68_STATUS_RUNTIME_ERROR;
    }
    if (actual != (Py68U32)count) {
        py68_value_release(runtime, sequence);
        if (actual < (Py68U32)count)
            py68_vm_error(runtime, PY68_ERROR_VALUE,
                          "not enough values to unpack");
        else
            py68_vm_error(runtime, PY68_ERROR_VALUE,
                          "too many values to unpack");
        return PY68_STATUS_RUNTIME_ERROR;
    }
    index = count;
    while (index != 0) {
        --index;
        if (is_list) {
            status = py68_list_get_copy(runtime, list, (Py68I32)index, &item);
        } else if (is_tuple) {
            status = py68_tuple_get_copy(runtime, tuple, (Py68I32)index, &item);
        } else {
            status = py68_string_get_char(runtime, string, (Py68I32)index,
                                          &character);
            if (status == PY68_STATUS_OK)
                item = py68_value_from_object(&character->base);
        }
        if (status != PY68_STATUS_OK) {
            while (pushed != 0) {
                --runtime->value_stack_count;
                py68_value_release(
                    runtime, runtime->value_stack[runtime->value_stack_count]);
                --pushed;
            }
            py68_value_release(runtime, sequence);
            if (runtime->error.active == 0)
                py68_vm_error(runtime, PY68_ERROR_MEMORY, "unpack failed");
            return PY68_STATUS_RUNTIME_ERROR;
        }
        status = py68_vm_push(runtime, item);
        if (status != PY68_STATUS_OK) {
            py68_value_release(runtime, item);
            while (pushed != 0) {
                --runtime->value_stack_count;
                py68_value_release(
                    runtime, runtime->value_stack[runtime->value_stack_count]);
                --pushed;
            }
            py68_value_release(runtime, sequence);
            return status;
        }
        ++pushed;
    }
    py68_value_release(runtime, sequence);
    return PY68_STATUS_OK;
}

static int py68_vm_truth(Py68Value value)
{
    return py68_value_truthy(value);
}

/* Called only on backward branches, so straight-line code pays nothing. */
static Py68Status py68_vm_poll(Py68Runtime *runtime)
{
    if (runtime->poll_interval == 0) return PY68_STATUS_OK;
    if (runtime->poll_counter > 1) {
        --runtime->poll_counter;
        return PY68_STATUS_OK;
    }
    runtime->poll_counter = runtime->poll_interval;
    if (py68_platform_poll(runtime, PY68_POLL_BREAK) == PY68_STATUS_OK)
        return PY68_STATUS_OK;
    py68_vm_error(runtime, PY68_ERROR_INTERRUPT, "interrupted");
    return PY68_STATUS_RUNTIME_ERROR;
}

static int py68_float_finite_bits(Py68U32 bits)
{
    return py68_f32_is_finite(bits);
}

static int py68_vm_catch(Py68Runtime *runtime, Py68Code **current_code,
                         Py68U32 *ip)
{
    Py68Exception *exception;
    Py68Frame *frame;
    if (!py68_error_is_catchable(runtime->error.kind))
        return 0;
    /* Prefer an exception already installed by OP_RAISE / type call so user
       exception type pointers survive catch (Level 0.8). Only reuse when the
       kind matches the pending error — a prior except must not sticky-reuse. */
    if (runtime->current_exception.type == PY68_VALUE_OBJECT &&
        runtime->current_exception.as.object != NULL &&
        runtime->current_exception.as.object->type == PY68_OBJECT_EXCEPTION &&
        ((Py68Exception *)runtime->current_exception.as.object)->kind ==
            runtime->error.kind) {
        exception = (Py68Exception *)runtime->current_exception.as.object;
    } else {
        if (py68_exception_from_error(runtime, &exception) != PY68_STATUS_OK)
            return 0;
        py68_value_release(runtime, runtime->current_exception);
        runtime->current_exception = py68_value_from_object(&exception->base);
    }
    while (runtime->frame_count != 0) {
        frame = &runtime->frames[runtime->frame_count - 1];
        if (frame->try_count != 0) {
            Py68TryBlock *block = &frame->try_stack[frame->try_count - 1];
            --frame->try_count;
            while (runtime->value_stack_count > block->stack_depth) {
                --runtime->value_stack_count;
                py68_value_release(
                    runtime, runtime->value_stack[runtime->value_stack_count]);
            }
            py68_value_retain(runtime->current_exception);
            if (py68_vm_push(runtime, runtime->current_exception) !=
                PY68_STATUS_OK)
                return 0;
            *current_code = frame->code;
            *ip = block->handler_ip;
            py68_error_clear(&runtime->error);
            return 1;
        }
        if (frame->return_code == NULL)
            return 0;
        *current_code = frame->return_code;
        *ip = frame->return_ip;
        py68_frame_pop(runtime);
    }
    return 0;
}

static void py68_vm_clear_stack(Py68Runtime *runtime)
{
    while (runtime->value_stack_count != 0) {
        Py68Value value = runtime->value_stack[--runtime->value_stack_count];
        py68_value_release(runtime, value);
    }
}

static int py68_vm_add(Py68I32 left, Py68I32 right, Py68I32 *result)
{
    if ((right > 0 && left > (Py68I32)0x7fffffff - right) ||
        (right < 0 && left < (Py68I32)-2147483647 - 1 - right)) return 0;
    *result = left + right;
    return 1;
}

static int py68_vm_subtract(Py68I32 left, Py68I32 right, Py68I32 *result)
{
    if ((right < 0 && left > (Py68I32)0x7fffffff + right) ||
        (right > 0 && left < (Py68I32)-2147483647 - 1 + right)) return 0;
    *result = left - right;
    return 1;
}

static int py68_vm_multiply(Py68I32 left, Py68I32 right, Py68I32 *result)
{
    if (left == 0 || right == 0) { *result = 0; return 1; }
    if (left == -1 && right == (Py68I32)-2147483647 - 1) return 0;
    if (right == -1 && left == (Py68I32)-2147483647 - 1) return 0;
    if (left > 0) {
        if (right > 0 && left > 2147483647L / right) return 0;
        if (right < 0 && right < -2147483647L / left - 1) return 0;
    } else {
        if (right > 0 && left < -2147483647L / right - 1) return 0;
        if (right < 0 && left < 2147483647L / right) return 0;
    }
    *result = left * right;
    return 1;
}

static Py68Status py68_vm_binary(Py68Runtime *runtime, Py68U8 opcode)
{
    Py68Value left, right, result;
    Py68I32 quotient, remainder, integer;
    if (py68_vm_pop(runtime, &right) != PY68_STATUS_OK ||
        py68_vm_pop(runtime, &left) != PY68_STATUS_OK) {
        return PY68_STATUS_RUNTIME_ERROR;
    }
    if (opcode == OP_IS || opcode == OP_IS_NOT) {
        integer = py68_value_identical(left, right);
        if (opcode == OP_IS_NOT) integer = !integer;
        py68_value_release(runtime, left);
        py68_value_release(runtime, right);
        return py68_vm_push(runtime, py68_value_bool((int)integer));
    }
    if (opcode == OP_CONTAINS || opcode == OP_NOT_CONTAINS) {
        int found = 0;
        Py68Status status = PY68_STATUS_OK;
        if (right.type == PY68_VALUE_OBJECT && right.as.object != NULL &&
            right.as.object->type == PY68_OBJECT_STRING) {
            if (left.type != PY68_VALUE_OBJECT || left.as.object == NULL ||
                left.as.object->type != PY68_OBJECT_STRING) {
                py68_value_release(runtime, left);
                py68_value_release(runtime, right);
                py68_vm_error(runtime, PY68_ERROR_TYPE,
                              "'in' requires string as left operand");
                return PY68_STATUS_RUNTIME_ERROR;
            }
            found = py68_string_contains((Py68String *)right.as.object,
                                         (Py68String *)left.as.object);
        } else if (right.type == PY68_VALUE_OBJECT && right.as.object != NULL &&
                   right.as.object->type == PY68_OBJECT_LIST) {
            found = py68_list_has_item(runtime, (Py68List *)right.as.object,
                                       left);
        } else if (right.type == PY68_VALUE_OBJECT && right.as.object != NULL &&
                   right.as.object->type == PY68_OBJECT_TUPLE) {
            found = py68_tuple_has_item(runtime, (Py68Tuple *)right.as.object,
                                        left);
        } else if (right.type == PY68_VALUE_OBJECT && right.as.object != NULL &&
                   right.as.object->type == PY68_OBJECT_DICT) {
            status = py68_dict_has_key(runtime, (Py68Dict *)right.as.object,
                                       left, &found);
            if (status != PY68_STATUS_OK) {
                py68_value_release(runtime, left);
                py68_value_release(runtime, right);
                py68_vm_error(runtime, PY68_ERROR_TYPE, "unhashable type");
                return PY68_STATUS_RUNTIME_ERROR;
            }
        } else if (right.type == PY68_VALUE_OBJECT && right.as.object != NULL &&
                   right.as.object->type == PY68_OBJECT_SET) {
            status = py68_set_contains(runtime, (Py68Set *)right.as.object,
                                       left, &found);
            if (status != PY68_STATUS_OK) {
                py68_value_release(runtime, left);
                py68_value_release(runtime, right);
                py68_vm_error(runtime, PY68_ERROR_TYPE, "unhashable type");
                return PY68_STATUS_RUNTIME_ERROR;
            }
        } else {
            py68_value_release(runtime, left);
            py68_value_release(runtime, right);
            py68_vm_error(runtime, PY68_ERROR_TYPE,
                          "argument of type is not iterable");
            return PY68_STATUS_RUNTIME_ERROR;
        }
        if (opcode == OP_NOT_CONTAINS) found = !found;
        py68_value_release(runtime, left);
        py68_value_release(runtime, right);
        return py68_vm_push(runtime, py68_value_bool(found));
    }
    if (opcode == OP_ADD && left.type == PY68_VALUE_OBJECT &&
        right.type == PY68_VALUE_OBJECT && left.as.object != NULL &&
        right.as.object != NULL) {
        if (left.as.object->type == PY68_OBJECT_STRING &&
            right.as.object->type == PY68_OBJECT_STRING) {
            Py68String *string;
            Py68Status status = py68_string_concat(
                runtime, (Py68String *)left.as.object,
                (Py68String *)right.as.object, &string);
            py68_value_release(runtime, left);
            py68_value_release(runtime, right);
            if (status != PY68_STATUS_OK) return status;
            return py68_vm_push(runtime, py68_value_from_object(&string->base));
        }
        if (left.as.object->type == PY68_OBJECT_LIST &&
            right.as.object->type == PY68_OBJECT_LIST) {
            Py68List *list;
            Py68Status status = py68_list_concat(
                runtime, (Py68List *)left.as.object,
                (Py68List *)right.as.object, &list);
            py68_value_release(runtime, left);
            py68_value_release(runtime, right);
            if (status != PY68_STATUS_OK) return status;
            return py68_vm_push(runtime, py68_value_from_object(&list->base));
        }
        if (left.as.object->type == PY68_OBJECT_TUPLE &&
            right.as.object->type == PY68_OBJECT_TUPLE) {
            Py68Tuple *tuple;
            Py68Status status = py68_tuple_concat(
                runtime, (Py68Tuple *)left.as.object,
                (Py68Tuple *)right.as.object, &tuple);
            py68_value_release(runtime, left);
            py68_value_release(runtime, right);
            if (status != PY68_STATUS_OK) return status;
            return py68_vm_push(runtime, py68_value_from_object(&tuple->base));
        }
    }
    if (opcode == OP_EQUAL || opcode == OP_NOT_EQUAL) {
        integer = py68_value_equal(runtime, left, right);
        if (opcode == OP_NOT_EQUAL) integer = !integer;
        py68_value_release(runtime, left);
        py68_value_release(runtime, right);
        result.type = PY68_VM_BOOL;
        result.reserved = 0;
        result.as.integer = integer;
        return py68_vm_push(runtime, result);
    }
    if (opcode == OP_LESS || opcode == OP_LESS_EQUAL ||
        opcode == OP_GREATER || opcode == OP_GREATER_EQUAL) {
        int cmp = 0;
        if (!py68_value_compare(runtime, left, right, &cmp)) {
            py68_value_release(runtime, left);
            py68_value_release(runtime, right);
            py68_vm_error(runtime, PY68_ERROR_TYPE,
                          "unsupported operand types for ordering comparison");
            return PY68_STATUS_RUNTIME_ERROR;
        }
        py68_value_release(runtime, left);
        py68_value_release(runtime, right);
        if (opcode == OP_LESS) integer = cmp < 0;
        else if (opcode == OP_LESS_EQUAL) integer = cmp <= 0;
        else if (opcode == OP_GREATER) integer = cmp > 0;
        else integer = cmp >= 0;
        result.type = PY68_VM_BOOL;
        result.reserved = 0;
        result.as.integer = integer;
        return py68_vm_push(runtime, result);
    }
    if (opcode == OP_TRUE_DIVIDE ||
        left.type == PY68_VALUE_FLOAT || right.type == PY68_VALUE_FLOAT) {
        Py68U32 left_n;
        Py68U32 right_n;
        Py68U32 number;
        int cmp;
        int div_status;
        if (!py68_value_is_number(left) || !py68_value_is_number(right)) {
            py68_value_release(runtime, left);
            py68_value_release(runtime, right);
            py68_vm_error(runtime, PY68_ERROR_TYPE, "numeric operands required");
            return PY68_STATUS_RUNTIME_ERROR;
        }
        left_n = py68_value_float_bits_get(left);
        right_n = py68_value_float_bits_get(right);
        if ((opcode == OP_TRUE_DIVIDE || opcode == OP_FLOOR_DIVIDE ||
             opcode == OP_MODULO) && py68_f32_is_zero(right_n)) {
            py68_vm_error(runtime, PY68_ERROR_ZERO_DIVISION, "division by zero");
            return PY68_STATUS_RUNTIME_ERROR;
        }
        if (opcode == OP_ADD) number = py68_f32_add(left_n, right_n);
        else if (opcode == OP_SUBTRACT) number = py68_f32_sub(left_n, right_n);
        else if (opcode == OP_MULTIPLY) number = py68_f32_mul(left_n, right_n);
        else if (opcode == OP_TRUE_DIVIDE) {
            div_status = py68_f32_div(left_n, right_n, &number);
            if (div_status == 1) {
                py68_vm_error(runtime, PY68_ERROR_ZERO_DIVISION,
                              "division by zero");
                return PY68_STATUS_RUNTIME_ERROR;
            }
        } else if (opcode == OP_FLOOR_DIVIDE || opcode == OP_MODULO) {
            py68_vm_error(runtime, PY68_ERROR_TYPE,
                          "floor divide and modulo require integers");
            return PY68_STATUS_RUNTIME_ERROR;
        } else {
            result.type = PY68_VM_BOOL;
            result.reserved = 0;
            cmp = py68_f32_compare(left_n, right_n);
            if (cmp == 2) {
                py68_vm_error(runtime, PY68_ERROR_VALUE,
                              "non-finite float result");
                return PY68_STATUS_RUNTIME_ERROR;
            }
            if (opcode == OP_LESS) integer = cmp < 0;
            else if (opcode == OP_LESS_EQUAL) integer = cmp <= 0;
            else if (opcode == OP_GREATER) integer = cmp > 0;
            else integer = cmp >= 0;
            result.as.integer = integer;
            return py68_vm_push(runtime, result);
        }
        if (!py68_float_finite_bits(number)) {
            py68_vm_error(runtime, PY68_ERROR_VALUE,
                          "non-finite float result");
            return PY68_STATUS_RUNTIME_ERROR;
        }
        return py68_vm_push(runtime, py68_value_float_bits(number));
    }
    if ((left.type != PY68_VM_INT && left.type != PY68_VM_BOOL) ||
        (right.type != PY68_VM_INT && right.type != PY68_VM_BOOL)) {
        py68_value_release(runtime, left);
        py68_value_release(runtime, right);
        py68_vm_error(runtime, PY68_ERROR_TYPE, "integer operands required");
        return PY68_STATUS_RUNTIME_ERROR;
    }
    result.type = PY68_VM_INT; result.reserved = 0;
    if (opcode == OP_ADD) {
        if (!py68_vm_add(left.as.integer, right.as.integer, &integer)) goto overflow;
    } else if (opcode == OP_SUBTRACT) {
        if (!py68_vm_subtract(left.as.integer, right.as.integer, &integer)) goto overflow;
    } else if (opcode == OP_MULTIPLY) {
        if (!py68_vm_multiply(left.as.integer, right.as.integer, &integer)) goto overflow;
    } else if (opcode == OP_FLOOR_DIVIDE || opcode == OP_MODULO) {
        if (right.as.integer == 0) {
            py68_vm_error(runtime, PY68_ERROR_ZERO_DIVISION, "division by zero");
            return PY68_STATUS_RUNTIME_ERROR;
        }
        if (left.as.integer == (Py68I32)-2147483647 - 1 && right.as.integer == -1)
            goto overflow;
        quotient = left.as.integer / right.as.integer;
        remainder = left.as.integer % right.as.integer;
        if (remainder != 0 && ((left.as.integer < 0) != (right.as.integer < 0))) {
            --quotient; remainder += right.as.integer;
        }
        integer = opcode == OP_FLOOR_DIVIDE ? quotient : remainder;
    } else if (opcode == OP_EQUAL || opcode == OP_NOT_EQUAL ||
               opcode == OP_LESS || opcode == OP_LESS_EQUAL ||
               opcode == OP_GREATER || opcode == OP_GREATER_EQUAL) {
        result.type = PY68_VM_BOOL;
        if (opcode == OP_EQUAL) integer = left.as.integer == right.as.integer;
        else if (opcode == OP_NOT_EQUAL) integer = left.as.integer != right.as.integer;
        else if (opcode == OP_LESS) integer = left.as.integer < right.as.integer;
        else if (opcode == OP_LESS_EQUAL) integer = left.as.integer <= right.as.integer;
        else if (opcode == OP_GREATER) integer = left.as.integer > right.as.integer;
        else integer = left.as.integer >= right.as.integer;
    } else goto bad_opcode;
    result.as.integer = integer;
    return py68_vm_push(runtime, result);
overflow:
    py68_vm_error(runtime, PY68_ERROR_OVERFLOW, "integer overflow");
    return PY68_STATUS_RUNTIME_ERROR;
bad_opcode:
    py68_vm_error(runtime, PY68_ERROR_BYTECODE, "unsupported arithmetic opcode");
    return PY68_STATUS_RUNTIME_ERROR;
}

Py68Status py68_vm_execute(Py68Runtime *runtime, Py68Code *code)
{
    return py68_vm_run(runtime, code, 1);
}

Py68Status py68_vm_execute_module(Py68Runtime *runtime, Py68Code *code)
{
    return py68_vm_run(runtime, code, 0);
}

static Py68Status py68_vm_run(Py68Runtime *runtime, Py68Code *code,
                              int top_level)
{
    Py68Code *current_code = code;
    Py68U32 ip = 0;
    Py68U32 target;
    Py68U16 index;
    Py68U8 opcode;
    Py68Value value, duplicate;
    Py68Status status;
    Py68U16 entry_frames;
    Py68U16 entry_stack;

    status = py68_verify_code(current_code, &runtime->error);
    if (status != PY68_STATUS_OK) return status;
    if (top_level)
        runtime->frame_count = 0;
    entry_frames = runtime->frame_count;
    entry_stack = runtime->value_stack_count;
    status = py68_frame_push(runtime, current_code, NULL, 0, 0, NULL, 0,
                             NULL);
    if (status != PY68_STATUS_OK) return status;
    while (ip < current_code->bytecode_length) {
        opcode = current_code->bytecode[ip];
        switch (opcode) {
        case OP_HALT:
            if (runtime->frame_count > entry_frames + 1) {
                Py68Code *caller_code =
                    runtime->frames[runtime->frame_count - 1].return_code;
                Py68U32 caller_ip =
                    runtime->frames[runtime->frame_count - 1].return_ip;
                py68_frame_pop(runtime);
                current_code = caller_code;
                ip = caller_ip;
                status = py68_vm_push(runtime, py68_value_none());
                break;
            }
            while (runtime->frame_count > entry_frames)
                py68_frame_pop(runtime);
            while (runtime->value_stack_count > entry_stack) {
                --runtime->value_stack_count;
                py68_value_release(
                    runtime, runtime->value_stack[runtime->value_stack_count]);
            }
            return PY68_STATUS_OK;
        case OP_LOAD_NONE: case OP_LOAD_TRUE: case OP_LOAD_FALSE:
            value = py68_value_none();
            value.type = opcode == OP_LOAD_NONE ? PY68_VM_NONE : PY68_VM_BOOL;
            value.as.integer = opcode == OP_LOAD_TRUE;
            status = py68_vm_push(runtime, value); ip += 1; break;
        case OP_LOAD_CONST:
            index = (Py68U16)(((Py68U16)current_code->bytecode[ip + 1] << 8) |
                              current_code->bytecode[ip + 2]);
            if (current_code->constants[index].kind == PY68_CONSTANT_STRING) {
                Py68String *string;
                Py68Constant *constant = &current_code->constants[index];
                if (current_code->source_data == NULL ||
                    constant->offset + constant->length > current_code->source_length) {
                    py68_vm_error(runtime, PY68_ERROR_BYTECODE, "string constant out of range");
                    status = PY68_STATUS_RUNTIME_ERROR; ip = current_code->bytecode_length; break;
                }
                status = py68_string_new_from_escaped(runtime,
                    (const char *)current_code->source_data + constant->offset,
                    constant->length, &string);
                if (status != PY68_STATUS_OK) {
                    py68_vm_error(runtime, PY68_ERROR_VALUE,
                                  "invalid string escape sequence");
                    status = PY68_STATUS_RUNTIME_ERROR;
                    ip = current_code->bytecode_length;
                    break;
                }
                status = py68_vm_push(runtime, py68_value_from_object(&string->base));
                ip += 3;
                break;
            }
            if (current_code->constants[index].kind == PY68_CONSTANT_FLOAT) {
                value = py68_value_float_bits(
                    (Py68U32)current_code->constants[index].integer);
                status = py68_vm_push(runtime, value);
                ip += 3;
                break;
            }
            if (current_code->constants[index].kind != PY68_CONSTANT_INTEGER) {
                py68_vm_error(runtime, PY68_ERROR_TYPE, "integer constant required");
                status = PY68_STATUS_RUNTIME_ERROR; ip = current_code->bytecode_length; break;
            }
            value = py68_value_int(current_code->constants[index].integer);
            status = py68_vm_push(runtime, value); ip += 3; break;
        case OP_LOAD_GLOBAL: {
            const Py68U8 *name_bytes;
            Py68U16 name_length;
            Py68Module *globals_owner = NULL;
            index = (Py68U16)(((Py68U16)current_code->bytecode[ip + 1] << 8) |
                              current_code->bytecode[ip + 2]);
            name_bytes = py68_code_name_bytes(current_code, index);
            name_length = current_code->name_lengths[index];
            if (runtime->frame_count != 0)
                globals_owner =
                    runtime->frames[runtime->frame_count - 1].globals_owner;
            if (globals_owner != NULL)
                status = py68_module_get(runtime, globals_owner, name_bytes,
                                         name_length, &value);
            else
                status = py68_global_get_copy(runtime, name_bytes, name_length,
                                              &value);
            if (status != PY68_STATUS_OK)
                status = py68_builtin_get_copy(runtime, name_bytes,
                                               name_length, &value);
            if (status == PY68_STATUS_OK)
                status = py68_vm_push(runtime, value);
            else
                py68_vm_error(runtime, PY68_ERROR_NAME, "name is not defined");
            ip += 3; break;
        }
        case OP_STORE_GLOBAL: {
            const Py68U8 *name_bytes;
            Py68U16 name_length;
            index = (Py68U16)(((Py68U16)current_code->bytecode[ip + 1] << 8) |
                              current_code->bytecode[ip + 2]);
            name_bytes = py68_code_name_bytes(current_code, index);
            name_length = current_code->name_lengths[index];
            status = py68_vm_pop(runtime, &value);
            if (status == PY68_STATUS_OK) {
                status = py68_global_set_copy(runtime, name_bytes,
                                              name_length, value);
                py68_value_release(runtime, value);
            }
            ip += 3; break;
        }
        case OP_LOAD_LOCAL:
            index = (Py68U16)(((Py68U16)current_code->bytecode[ip + 1] << 8) |
                              current_code->bytecode[ip + 2]);
            status = py68_frame_get_local(runtime, index, &value);
            if (status == PY68_STATUS_OK && value.type == PY68_VALUE_UNBOUND) {
                py68_vm_error(runtime, PY68_ERROR_NAME,
                              "local variable referenced before assignment");
                status = PY68_STATUS_RUNTIME_ERROR;
            } else if (status == PY68_STATUS_OK) {
                status = py68_vm_push(runtime, value);
            }
            ip += 3; break;
        case OP_STORE_LOCAL:
            index = (Py68U16)(((Py68U16)current_code->bytecode[ip + 1] << 8) |
                              current_code->bytecode[ip + 2]);
            status = py68_vm_pop(runtime, &value);
            if (status == PY68_STATUS_OK) {
                status = py68_frame_set_local_copy(runtime, index, value);
                py68_value_release(runtime, value);
            }
            ip += 3; break;
        case OP_MAKE_FUNCTION: {
            Py68Code *nested_code;
            Py68Function *made_function;
            index = (Py68U16)(((Py68U16)current_code->bytecode[ip + 1] << 8) |
                              current_code->bytecode[ip + 2]);
            if (index >= current_code->constant_count ||
                current_code->constants[index].kind != PY68_CONSTANT_CODE ||
                current_code->constants[index].integer >=
                    current_code->nested_count) {
                py68_vm_error(runtime, PY68_ERROR_BYTECODE,
                              "invalid function constant");
                status = PY68_STATUS_RUNTIME_ERROR;
                ip = current_code->bytecode_length;
                break;
            }
            nested_code = &current_code->nested[
                current_code->constants[index].integer];
            status = py68_function_new(runtime, nested_code,
                                       nested_code->argument_count,
                                       nested_code->local_count,
                                       runtime->executing_module,
                                       &made_function);
            if (status == PY68_STATUS_OK) {
                status = py68_vm_push(runtime,
                    py68_value_from_object(&made_function->base));
            }
            ip += 3; break;
        }
        case OP_POP:
            status = py68_vm_pop(runtime, &value);
            if (status == PY68_STATUS_OK)
                py68_value_release(runtime, value);
            ip += 1;
            break;
        case OP_UNPACK: {
            Py68U16 unpack_count =
                (Py68U16)(((Py68U16)current_code->bytecode[ip + 1] << 8) |
                          current_code->bytecode[ip + 2]);
            status = py68_vm_unpack(runtime, unpack_count);
            ip += 3;
            break;
        }
        case OP_STORE_INDEX: {
            Py68Value value_val, index_val, container_val;
            status = py68_vm_pop(runtime, &value_val);
            if (status == PY68_STATUS_OK)
                status = py68_vm_pop(runtime, &index_val);
            if (status == PY68_STATUS_OK)
                status = py68_vm_pop(runtime, &container_val);
            if (status != PY68_STATUS_OK) break;
            if (container_val.type == PY68_VALUE_OBJECT &&
                container_val.as.object != NULL &&
                container_val.as.object->type == PY68_OBJECT_DICT) {
                status = py68_dict_set_copy(
                    runtime, (Py68Dict *)container_val.as.object, index_val,
                    value_val);
                if (status != PY68_STATUS_OK) {
                    py68_vm_error(runtime, PY68_ERROR_TYPE,
                                  "unhashable or cyclic dict key");
                    status = PY68_STATUS_RUNTIME_ERROR;
                    ip = current_code->bytecode_length;
                }
            } else if (index_val.type != PY68_VALUE_INT &&
                index_val.type != PY68_VALUE_BOOL) {
                py68_value_release(runtime, value_val);
                py68_value_release(runtime, index_val);
                py68_value_release(runtime, container_val);
                py68_vm_error(runtime, PY68_ERROR_TYPE, "index must be integer");
                status = PY68_STATUS_RUNTIME_ERROR;
                ip = current_code->bytecode_length;
                break;
            } else if (container_val.type == PY68_VALUE_OBJECT &&
                container_val.as.object != NULL &&
                container_val.as.object->type == PY68_OBJECT_LIST) {
                status = py68_list_set_copy(
                    runtime, (Py68List *)container_val.as.object,
                    index_val.as.integer, value_val);
                if (status != PY68_STATUS_OK) {
                    py68_vm_error(runtime, PY68_ERROR_INDEX,
                                  "list index out of range");
                    status = PY68_STATUS_RUNTIME_ERROR;
                    ip = current_code->bytecode_length;
                }
            } else if (container_val.type == PY68_VALUE_OBJECT &&
                       container_val.as.object != NULL &&
                       container_val.as.object->type == PY68_OBJECT_STRING) {
                py68_vm_error(runtime, PY68_ERROR_TYPE,
                              "strings do not support item assignment");
                status = PY68_STATUS_RUNTIME_ERROR;
                ip = current_code->bytecode_length;
            } else if (container_val.type == PY68_VALUE_OBJECT &&
                       container_val.as.object != NULL &&
                       container_val.as.object->type == PY68_OBJECT_TUPLE) {
                py68_vm_error(runtime, PY68_ERROR_TYPE,
                              "tuples do not support item assignment");
                status = PY68_STATUS_RUNTIME_ERROR;
                ip = current_code->bytecode_length;
            } else {
                py68_vm_error(runtime, PY68_ERROR_TYPE,
                              "container does not support item assignment");
                status = PY68_STATUS_RUNTIME_ERROR;
                ip = current_code->bytecode_length;
            }
            py68_value_release(runtime, value_val);
            py68_value_release(runtime, index_val);
            py68_value_release(runtime, container_val);
            ip += 1;
            break;
        }
        case OP_BUILD_LIST: {
            Py68U16 count = (Py68U16)(((Py68U16)current_code->bytecode[ip + 1] << 8) |
                                      current_code->bytecode[ip + 2]);
            Py68List *list;
            Py68U16 item;
            if (runtime->value_stack_count < count ||
                py68_list_new(runtime, &list) != PY68_STATUS_OK) {
                py68_vm_error(runtime, PY68_ERROR_MEMORY, "list construction failed");
                status = PY68_STATUS_RUNTIME_ERROR;
                ip = current_code->bytecode_length;
                break;
            }
            for (item = 0; item < count; ++item) {
                Py68Value element = runtime->value_stack[
                    runtime->value_stack_count - count + item];
                status = py68_list_append_copy(runtime, list, element);
                if (status != PY68_STATUS_OK) break;
            }
            if (status == PY68_STATUS_OK) {
                while (count != 0) {
                    --count;
                    py68_value_release(runtime, runtime->value_stack[
                        runtime->value_stack_count - 1]);
                    --runtime->value_stack_count;
                }
                status = py68_vm_push(runtime, py68_value_from_object(&list->base));
            } else {
                py68_object_release(runtime, &list->base);
            }
            ip += 3;
            break;
        }
        case OP_RANGE_INIT: {
            Py68U8 argc = current_code->bytecode[ip + 1];
            Py68I32 start = 0;
            Py68I32 stop = 0;
            Py68I32 step = 1;
            Py68Range *range_obj = NULL;
            Py68Value arg0, arg1, arg2;
            if (argc < 1 || argc > 3 || runtime->value_stack_count < argc) {
                py68_vm_error(runtime, PY68_ERROR_TYPE, "range arguments invalid");
                status = PY68_STATUS_RUNTIME_ERROR;
                ip = current_code->bytecode_length;
                break;
            }
            if (argc == 1) {
                status = py68_vm_pop(runtime, &arg0);
                if (status != PY68_STATUS_OK) break;
                /* Iterators pass straight through: a range iterator, or a
                   generator, which is its own iterator (D-0045). */
                if (arg0.type == PY68_VALUE_OBJECT && arg0.as.object != NULL &&
                    (arg0.as.object->type == PY68_OBJECT_RANGE ||
                     arg0.as.object->type == PY68_OBJECT_GENERATOR)) {
                    status = py68_vm_push(runtime, arg0);
                    ip += 2;
                    break;
                }
                {
                    Py68Range *converted = NULL;
                    Py68Status convert = py68_iterable_get_iter(
                        runtime, arg0, &converted);
                    if (convert == PY68_STATUS_OK) {
                        py68_value_release(runtime, arg0);
                        status = py68_vm_push(
                            runtime, py68_value_from_object(&converted->base));
                        ip += 2;
                        break;
                    }
                    if (convert == PY68_STATUS_MEMORY_ERROR) {
                        py68_value_release(runtime, arg0);
                        status = convert;
                        ip = current_code->bytecode_length;
                        break;
                    }
                }
                if (arg0.type != PY68_VALUE_INT) {
                    py68_value_release(runtime, arg0);
                    py68_vm_error(runtime, PY68_ERROR_TYPE, "integer required for range");
                    status = PY68_STATUS_RUNTIME_ERROR;
                    ip = current_code->bytecode_length;
                    break;
                }
                stop = arg0.as.integer;
            } else if (argc == 2) {
                status = py68_vm_pop(runtime, &arg1);
                if (status == PY68_STATUS_OK) status = py68_vm_pop(runtime, &arg0);
                if (status != PY68_STATUS_OK ||
                    arg0.type != PY68_VALUE_INT || arg1.type != PY68_VALUE_INT) {
                    py68_vm_error(runtime, PY68_ERROR_TYPE, "integers required for range");
                    status = PY68_STATUS_RUNTIME_ERROR;
                    ip = current_code->bytecode_length;
                    break;
                }
                start = arg0.as.integer;
                stop = arg1.as.integer;
            } else {
                status = py68_vm_pop(runtime, &arg2);
                if (status == PY68_STATUS_OK) status = py68_vm_pop(runtime, &arg1);
                if (status == PY68_STATUS_OK) status = py68_vm_pop(runtime, &arg0);
                if (status != PY68_STATUS_OK ||
                    arg0.type != PY68_VALUE_INT || arg1.type != PY68_VALUE_INT ||
                    arg2.type != PY68_VALUE_INT) {
                    py68_vm_error(runtime, PY68_ERROR_TYPE, "integers required for range");
                    status = PY68_STATUS_RUNTIME_ERROR;
                    ip = current_code->bytecode_length;
                    break;
                }
                start = arg0.as.integer;
                stop = arg1.as.integer;
                step = arg2.as.integer;
            }
            if (step == 0) {
                py68_vm_error(runtime, PY68_ERROR_VALUE, "range step cannot be zero");
                status = PY68_STATUS_RUNTIME_ERROR;
                ip = current_code->bytecode_length;
                break;
            }
            status = py68_range_new(runtime, start, stop, step, &range_obj);
            if (status == PY68_STATUS_OK) {
                status = py68_vm_push(runtime,
                    py68_value_from_object(&range_obj->base));
            }
            ip += 2;
            break;
        }
        case OP_RANGE_NEXT: {
            Py68I16 displacement = (Py68I16)(((Py68U16)current_code->bytecode[ip + 1] << 8) |
                                              current_code->bytecode[ip + 2]);
            Py68Value range_val;
            Py68Range *range_obj;
            Py68Value next_val;
            int has_next = 0;
            if (runtime->value_stack_count == 0) {
                py68_vm_error(runtime, PY68_ERROR_BYTECODE, "value stack underflow");
                status = PY68_STATUS_RUNTIME_ERROR;
                ip = current_code->bytecode_length;
                break;
            }
            range_val = runtime->value_stack[runtime->value_stack_count - 1];
            if (range_val.type == PY68_VALUE_OBJECT &&
                range_val.as.object != NULL &&
                range_val.as.object->type == PY68_OBJECT_GENERATOR) {
                Py68Generator *generator = (Py68Generator *)range_val.as.object;
                if (generator->state == PY68_GENERATOR_DONE) {
                    /* An exhausted generator iterates as empty, as in
                       CPython: pop it and take the loop-exit branch. */
                    --runtime->value_stack_count;
                    py68_value_release(runtime, range_val);
                    ip = (Py68U32)((Py68I32)(ip + 3) + (Py68I32)displacement);
                    status = PY68_STATUS_OK;
                    break;
                }
                status = py68_vm_resume_generator(runtime, generator,
                                                  current_code, ip,
                                                  PY68_RESUME_FOR);
                if (status == PY68_STATUS_OK) {
                    current_code = generator->code;
                    ip = generator->resume_ip;
                }
                break;
            }
            if (range_val.type != PY68_VALUE_OBJECT || range_val.as.object == NULL ||
                range_val.as.object->type != PY68_OBJECT_RANGE) {
                py68_vm_error(runtime, PY68_ERROR_TYPE, "range iterator required");
                status = PY68_STATUS_RUNTIME_ERROR;
                ip = current_code->bytecode_length;
                break;
            }
            range_obj = (Py68Range *)range_val.as.object;
            status = py68_range_next_value(runtime, range_obj, &next_val, &has_next);
            if (status != PY68_STATUS_OK) {
                ip = current_code->bytecode_length;
                break;
            }
            if (has_next) {
                status = py68_vm_push(runtime, next_val);
                ip += 3;
            } else {
                status = py68_vm_pop(runtime, &range_val);
                py68_value_release(runtime, range_val);
                target = (Py68U32)((Py68I32)(ip + 3) + (Py68I32)displacement);
                ip = target;
                status = PY68_STATUS_OK;
            }
            break;
        }
        case OP_DUP:
            if (runtime->value_stack_count == 0) {
                py68_vm_error(runtime, PY68_ERROR_BYTECODE, "value stack underflow");
                status = PY68_STATUS_RUNTIME_ERROR;
            } else {
                duplicate = runtime->value_stack[runtime->value_stack_count - 1];
                py68_value_retain(duplicate);
                status = py68_vm_push(runtime, duplicate);
            }
            ip += 1; break;
        case OP_LOAD_INDEX: {
            Py68Value index_val, container_val, item_val;
            status = py68_vm_pop(runtime, &index_val);
            if (status == PY68_STATUS_OK)
                status = py68_vm_pop(runtime, &container_val);
            if (status != PY68_STATUS_OK) break;
            if (index_val.type != PY68_VALUE_INT &&
                index_val.type != PY68_VALUE_BOOL &&
                !(container_val.type == PY68_VALUE_OBJECT &&
                  container_val.as.object != NULL &&
                  container_val.as.object->type == PY68_OBJECT_DICT)) {
                py68_value_release(runtime, index_val);
                py68_value_release(runtime, container_val);
                py68_vm_error(runtime, PY68_ERROR_TYPE, "index must be integer");
                status = PY68_STATUS_RUNTIME_ERROR;
                ip = current_code->bytecode_length;
                break;
            }
            if (container_val.type == PY68_VALUE_OBJECT &&
                container_val.as.object != NULL &&
                container_val.as.object->type == PY68_OBJECT_LIST) {
                status = py68_list_get_copy(runtime,
                    (Py68List *)container_val.as.object,
                    index_val.as.integer, &item_val);
                if (status != PY68_STATUS_OK) {
                    py68_vm_error(runtime, PY68_ERROR_INDEX, "list index out of range");
                    status = PY68_STATUS_RUNTIME_ERROR;
                    ip = current_code->bytecode_length;
                } else {
                    status = py68_vm_push(runtime, item_val);
                }
            } else if (container_val.type == PY68_VALUE_OBJECT &&
                       container_val.as.object != NULL &&
                       container_val.as.object->type == PY68_OBJECT_STRING) {
                Py68String *character;
                status = py68_string_get_char(
                    runtime, (Py68String *)container_val.as.object,
                    index_val.as.integer, &character);
                if (status != PY68_STATUS_OK) {
                    py68_vm_error(runtime, PY68_ERROR_INDEX,
                                  "string index out of range");
                    status = PY68_STATUS_RUNTIME_ERROR;
                    ip = current_code->bytecode_length;
                } else {
                    status = py68_vm_push(
                        runtime, py68_value_from_object(&character->base));
                }
            } else if (container_val.type == PY68_VALUE_OBJECT &&
                       container_val.as.object != NULL &&
                       container_val.as.object->type == PY68_OBJECT_TUPLE) {
                status = py68_tuple_get_copy(
                    runtime, (Py68Tuple *)container_val.as.object,
                    index_val.as.integer, &item_val);
                if (status != PY68_STATUS_OK) {
                    py68_vm_error(runtime, PY68_ERROR_INDEX,
                                  "tuple index out of range");
                    status = PY68_STATUS_RUNTIME_ERROR;
                    ip = current_code->bytecode_length;
                } else {
                    status = py68_vm_push(runtime, item_val);
                }
            } else if (container_val.type == PY68_VALUE_OBJECT &&
                       container_val.as.object != NULL &&
                       container_val.as.object->type == PY68_OBJECT_DICT) {
                status = py68_dict_get_copy(
                    runtime, (Py68Dict *)container_val.as.object, index_val,
                    &item_val);
                if (status != PY68_STATUS_OK) {
                    py68_vm_error(runtime, PY68_ERROR_KEY, "key not found");
                    status = PY68_STATUS_RUNTIME_ERROR;
                    ip = current_code->bytecode_length;
                } else {
                    status = py68_vm_push(runtime, item_val);
                }
            } else {
                py68_vm_error(runtime, PY68_ERROR_TYPE, "container does not support indexing");
                status = PY68_STATUS_RUNTIME_ERROR;
                ip = current_code->bytecode_length;
            }
            py68_value_release(runtime, index_val);
            py68_value_release(runtime, container_val);
            ip += 1;
            break;
        }
        case OP_LOAD_SLICE: {
            Py68Value end_val, start_val, container_val;
            int start_omitted;
            int end_omitted;
            status = py68_vm_pop(runtime, &end_val);
            if (status == PY68_STATUS_OK)
                status = py68_vm_pop(runtime, &start_val);
            if (status == PY68_STATUS_OK)
                status = py68_vm_pop(runtime, &container_val);
            if (status != PY68_STATUS_OK) break;
            start_omitted = start_val.type == PY68_VALUE_NONE;
            end_omitted = end_val.type == PY68_VALUE_NONE;
            if ((!start_omitted && start_val.type != PY68_VALUE_INT) ||
                (!end_omitted && end_val.type != PY68_VALUE_INT)) {
                py68_value_release(runtime, end_val);
                py68_value_release(runtime, start_val);
                py68_value_release(runtime, container_val);
                py68_vm_error(runtime, PY68_ERROR_TYPE,
                              "slice bounds must be integers or None");
                status = PY68_STATUS_RUNTIME_ERROR;
                ip = current_code->bytecode_length;
                break;
            }
            if (container_val.type == PY68_VALUE_OBJECT &&
                container_val.as.object != NULL &&
                container_val.as.object->type == PY68_OBJECT_STRING) {
                Py68String *sliced;
                status = py68_string_slice(
                    runtime, (Py68String *)container_val.as.object,
                    start_omitted ? 0 : start_val.as.integer,
                    end_omitted ? 0 : end_val.as.integer, start_omitted,
                    end_omitted, &sliced);
                if (status == PY68_STATUS_OK)
                    status = py68_vm_push(
                        runtime, py68_value_from_object(&sliced->base));
            } else if (container_val.type == PY68_VALUE_OBJECT &&
                       container_val.as.object != NULL &&
                       container_val.as.object->type == PY68_OBJECT_LIST) {
                Py68List *sliced;
                status = py68_list_slice(
                    runtime, (Py68List *)container_val.as.object,
                    start_omitted ? 0 : start_val.as.integer,
                    end_omitted ? 0 : end_val.as.integer, start_omitted,
                    end_omitted, &sliced);
                if (status == PY68_STATUS_OK)
                    status = py68_vm_push(
                        runtime, py68_value_from_object(&sliced->base));
            } else if (container_val.type == PY68_VALUE_OBJECT &&
                       container_val.as.object != NULL &&
                       container_val.as.object->type == PY68_OBJECT_TUPLE) {
                Py68Tuple *sliced;
                status = py68_tuple_slice(
                    runtime, (Py68Tuple *)container_val.as.object,
                    start_omitted ? 0 : start_val.as.integer,
                    end_omitted ? 0 : end_val.as.integer, start_omitted,
                    end_omitted, &sliced);
                if (status == PY68_STATUS_OK)
                    status = py68_vm_push(
                        runtime, py68_value_from_object(&sliced->base));
            } else {
                py68_vm_error(runtime, PY68_ERROR_TYPE,
                              "container does not support slicing");
                status = PY68_STATUS_RUNTIME_ERROR;
                ip = current_code->bytecode_length;
            }
            py68_value_release(runtime, end_val);
            py68_value_release(runtime, start_val);
            py68_value_release(runtime, container_val);
            if (status != PY68_STATUS_OK &&
                status != PY68_STATUS_RUNTIME_ERROR) {
                py68_vm_error(runtime, PY68_ERROR_MEMORY, "slice failed");
                status = PY68_STATUS_RUNTIME_ERROR;
                ip = current_code->bytecode_length;
            }
            ip += 1;
            break;
        }
        case OP_ADD: case OP_SUBTRACT: case OP_MULTIPLY: case OP_FLOOR_DIVIDE:
        case OP_MODULO: case OP_TRUE_DIVIDE: case OP_EQUAL: case OP_NOT_EQUAL:
        case OP_LESS:
        case OP_LESS_EQUAL: case OP_GREATER: case OP_GREATER_EQUAL:
        case OP_CONTAINS: case OP_NOT_CONTAINS:
        case OP_IS: case OP_IS_NOT:
            status = py68_vm_binary(runtime, opcode); ip += 1; break;
        case OP_NEGATE:
            status = py68_vm_pop(runtime, &value);
            if (status == PY68_STATUS_OK && value.type == PY68_VM_INT) {
                if (value.as.integer == (Py68I32)-2147483647 - 1) {
                    py68_vm_error(runtime, PY68_ERROR_OVERFLOW, "integer overflow");
                    status = PY68_STATUS_RUNTIME_ERROR;
                } else { value.as.integer = -value.as.integer; status = py68_vm_push(runtime, value); }
            } else if (status == PY68_STATUS_OK && value.type == PY68_VALUE_FLOAT) {
                status = py68_vm_push(runtime,
                    py68_value_float_bits(
                        py68_f32_negate((Py68U32)value.as.integer)));
            } else if (status == PY68_STATUS_OK) {
                py68_vm_error(runtime, PY68_ERROR_TYPE, "numeric operand required");
                status = PY68_STATUS_RUNTIME_ERROR;
            }
            ip += 1; break;
        case OP_POSITIVE:
            status = py68_vm_pop(runtime, &value);
            if (status == PY68_STATUS_OK &&
                (value.type == PY68_VM_INT || value.type == PY68_VALUE_FLOAT))
                status = py68_vm_push(runtime, value);
            else if (status == PY68_STATUS_OK) {
                py68_vm_error(runtime, PY68_ERROR_TYPE, "numeric operand required");
                status = PY68_STATUS_RUNTIME_ERROR;
            }
            ip += 1; break;
        case OP_NOT: {
            int truth;
            status = py68_vm_pop(runtime, &value);
            if (status == PY68_STATUS_OK) {
                truth = py68_vm_truth(value);
                py68_value_release(runtime, value);
                status = py68_vm_push(runtime, py68_value_bool(!truth));
            }
            ip += 1; break;
        }
        case OP_JUMP:
            target = (Py68U32)((Py68I32)(ip + 3) +
                (Py68I32)(Py68I16)(((Py68U16)current_code->bytecode[ip + 1] << 8) |
                                   current_code->bytecode[ip + 2]));
            status = target <= ip ? py68_vm_poll(runtime) : PY68_STATUS_OK;
            if (status == PY68_STATUS_OK) ip = target;
            break;
        case OP_JUMP_IF_FALSE: case OP_JUMP_IF_TRUE: {
            int taken;
            status = py68_vm_pop(runtime, &value);
            if (status != PY68_STATUS_OK) break;
            taken = (opcode == OP_JUMP_IF_FALSE && !py68_vm_truth(value)) ||
                    (opcode == OP_JUMP_IF_TRUE && py68_vm_truth(value));
            py68_value_release(runtime, value);
            if (!taken) { ip += 3; break; }
            target = (Py68U32)((Py68I32)(ip + 3) +
                (Py68I32)(Py68I16)(((Py68U16)current_code->bytecode[ip + 1] << 8) |
                                   current_code->bytecode[ip + 2]));
            if (target <= ip) status = py68_vm_poll(runtime);
            if (status == PY68_STATUS_OK) ip = target;
            break;
        }
        case OP_JUMP_IF_FALSE_OR_POP: case OP_JUMP_IF_TRUE_OR_POP:
            if (runtime->value_stack_count == 0) {
                py68_vm_error(runtime, PY68_ERROR_BYTECODE, "value stack underflow");
                status = PY68_STATUS_RUNTIME_ERROR;
                break;
            }
            value = runtime->value_stack[runtime->value_stack_count - 1];
            status = PY68_STATUS_OK;
            if ((opcode == OP_JUMP_IF_FALSE_OR_POP && !py68_vm_truth(value)) ||
                (opcode == OP_JUMP_IF_TRUE_OR_POP && py68_vm_truth(value))) {
                target = (Py68U32)((Py68I32)(ip + 3) +
                    (Py68I32)(Py68I16)(((Py68U16)current_code->bytecode[ip + 1] << 8) |
                                       current_code->bytecode[ip + 2]));
                if (target <= ip) status = py68_vm_poll(runtime);
                if (status == PY68_STATUS_OK) ip = target;
            } else {
                --runtime->value_stack_count;
                py68_value_release(runtime, value);
                ip += 3;
            }
            break;
        case OP_CALL: {
            Py68U8 argument_count = current_code->bytecode[ip + 1];
            Py68Value callee;
            Py68Value result;
            Py68Value *arguments;
            Py68U16 argument_index;
            if (runtime->value_stack_count < (Py68U16)argument_count + 1) {
                py68_vm_error(runtime, PY68_ERROR_BYTECODE,
                              "call stack underflow");
                status = PY68_STATUS_RUNTIME_ERROR;
                ip = current_code->bytecode_length;
                break;
            }
            arguments = &runtime->value_stack[
                runtime->value_stack_count - argument_count];
            callee = runtime->value_stack[
                runtime->value_stack_count - argument_count - 1];
            if (callee.type != PY68_VALUE_OBJECT || callee.as.object == NULL) {
                py68_vm_error(runtime, PY68_ERROR_TYPE, "callable required");
                status = PY68_STATUS_RUNTIME_ERROR;
                ip = current_code->bytecode_length;
                break;
            }
            /* next(gen) and next(gen, default) resume the generator inline:
               a native callback could not re-enter the interpreter without
               C recursion (D-0045). */
            if (callee.as.object->type == PY68_OBJECT_NATIVE_FUNCTION &&
                ((Py68NativeFunction *)callee.as.object)->callback ==
                    py68_builtin_next &&
                argument_count >= 1 && argument_count <= 2 &&
                arguments[0].type == PY68_VALUE_OBJECT &&
                arguments[0].as.object != NULL &&
                arguments[0].as.object->type == PY68_OBJECT_GENERATOR) {
                Py68Generator *generator =
                    (Py68Generator *)arguments[0].as.object;
                Py68U16 slot = (Py68U16)(runtime->value_stack_count -
                                         argument_count - 1);
                Py68Value placeholder = argument_count == 2 ? arguments[1]
                                                           : py68_value_none();
                py68_object_retain(&generator->base);
                py68_value_release(runtime, callee);
                py68_value_release(runtime, arguments[0]);
                runtime->value_stack[slot] = placeholder;
                runtime->value_stack_count = (Py68U16)(slot + 1);
                if (generator->state == PY68_GENERATOR_DONE) {
                    if (argument_count == 2) {
                        status = PY68_STATUS_OK;
                        ip += 2;
                    } else {
                        --runtime->value_stack_count;
                        py68_vm_error(runtime, PY68_ERROR_STOP_ITERATION,
                                      "StopIteration");
                        status = PY68_STATUS_RUNTIME_ERROR;
                    }
                    py68_object_release(runtime, &generator->base);
                    break;
                }
                status = py68_vm_resume_generator(
                    runtime, generator, current_code, ip + 2,
                    argument_count == 2 ? PY68_RESUME_NEXT_DEFAULT
                                        : PY68_RESUME_NEXT);
                if (status == PY68_STATUS_OK) {
                    current_code = generator->code;
                    ip = generator->resume_ip;
                }
                py68_object_release(runtime, &generator->base);
                break;
            }
            /* list(gen), sum(gen), ... : drain the generator into a list, then
               re-execute this OP_CALL with the list in its place (D-0045). */
            if (callee.as.object->type == PY68_OBJECT_NATIVE_FUNCTION &&
                ((Py68NativeFunction *)callee.as.object)->consumes_iterable ==
                    PY68_NATIVE_ITERABLE_ARG &&
                argument_count >= 1 &&
                arguments[0].type == PY68_VALUE_OBJECT &&
                arguments[0].as.object != NULL &&
                arguments[0].as.object->type == PY68_OBJECT_GENERATOR) {
                Py68Generator *generator =
                    (Py68Generator *)arguments[0].as.object;
                Py68U16 generator_slot = (Py68U16)(
                    runtime->value_stack_count - argument_count);
                Py68List *collected;
                status = py68_list_new(runtime, &collected);
                if (status != PY68_STATUS_OK) {
                    py68_vm_error(runtime, PY68_ERROR_MEMORY,
                                  "list allocation failed");
                    status = PY68_STATUS_RUNTIME_ERROR;
                    break;
                }
                status = py68_vm_push_owned(
                    runtime, py68_value_from_object(&collected->base));
                if (status != PY68_STATUS_OK) {
                    py68_vm_error(runtime, PY68_ERROR_MEMORY,
                                  "value stack growth failed");
                    status = PY68_STATUS_RUNTIME_ERROR;
                    break;
                }
                if (generator->state == PY68_GENERATOR_DONE) {
                    /* Already exhausted: hand over the empty list directly. */
                    Py68U16 list_slot =
                        (Py68U16)(runtime->value_stack_count - 1);
                    py68_value_release(runtime,
                                       runtime->value_stack[generator_slot]);
                    runtime->value_stack[generator_slot] =
                        runtime->value_stack[list_slot];
                    runtime->value_stack_count = list_slot;
                    status = PY68_STATUS_OK;
                    break;
                }
                status = py68_vm_resume_generator(runtime, generator,
                                                  current_code, ip,
                                                  PY68_RESUME_COLLECT);
                if (status == PY68_STATUS_OK) {
                    current_code = generator->code;
                    ip = generator->resume_ip;
                }
                break;
            }
            if (callee.as.object->type == PY68_OBJECT_NATIVE_FUNCTION) {
                status = py68_native_call(
                    (Py68NativeFunction *)callee.as.object, runtime,
                    argument_count, arguments, &result);
            } else if (callee.as.object->type == PY68_OBJECT_FUNCTION) {
                Py68Function *function = (Py68Function *)callee.as.object;
                Py68Code *callee_code = function->code;
                Py68U16 local_count = function->local_count;
                status = py68_function_check_arguments(function, argument_count);
                if (status != PY68_STATUS_OK)
                    py68_vm_error(runtime, PY68_ERROR_TYPE,
                                  "function called with the wrong "
                                  "number of arguments");
                if (status == PY68_STATUS_OK)
                    status = py68_verify_code(callee_code, &runtime->error);
                if (status == PY68_STATUS_OK && callee_code->is_generator != 0) {
                    /* Calling a generator factory builds the generator and
                       runs no bytecode (D-0045). */
                    Py68Generator *generator;
                    status = py68_generator_new(runtime, callee_code,
                                                function->globals_owner,
                                                argument_count, arguments,
                                                &generator);
                    if (status == PY68_STATUS_OK)
                        result = py68_value_from_object(&generator->base);
                    else
                        py68_vm_error(runtime, PY68_ERROR_MEMORY,
                                      "generator allocation failed");
                } else {
                    if (status == PY68_STATUS_OK)
                        status = py68_frame_push(runtime, callee_code,
                                                 current_code, local_count,
                                                 argument_count, arguments,
                                                 ip + 2,
                                                 function->globals_owner);
                    if (status == PY68_STATUS_OK) {
                        runtime->value_stack_count = (Py68U16)(
                            runtime->value_stack_count - argument_count - 1);
                        py68_value_release(runtime, callee);
                        while (argument_count != 0) {
                            --argument_count;
                            py68_value_release(runtime,
                                               arguments[argument_count]);
                        }
                        current_code = callee_code;
                        ip = 0;
                    }
                    break;
                }
            } else if (callee.as.object->type == PY68_OBJECT_BOUND_METHOD) {
                Py68BoundMethod *method =
                    (Py68BoundMethod *)callee.as.object;
                Py68Value bound_args[8];
                Py68U16 bound_count = (Py68U16)(argument_count + 1);
                Py68U16 copy_index;
                if (method->callable == NULL) {
                    py68_vm_error(runtime, PY68_ERROR_TYPE,
                                  "bound method has no callable");
                    status = PY68_STATUS_RUNTIME_ERROR;
                } else if (bound_count > 8) {
                    py68_vm_error(runtime, PY68_ERROR_TYPE,
                                  "too many bound-method arguments");
                    status = PY68_STATUS_RUNTIME_ERROR;
                } else if (method->callable->type ==
                           PY68_OBJECT_NATIVE_FUNCTION) {
                    bound_args[0] = method->self;
                    for (copy_index = 0; copy_index < argument_count;
                         ++copy_index)
                        bound_args[copy_index + 1] = arguments[copy_index];
                    status = py68_native_call(
                        (Py68NativeFunction *)method->callable, runtime,
                        bound_count, bound_args, &result);
                } else if (method->callable->type == PY68_OBJECT_FUNCTION) {
                    Py68Function *function =
                        (Py68Function *)method->callable;
                    Py68Code *callee_code = function->code;
                    bound_args[0] = method->self;
                    for (copy_index = 0; copy_index < argument_count;
                         ++copy_index)
                        bound_args[copy_index + 1] = arguments[copy_index];
                    status = py68_function_check_arguments(function,
                                                           bound_count);
                    if (status != PY68_STATUS_OK)
                        py68_vm_error(runtime, PY68_ERROR_TYPE,
                                      "function called with the wrong "
                                      "number of arguments");
                    if (status == PY68_STATUS_OK)
                        status = py68_verify_code(callee_code, &runtime->error);
                    if (status == PY68_STATUS_OK)
                        status = py68_frame_push(
                            runtime, callee_code, current_code,
                            function->local_count, bound_count, bound_args,
                            ip + 2, function->globals_owner);
                    if (status == PY68_STATUS_OK) {
                        runtime->value_stack_count = (Py68U16)(
                            runtime->value_stack_count - argument_count - 1);
                        py68_value_release(runtime, callee);
                        while (argument_count != 0) {
                            --argument_count;
                            py68_value_release(runtime,
                                               arguments[argument_count]);
                        }
                        current_code = callee_code;
                        ip = 0;
                    }
                    break;
                } else {
                    py68_vm_error(runtime, PY68_ERROR_TYPE,
                                  "unsupported bound method");
                    status = PY68_STATUS_RUNTIME_ERROR;
                }
            } else if (callee.as.object->type == PY68_OBJECT_TYPE) {
                Py68Type *type = (Py68Type *)callee.as.object;
                if (type == runtime->type_type) {
                    if (argument_count != 1) {
                        py68_vm_error(runtime, PY68_ERROR_TYPE,
                                      "type() takes exactly 1 argument");
                        status = PY68_STATUS_RUNTIME_ERROR;
                    } else {
                        status = py68_type_of_value(runtime, arguments[0],
                                                    &result);
                    }
                } else if ((type->flags & PY68_TYPE_FLAG_EXCEPTION) != 0) {
                    status = py68_type_call_as_exception(
                        runtime, type, argument_count, arguments, &result);
                } else {
                    Py68Instance *instance;
                    Py68Value init_value;
                    int found = 0;
                    status = py68_instance_new(runtime, type, &instance);
                    if (status != PY68_STATUS_OK) {
                        py68_vm_error(runtime, PY68_ERROR_MEMORY,
                                      "instance allocation failed");
                    } else {
                        status = py68_type_lookup(
                            runtime, type, (const Py68U8 *)"__init__", 8,
                            &init_value, &found);
                        if (status != PY68_STATUS_OK) {
                            py68_object_release(runtime, &instance->base);
                        } else if (!found) {
                            if (argument_count != 0) {
                                py68_object_release(runtime, &instance->base);
                                py68_vm_error(runtime, PY68_ERROR_TYPE,
                                              "object() takes no arguments");
                                status = PY68_STATUS_RUNTIME_ERROR;
                            } else {
                                result = py68_value_from_object(
                                    &instance->base);
                                status = PY68_STATUS_OK;
                            }
                        } else if (init_value.type == PY68_VALUE_OBJECT &&
                                   init_value.as.object != NULL &&
                                   init_value.as.object->type ==
                                       PY68_OBJECT_FUNCTION) {
                            Py68Function *function =
                                (Py68Function *)init_value.as.object;
                            Py68Code *callee_code = function->code;
                            Py68Value bound_args[8];
                            Py68U16 bound_count =
                                (Py68U16)(argument_count + 1);
                            Py68U16 copy_index;
                            if (bound_count > 8) {
                                py68_value_release(runtime, init_value);
                                py68_object_release(runtime, &instance->base);
                                py68_vm_error(runtime, PY68_ERROR_TYPE,
                                              "too many __init__ arguments");
                                status = PY68_STATUS_RUNTIME_ERROR;
                            } else {
                                bound_args[0] = py68_value_from_object(
                                    &instance->base);
                                for (copy_index = 0;
                                     copy_index < argument_count;
                                     ++copy_index)
                                    bound_args[copy_index + 1] =
                                        arguments[copy_index];
                                status = py68_function_check_arguments(
                                    function, bound_count);
                                if (status != PY68_STATUS_OK)
                                    py68_vm_error(
                                        runtime, PY68_ERROR_TYPE,
                                        "function called with the wrong "
                                        "number of arguments");
                                if (status == PY68_STATUS_OK)
                                    status = py68_verify_code(
                                        callee_code, &runtime->error);
                                if (status == PY68_STATUS_OK)
                                    status = py68_frame_push(
                                        runtime, callee_code, current_code,
                                        function->local_count, bound_count,
                                        bound_args, ip + 2,
                                        function->globals_owner);
                                if (status == PY68_STATUS_OK) {
                                    runtime->frames[runtime->frame_count - 1]
                                        .constructing =
                                        py68_value_from_object(
                                            &instance->base);
                                    py68_object_retain(&instance->base);
                                    runtime->value_stack_count = (Py68U16)(
                                        runtime->value_stack_count -
                                        argument_count - 1);
                                    py68_value_release(runtime, callee);
                                    py68_value_release(runtime, init_value);
                                    py68_object_release(runtime,
                                                        &instance->base);
                                    while (argument_count != 0) {
                                        --argument_count;
                                        py68_value_release(
                                            runtime,
                                            arguments[argument_count]);
                                    }
                                    current_code = callee_code;
                                    ip = 0;
                                    break;
                                }
                                py68_value_release(runtime, init_value);
                                py68_object_release(runtime, &instance->base);
                            }
                        } else {
                            py68_value_release(runtime, init_value);
                            py68_object_release(runtime, &instance->base);
                            py68_vm_error(runtime, PY68_ERROR_TYPE,
                                          "__init__ must be a function");
                            status = PY68_STATUS_RUNTIME_ERROR;
                        }
                    }
                }
            } else {
                py68_vm_error(runtime, PY68_ERROR_TYPE, "unsupported callable");
                status = PY68_STATUS_RUNTIME_ERROR;
            }
            if (status == PY68_STATUS_OK) {
                runtime->value_stack_count = (Py68U16)(
                    runtime->value_stack_count - argument_count - 1);
                py68_value_release(runtime, callee);
                for (argument_index = 0; argument_index < argument_count;
                     ++argument_index)
                    py68_value_release(runtime, arguments[argument_index]);
                status = py68_vm_push(runtime, result);
            }
            if (status != PY68_STATUS_OK) {
                ip = current_code->bytecode_length;
                break;
            }
            ip += 2;
            break;
        }
        case OP_YIELD_VALUE: {
            Py68Frame *frame;
            Py68Generator *generator;
            Py68Value yielded;
            Py68Code *caller_code;
            Py68U32 caller_ip;
            Py68U16 resume_kind;
            Py68U16 stack_base;
            Py68U16 saved_count;
            Py68U16 try_index;
            if (runtime->frame_count == 0 ||
                runtime->frames[runtime->frame_count - 1].generator == NULL) {
                py68_vm_error(runtime, PY68_ERROR_BYTECODE,
                              "yield outside a generator frame");
                status = PY68_STATUS_RUNTIME_ERROR;
                break;
            }
            frame = &runtime->frames[runtime->frame_count - 1];
            generator = frame->generator;
            stack_base = frame->stack_base;
            resume_kind = frame->resume_kind;
            caller_code = frame->return_code;
            caller_ip = frame->return_ip;
            status = py68_vm_pop(runtime, &yielded);
            if (status != PY68_STATUS_OK) break;
            if (resume_kind == PY68_RESUME_COLLECT) {
                /* Appending keeps this activation running, so no state is
                   saved and no frame is popped. */
                Py68Value target = stack_base == 0
                    ? py68_value_none()
                    : runtime->value_stack[stack_base - 1];
                if (target.type != PY68_VALUE_OBJECT ||
                    target.as.object == NULL ||
                    target.as.object->type != PY68_OBJECT_LIST) {
                    py68_value_release(runtime, yielded);
                    py68_vm_error(runtime, PY68_ERROR_BYTECODE,
                                  "generator collection target is not a list");
                    status = PY68_STATUS_RUNTIME_ERROR;
                    break;
                }
                status = py68_list_append_copy(
                    runtime, (Py68List *)target.as.object, yielded);
                py68_value_release(runtime, yielded);
                if (status != PY68_STATUS_OK) {
                    py68_vm_error(runtime, PY68_ERROR_MEMORY,
                                  "list append failed");
                    status = PY68_STATUS_RUNTIME_ERROR;
                    break;
                }
                ++ip;
                break;
            }
            if (runtime->value_stack_count < stack_base ||
                (resume_kind != PY68_RESUME_FOR && stack_base == 0)) {
                py68_value_release(runtime, yielded);
                py68_vm_error(runtime, PY68_ERROR_BYTECODE,
                              "generator stack is inconsistent");
                status = PY68_STATUS_RUNTIME_ERROR;
                break;
            }
            saved_count = (Py68U16)(runtime->value_stack_count - stack_base);
            status = py68_generator_store_stack(
                generator, &runtime->value_stack[stack_base], saved_count);
            if (status != PY68_STATUS_OK) {
                py68_value_release(runtime, yielded);
                py68_vm_error(runtime, PY68_ERROR_BYTECODE,
                              "generator operand stack overflow");
                status = PY68_STATUS_RUNTIME_ERROR;
                break;
            }
            runtime->value_stack_count = stack_base;
            generator->resume_ip = ip + 1;
            generator->state = PY68_GENERATOR_SUSPENDED;
            generator->try_count = frame->try_count;
            for (try_index = 0; try_index < frame->try_count; ++try_index) {
                generator->try_stack[try_index].handler_ip =
                    frame->try_stack[try_index].handler_ip;
                generator->try_stack[try_index].stack_depth = (Py68U16)(
                    frame->try_stack[try_index].stack_depth - stack_base);
            }
            py68_frame_pop_suspend(runtime);
            current_code = caller_code;
            if (resume_kind == PY68_RESUME_FOR) {
                /* Leaves [.., generator, yielded] for the loop body; the
                   slot is already inside the stack capacity. */
                ip = caller_ip + 3;
                status = py68_vm_push(runtime, yielded);
            } else {
                ip = caller_ip;
                py68_value_release(runtime,
                                   runtime->value_stack[stack_base - 1]);
                runtime->value_stack[stack_base - 1] = yielded;
                status = PY68_STATUS_OK;
            }
            break;
        }
        case OP_RETURN_VALUE: case OP_RETURN_NONE: {
            Py68Value return_value;
            Py68Code *caller_code;
            Py68U32 caller_ip;
            if (runtime->frame_count <= 1) {
                py68_vm_error(runtime, PY68_ERROR_BYTECODE,
                              "return outside call frame");
                status = PY68_STATUS_RUNTIME_ERROR;
                break;
            }
            if (opcode == OP_RETURN_VALUE) {
                status = py68_vm_pop(runtime, &return_value);
                if (status != PY68_STATUS_OK) break;
            } else {
                return_value = py68_value_none();
            }
            if (runtime->frames[runtime->frame_count - 1].generator != NULL) {
                /* return finishes the generator; the value is discarded
                   because StopIteration.value does not exist here (D-0045). */
                Py68Frame *frame = &runtime->frames[runtime->frame_count - 1];
                Py68U16 resume_kind = frame->resume_kind;
                Py68U16 stack_base = frame->stack_base;
                caller_code = frame->return_code;
                caller_ip = frame->return_ip;
                py68_value_release(runtime, return_value);
                while (runtime->value_stack_count > stack_base) {
                    --runtime->value_stack_count;
                    py68_value_release(
                        runtime,
                        runtime->value_stack[runtime->value_stack_count]);
                }
                py68_frame_pop(runtime);
                current_code = caller_code;
                ip = caller_ip;
                status = PY68_STATUS_OK;
                if (resume_kind == PY68_RESUME_NEXT) {
                    /* Drop the None placeholder and report exhaustion. */
                    if (runtime->value_stack_count != 0) {
                        --runtime->value_stack_count;
                        py68_value_release(
                            runtime,
                            runtime->value_stack[runtime->value_stack_count]);
                    }
                    py68_vm_error(runtime, PY68_ERROR_STOP_ITERATION,
                                  "StopIteration");
                    status = PY68_STATUS_RUNTIME_ERROR;
                } else if (resume_kind == PY68_RESUME_COLLECT) {
                    /* Replace the drained generator argument with the collected
                       list and re-execute the call. */
                    Py68U16 list_slot = (Py68U16)(stack_base - 1);
                    Py68U8 collect_argc;
                    Py68U16 generator_slot;
                    if (stack_base == 0 || caller_code == NULL ||
                        caller_ip + 1 >= caller_code->bytecode_length ||
                        caller_code->bytecode[caller_ip] != OP_CALL) {
                        py68_vm_error(runtime, PY68_ERROR_BYTECODE,
                                      "generator collection site is invalid");
                        status = PY68_STATUS_RUNTIME_ERROR;
                        break;
                    }
                    collect_argc = caller_code->bytecode[caller_ip + 1];
                    if (collect_argc == 0 || list_slot < collect_argc ||
                        list_slot - collect_argc == 0) {
                        py68_vm_error(runtime, PY68_ERROR_BYTECODE,
                                      "generator collection site is invalid");
                        status = PY68_STATUS_RUNTIME_ERROR;
                        break;
                    }
                    generator_slot = (Py68U16)(list_slot - collect_argc);
                    py68_value_release(runtime,
                                       runtime->value_stack[generator_slot]);
                    runtime->value_stack[generator_slot] =
                        runtime->value_stack[list_slot];
                    runtime->value_stack_count = list_slot;
                }
                break;
            }
            caller_code = runtime->frames[runtime->frame_count - 1].return_code;
            caller_ip = runtime->frames[runtime->frame_count - 1].return_ip;
            if (runtime->frames[runtime->frame_count - 1].constructing.type ==
                PY68_VALUE_OBJECT) {
                Py68Value constructed =
                    runtime->frames[runtime->frame_count - 1].constructing;
                runtime->frames[runtime->frame_count - 1].constructing =
                    py68_value_none();
                if (return_value.type != PY68_VALUE_NONE) {
                    py68_value_release(runtime, return_value);
                    py68_value_release(runtime, constructed);
                    py68_frame_pop(runtime);
                    py68_vm_error(runtime, PY68_ERROR_TYPE,
                                  "__init__() should return None");
                    status = PY68_STATUS_RUNTIME_ERROR;
                    break;
                }
                py68_value_release(runtime, return_value);
                return_value = constructed;
            }
            py68_frame_pop(runtime);
            current_code = caller_code;
            ip = caller_ip;
            status = py68_vm_push(runtime, return_value);
            break;
        }
        case OP_ROT_TWO:
            if (runtime->value_stack_count < 2) {
                py68_vm_error(runtime, PY68_ERROR_BYTECODE, "value stack underflow");
                status = PY68_STATUS_RUNTIME_ERROR;
            } else {
                Py68Value top = runtime->value_stack[runtime->value_stack_count - 1];
                runtime->value_stack[runtime->value_stack_count - 1] =
                    runtime->value_stack[runtime->value_stack_count - 2];
                runtime->value_stack[runtime->value_stack_count - 2] = top;
                status = PY68_STATUS_OK;
            }
            ip += 1;
            break;
        case OP_BUILD_TUPLE: {
            Py68U16 count = (Py68U16)(((Py68U16)current_code->bytecode[ip + 1] << 8) |
                                      current_code->bytecode[ip + 2]);
            Py68Tuple *tuple;
            if (runtime->value_stack_count < count) {
                py68_vm_error(runtime, PY68_ERROR_BYTECODE, "value stack underflow");
                status = PY68_STATUS_RUNTIME_ERROR;
                break;
            }
            status = py68_tuple_from_values(
                runtime, &runtime->value_stack[runtime->value_stack_count - count],
                count, &tuple);
            if (status == PY68_STATUS_OK) {
                while (count != 0) {
                    --count;
                    py68_value_release(runtime, runtime->value_stack[
                        runtime->value_stack_count - 1]);
                    --runtime->value_stack_count;
                }
                status = py68_vm_push(runtime, py68_value_from_object(&tuple->base));
            }
            ip += 3;
            break;
        }
        case OP_BUILD_SET: {
            Py68U16 count = (Py68U16)(((Py68U16)current_code->bytecode[ip + 1] << 8) |
                                      current_code->bytecode[ip + 2]);
            Py68Set *set;
            Py68U16 item;
            if (runtime->value_stack_count < count ||
                py68_set_new(runtime, &set) != PY68_STATUS_OK) {
                py68_vm_error(runtime, PY68_ERROR_MEMORY, "set construction failed");
                status = PY68_STATUS_RUNTIME_ERROR;
                break;
            }
            for (item = 0; item < count; ++item) {
                Py68Value element = runtime->value_stack[
                    runtime->value_stack_count - count + item];
                status = py68_set_add(runtime, set, element);
                if (status != PY68_STATUS_OK) break;
            }
            if (status == PY68_STATUS_OK) {
                while (count != 0) {
                    --count;
                    py68_value_release(runtime, runtime->value_stack[
                        runtime->value_stack_count - 1]);
                    --runtime->value_stack_count;
                }
                status = py68_vm_push(runtime, py68_value_from_object(&set->base));
            } else {
                py68_object_release(runtime, &set->base);
                py68_vm_error(runtime, PY68_ERROR_TYPE, "unhashable set item");
                status = PY68_STATUS_RUNTIME_ERROR;
            }
            ip += 3;
            break;
        }
        case OP_BUILD_DICT: {
            Py68U16 count = (Py68U16)(((Py68U16)current_code->bytecode[ip + 1] << 8) |
                                      current_code->bytecode[ip + 2]);
            Py68Dict *dict;
            Py68U16 item;
            Py68U16 consumed = (Py68U16)(count * 2);
            if (runtime->value_stack_count < consumed ||
                py68_dict_new(runtime, &dict) != PY68_STATUS_OK) {
                py68_vm_error(runtime, PY68_ERROR_MEMORY, "dict construction failed");
                status = PY68_STATUS_RUNTIME_ERROR;
                break;
            }
            for (item = 0; item < count; ++item) {
                Py68Value key = runtime->value_stack[
                    runtime->value_stack_count - consumed + item * 2];
                Py68Value item_value = runtime->value_stack[
                    runtime->value_stack_count - consumed + item * 2 + 1];
                status = py68_dict_set_copy(runtime, dict, key, item_value);
                if (status != PY68_STATUS_OK) break;
            }
            if (status == PY68_STATUS_OK) {
                while (consumed != 0) {
                    --consumed;
                    py68_value_release(runtime, runtime->value_stack[
                        runtime->value_stack_count - 1]);
                    --runtime->value_stack_count;
                }
                status = py68_vm_push(runtime, py68_value_from_object(&dict->base));
            } else {
                py68_object_release(runtime, &dict->base);
                py68_vm_error(runtime, PY68_ERROR_TYPE, "invalid dict key");
                status = PY68_STATUS_RUNTIME_ERROR;
            }
            ip += 3;
            break;
        }
        case OP_LIST_APPEND:
        case OP_SET_ADD: {
            Py68U8 depth_op = current_code->bytecode[ip + 1];
            Py68Value element;
            Py68Value container;
            if (runtime->value_stack_count <= depth_op) {
                py68_vm_error(runtime, PY68_ERROR_BYTECODE,
                              "value stack underflow");
                status = PY68_STATUS_RUNTIME_ERROR;
                ip = current_code->bytecode_length;
                break;
            }
            container = runtime->value_stack[
                runtime->value_stack_count - 1 - depth_op];
            status = py68_vm_pop(runtime, &element);
            if (status != PY68_STATUS_OK) break;
            if (opcode == OP_LIST_APPEND) {
                if (container.type != PY68_VALUE_OBJECT ||
                    container.as.object == NULL ||
                    container.as.object->type != PY68_OBJECT_LIST) {
                    py68_value_release(runtime, element);
                    py68_vm_error(runtime, PY68_ERROR_TYPE,
                                  "list append requires a list");
                    status = PY68_STATUS_RUNTIME_ERROR;
                    ip = current_code->bytecode_length;
                    break;
                }
                status = py68_list_append_copy(
                    runtime, (Py68List *)container.as.object, element);
                py68_value_release(runtime, element);
                if (status == PY68_STATUS_MEMORY_ERROR) {
                    py68_vm_error(runtime, PY68_ERROR_MEMORY,
                                  "list append failed");
                    status = PY68_STATUS_RUNTIME_ERROR;
                    ip = current_code->bytecode_length;
                    break;
                }
                if (status != PY68_STATUS_OK) {
                    py68_vm_error(runtime, PY68_ERROR_VALUE,
                                  "cyclic containers are not supported");
                    status = PY68_STATUS_RUNTIME_ERROR;
                    ip = current_code->bytecode_length;
                    break;
                }
            } else {
                if (container.type != PY68_VALUE_OBJECT ||
                    container.as.object == NULL ||
                    container.as.object->type != PY68_OBJECT_SET) {
                    py68_value_release(runtime, element);
                    py68_vm_error(runtime, PY68_ERROR_TYPE,
                                  "set add requires a set");
                    status = PY68_STATUS_RUNTIME_ERROR;
                    ip = current_code->bytecode_length;
                    break;
                }
                status = py68_set_add(runtime, (Py68Set *)container.as.object,
                                      element);
                py68_value_release(runtime, element);
                if (status != PY68_STATUS_OK) {
                    py68_vm_error(runtime, PY68_ERROR_TYPE,
                                  "unhashable set item");
                    status = PY68_STATUS_RUNTIME_ERROR;
                    ip = current_code->bytecode_length;
                    break;
                }
            }
            ip += 2;
            break;
        }
        case OP_MAP_ADD: {
            Py68U8 depth_op = current_code->bytecode[ip + 1];
            Py68Value key_val;
            Py68Value value_val;
            Py68Value container;
            if (runtime->value_stack_count <= depth_op) {
                py68_vm_error(runtime, PY68_ERROR_BYTECODE,
                              "value stack underflow");
                status = PY68_STATUS_RUNTIME_ERROR;
                ip = current_code->bytecode_length;
                break;
            }
            container = runtime->value_stack[
                runtime->value_stack_count - 1 - depth_op];
            status = py68_vm_pop(runtime, &value_val);
            if (status == PY68_STATUS_OK)
                status = py68_vm_pop(runtime, &key_val);
            if (status != PY68_STATUS_OK) break;
            if (container.type != PY68_VALUE_OBJECT ||
                container.as.object == NULL ||
                container.as.object->type != PY68_OBJECT_DICT) {
                py68_value_release(runtime, value_val);
                py68_value_release(runtime, key_val);
                py68_vm_error(runtime, PY68_ERROR_TYPE,
                              "dict store requires a dict");
                status = PY68_STATUS_RUNTIME_ERROR;
                ip = current_code->bytecode_length;
                break;
            }
            status = py68_dict_set_copy(runtime,
                                        (Py68Dict *)container.as.object,
                                        key_val, value_val);
            py68_value_release(runtime, value_val);
            py68_value_release(runtime, key_val);
            if (status != PY68_STATUS_OK) {
                if (status == PY68_STATUS_RUNTIME_ERROR)
                    py68_vm_error(runtime, PY68_ERROR_VALUE,
                                  "cyclic containers are not supported");
                else
                    py68_vm_error(runtime, PY68_ERROR_TYPE,
                                  "unhashable or cyclic dict key");
                status = PY68_STATUS_RUNTIME_ERROR;
                ip = current_code->bytecode_length;
                break;
            }
            ip += 2;
            break;
        }
        case OP_LOAD_ATTR: {
            const Py68U8 *name_bytes;
            Py68U16 name_length;
            index = (Py68U16)(((Py68U16)current_code->bytecode[ip + 1] << 8) |
                              current_code->bytecode[ip + 2]);
            name_bytes = py68_code_name_bytes(current_code, index);
            name_length = current_code->name_lengths[index];
            status = py68_vm_pop(runtime, &value);
            if (status == PY68_STATUS_OK) {
                Py68Value attr;
                status = py68_attr_load(runtime, value, name_bytes, name_length,
                                        &attr);
                py68_value_release(runtime, value);
                if (status == PY68_STATUS_OK)
                    status = py68_vm_push(runtime, attr);
            }
            ip += 3;
            break;
        }
        case OP_STORE_ATTR: {
            const Py68U8 *name_bytes;
            Py68U16 name_length;
            Py68Value object;
            index = (Py68U16)(((Py68U16)current_code->bytecode[ip + 1] << 8) |
                              current_code->bytecode[ip + 2]);
            name_bytes = py68_code_name_bytes(current_code, index);
            name_length = current_code->name_lengths[index];
            status = py68_vm_pop(runtime, &value);
            if (status == PY68_STATUS_OK)
                status = py68_vm_pop(runtime, &object);
            if (status == PY68_STATUS_OK) {
                status = py68_attr_store(runtime, object, name_bytes,
                                         name_length, value);
                py68_value_release(runtime, value);
                py68_value_release(runtime, object);
            }
            ip += 3;
            break;
        }
        case OP_SETUP_TRY: {
            Py68Frame *frame;
            Py68I16 displacement;
            if (runtime->frame_count == 0) {
                py68_vm_error(runtime, PY68_ERROR_BYTECODE, "try without frame");
                status = PY68_STATUS_RUNTIME_ERROR;
                break;
            }
            frame = &runtime->frames[runtime->frame_count - 1];
            if (frame->try_count >= PY68_TRY_MAX) {
                py68_vm_error(runtime, PY68_ERROR_VALUE, "too many nested try blocks");
                status = PY68_STATUS_RUNTIME_ERROR;
                break;
            }
            displacement = (Py68I16)(((Py68U16)current_code->bytecode[ip + 1] << 8) |
                                     current_code->bytecode[ip + 2]);
            frame->try_stack[frame->try_count].handler_ip =
                (Py68U32)((Py68I32)(ip + 3) + (Py68I32)displacement);
            frame->try_stack[frame->try_count].stack_depth =
                runtime->value_stack_count;
            ++frame->try_count;
            ip += 3;
            status = PY68_STATUS_OK;
            break;
        }
        case OP_POP_TRY: {
            Py68Frame *frame;
            if (runtime->frame_count == 0 ||
                runtime->frames[runtime->frame_count - 1].try_count == 0) {
                py68_vm_error(runtime, PY68_ERROR_BYTECODE, "try stack underflow");
                status = PY68_STATUS_RUNTIME_ERROR;
                break;
            }
            frame = &runtime->frames[runtime->frame_count - 1];
            --frame->try_count;
            ip += 1;
            status = PY68_STATUS_OK;
            break;
        }
        case OP_RAISE: {
            Py68Value raised;
            Py68Exception *exception = NULL;
            status = py68_vm_pop(runtime, &raised);
            if (status != PY68_STATUS_OK) break;
            if (raised.type == PY68_VALUE_NONE) {
                py68_value_release(runtime, raised);
                if (runtime->current_exception.type != PY68_VALUE_OBJECT ||
                    runtime->current_exception.as.object == NULL ||
                    runtime->current_exception.as.object->type !=
                        PY68_OBJECT_EXCEPTION) {
                    py68_vm_error(runtime, PY68_ERROR_TYPE, "no active exception");
                    status = PY68_STATUS_RUNTIME_ERROR;
                    break;
                }
                exception = (Py68Exception *)runtime->current_exception.as.object;
                py68_object_retain(&exception->base);
            } else if (raised.type == PY68_VALUE_OBJECT &&
                       raised.as.object != NULL &&
                       raised.as.object->type == PY68_OBJECT_TYPE &&
                       (((Py68Type *)raised.as.object)->flags &
                        PY68_TYPE_FLAG_EXCEPTION) != 0) {
                Py68Type *type = (Py68Type *)raised.as.object;
                Py68Value constructed;
                status = py68_type_call_as_exception(runtime, type, 0, NULL,
                                                     &constructed);
                py68_value_release(runtime, raised);
                if (status != PY68_STATUS_OK) break;
                raised = constructed;
                exception = (Py68Exception *)raised.as.object;
            } else if (raised.type == PY68_VALUE_OBJECT &&
                       raised.as.object != NULL &&
                       raised.as.object->type == PY68_OBJECT_NATIVE_FUNCTION &&
                       raised.as.object->flags != 0) {
                status = py68_exception_new(
                    runtime, raised.as.object->flags,
                    py68_error_kind_name(raised.as.object->flags), &exception);
                py68_value_release(runtime, raised);
                if (status != PY68_STATUS_OK) break;
            } else if (raised.type == PY68_VALUE_OBJECT &&
                       raised.as.object != NULL &&
                       raised.as.object->type == PY68_OBJECT_EXCEPTION) {
                exception = (Py68Exception *)raised.as.object;
            } else {
                py68_value_release(runtime, raised);
                py68_vm_error(runtime, PY68_ERROR_TYPE,
                              "exceptions must be exception objects");
                status = PY68_STATUS_RUNTIME_ERROR;
                break;
            }
            py68_value_release(runtime, runtime->current_exception);
            runtime->current_exception =
                py68_value_from_object(&exception->base);
            py68_vm_error(runtime, (Py68ErrorKind)exception->kind,
                          exception->message);
            status = PY68_STATUS_RUNTIME_ERROR;
            break;
        }
        case OP_CHECK_EXCEPT: {
            Py68Value matcher;
            Py68Value pending;
            Py68I16 displacement;
            status = py68_vm_pop(runtime, &matcher);
            if (status != PY68_STATUS_OK) break;
            if (runtime->value_stack_count == 0) {
                py68_value_release(runtime, matcher);
                py68_vm_error(runtime, PY68_ERROR_BYTECODE, "value stack underflow");
                status = PY68_STATUS_RUNTIME_ERROR;
                break;
            }
            pending = runtime->value_stack[runtime->value_stack_count - 1];
            displacement = (Py68I16)(((Py68U16)current_code->bytecode[ip + 1] << 8) |
                                     current_code->bytecode[ip + 2]);
            if (pending.type == PY68_VALUE_OBJECT && pending.as.object != NULL &&
                pending.as.object->type == PY68_OBJECT_EXCEPTION &&
                py68_exception_matches(runtime,
                                       (Py68Exception *)pending.as.object,
                                       matcher)) {
                ip += 3;
            } else {
                ip = (Py68U32)((Py68I32)(ip + 3) + (Py68I32)displacement);
            }
            py68_value_release(runtime, matcher);
            status = PY68_STATUS_OK;
            break;
        }
        case OP_IMPORT_NAME: {
            const Py68U8 *name_bytes;
            Py68U16 name_length;
            index = (Py68U16)(((Py68U16)current_code->bytecode[ip + 1] << 8) |
                              current_code->bytecode[ip + 2]);
            name_bytes = py68_code_name_bytes(current_code, index);
            name_length = current_code->name_lengths[index];
            status = py68_import_name(runtime, name_bytes, name_length, &value);
            if (status == PY68_STATUS_OK)
                status = py68_vm_push(runtime, value);
            ip += 3;
            break;
        }
        case OP_IMPORT_FROM: {
            const Py68U8 *name_bytes;
            Py68U16 name_length;
            Py68Value module_value;
            index = (Py68U16)(((Py68U16)current_code->bytecode[ip + 1] << 8) |
                              current_code->bytecode[ip + 2]);
            name_bytes = py68_code_name_bytes(current_code, index);
            name_length = current_code->name_lengths[index];
            if (runtime->value_stack_count == 0) {
                py68_vm_error(runtime, PY68_ERROR_BYTECODE, "value stack underflow");
                status = PY68_STATUS_RUNTIME_ERROR;
                break;
            }
            module_value = runtime->value_stack[runtime->value_stack_count - 1];
            status = py68_attr_load(runtime, module_value, name_bytes,
                                    name_length, &value);
            if (status == PY68_STATUS_OK)
                status = py68_vm_push(runtime, value);
            ip += 3;
            break;
        }
        default:
            py68_vm_error(runtime, PY68_ERROR_BYTECODE, "opcode not implemented");
            status = PY68_STATUS_RUNTIME_ERROR; ip = current_code->bytecode_length; break;
        }
        if (status != PY68_STATUS_OK) {
            if (status == PY68_STATUS_EXIT) {
                if (top_level) {
                    py68_vm_clear_stack(runtime);
                    py68_frame_unwind(runtime);
                }
                return status;
            }
            if (py68_vm_catch(runtime, &current_code, &ip)) {
                status = PY68_STATUS_OK;
                continue;
            }
            if (!top_level) {
                while (runtime->frame_count > entry_frames)
                    py68_frame_pop(runtime);
                while (runtime->value_stack_count > entry_stack) {
                    --runtime->value_stack_count;
                    py68_value_release(
                        runtime,
                        runtime->value_stack[runtime->value_stack_count]);
                }
                return status;
            }
            py68_vm_fail(runtime, current_code, ip);
            return status;
        }
    }
    py68_vm_clear_stack(runtime);
    py68_frame_unwind(runtime);
    py68_vm_error(runtime, PY68_ERROR_BYTECODE, "VM fell off bytecode");
    return PY68_STATUS_RUNTIME_ERROR;
}
