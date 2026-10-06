/* 2026 by Piotr Rozentreter (Rozsoft) */

#include "py68k_frame.h"
#include "py68k_generator.h"

#include <stddef.h>

static Py68Status py68_frame_grow(Py68Runtime *runtime)
{
    Py68U16 capacity = runtime->frame_capacity == 0 ? 8 :
                       (Py68U16)(runtime->frame_capacity * 2);
    Py68Frame *frames;
    if (capacity < runtime->frame_capacity) return PY68_STATUS_MEMORY_ERROR;
    frames = (Py68Frame *)py68_realloc(
        &runtime->allocator, PY68_MEM_STACK, runtime->frames,
        (Py68U32)runtime->frame_capacity * sizeof(Py68Frame),
        (Py68U32)capacity * sizeof(Py68Frame));
    if (frames == NULL) return PY68_STATUS_MEMORY_ERROR;
    runtime->frames = frames;
    runtime->frame_capacity = capacity;
    return PY68_STATUS_OK;
}

static Py68Status py68_frame_reserve(Py68Runtime *runtime)
{
    Py68Location location;
    if (runtime->recursion_limit != 0 &&
        runtime->frame_count >= runtime->recursion_limit) {
        location.offset = 0;
        location.line = 0;
        location.column = 0;
        location.length = 0;
        py68_error_set(&runtime->error, PY68_ERROR_RECURSION,
                       location, NULL,
                       "maximum recursion depth exceeded");
        return PY68_STATUS_RUNTIME_ERROR;
    }
    if (runtime->frame_count == runtime->frame_capacity)
        return py68_frame_grow(runtime);
    return PY68_STATUS_OK;
}

Py68Status py68_frame_push(Py68Runtime *runtime, Py68Code *code,
                           Py68Code *return_code, Py68U16 local_count,
                           Py68U16 argument_count, Py68Value *arguments,
                           Py68U32 return_ip,
                           struct Py68Module *globals_owner)
{
    Py68Frame *frame;
    Py68U16 index;
    Py68Status status;
    if (argument_count > local_count) return PY68_STATUS_RUNTIME_ERROR;
    status = py68_frame_reserve(runtime);
    if (status != PY68_STATUS_OK) return status;
    frame = &runtime->frames[runtime->frame_count];
    frame->locals = (Py68Value *)py68_alloc(
        &runtime->allocator, PY68_MEM_STACK,
        (Py68U32)local_count * sizeof(Py68Value));
    if (frame->locals == NULL && local_count != 0)
        return PY68_STATUS_MEMORY_ERROR;
    frame->local_count = local_count;
    frame->code = code;
    frame->return_code = return_code;
    frame->globals_owner = globals_owner;
    frame->argument_count = argument_count;
    frame->return_ip = return_ip;
    frame->try_count = 0;
    frame->generator = NULL;
    frame->stack_base = 0;
    frame->resume_kind = PY68_RESUME_NONE;
    frame->constructing = py68_value_none();
    for (index = 0; index < local_count; ++index)
        frame->locals[index] = py68_value_unbound();
    for (index = 0; index < argument_count; ++index) {
        frame->locals[index] = arguments[index];
        py68_value_retain(frame->locals[index]);
    }
    ++runtime->frame_count;
    return PY68_STATUS_OK;
}

Py68Status py68_frame_push_generator(Py68Runtime *runtime,
                                     struct Py68Generator *generator,
                                     Py68Code *return_code, Py68U32 return_ip,
                                     Py68U16 stack_base, Py68U16 resume_kind)
{
    Py68Frame *frame;
    Py68U16 index;
    Py68Status status;
    if (generator == NULL) return PY68_STATUS_INTERNAL_ERROR;
    status = py68_frame_reserve(runtime);
    if (status != PY68_STATUS_OK) return status;
    frame = &runtime->frames[runtime->frame_count];
    frame->code = generator->code;
    frame->return_code = return_code;
    frame->locals = generator->locals;
    frame->local_count = generator->local_count;
    frame->globals_owner = generator->globals_owner;
    frame->argument_count = generator->code->argument_count;
    frame->return_ip = return_ip;
    frame->generator = generator;
    frame->stack_base = stack_base;
    frame->resume_kind = resume_kind;
    frame->try_count = generator->try_count;
    frame->constructing = py68_value_none();
    for (index = 0; index < generator->try_count; ++index) {
        frame->try_stack[index].handler_ip =
            generator->try_stack[index].handler_ip;
        frame->try_stack[index].stack_depth =
            (Py68U16)(generator->try_stack[index].stack_depth + stack_base);
    }
    py68_object_retain(&generator->base);
    ++runtime->frame_count;
    return PY68_STATUS_OK;
}

void py68_frame_pop(Py68Runtime *runtime)
{
    Py68Frame *frame;
    Py68Generator *generator;
    Py68Value *locals;
    Py68U16 local_count;
    Py68U16 index;
    if (runtime->frame_count == 0) return;
    frame = &runtime->frames[runtime->frame_count - 1];
    generator = frame->generator;
    locals = frame->locals;
    local_count = frame->local_count;
    frame->locals = NULL;
    frame->local_count = 0;
    frame->generator = NULL;
    py68_value_release(runtime, frame->constructing);
    frame->constructing = py68_value_none();
    --runtime->frame_count;
    if (generator != NULL) {
        /* The locals belong to the generator, which owns their lifetime. */
        py68_generator_finish(runtime, generator);
        py68_object_release(runtime, &generator->base);
        return;
    }
    for (index = 0; index < local_count; ++index)
        py68_value_release(runtime, locals[index]);
    py68_free(&runtime->allocator, PY68_MEM_STACK, locals,
              (Py68U32)local_count * sizeof(Py68Value));
}

void py68_frame_pop_suspend(Py68Runtime *runtime)
{
    Py68Frame *frame;
    Py68Generator *generator;
    if (runtime->frame_count == 0) return;
    frame = &runtime->frames[runtime->frame_count - 1];
    generator = frame->generator;
    if (generator == NULL) {
        py68_frame_pop(runtime);
        return;
    }
    frame->locals = NULL;
    frame->local_count = 0;
    frame->generator = NULL;
    --runtime->frame_count;
    py68_object_release(runtime, &generator->base);
}

void py68_frame_unwind(Py68Runtime *runtime)
{
    while (runtime->frame_count != 0) py68_frame_pop(runtime);
}

Py68Status py68_frame_get_local(Py68Runtime *runtime, Py68U16 slot,
                                Py68Value *result)
{
    Py68Frame *frame;
    if (runtime->frame_count == 0) return PY68_STATUS_RUNTIME_ERROR;
    frame = &runtime->frames[runtime->frame_count - 1];
    if (slot >= frame->local_count) return PY68_STATUS_RUNTIME_ERROR;
    *result = frame->locals[slot];
    py68_value_retain(*result);
    return PY68_STATUS_OK;
}

Py68Status py68_frame_set_local_copy(Py68Runtime *runtime, Py68U16 slot,
                                     Py68Value value)
{
    Py68Frame *frame;
    Py68Value old;
    if (runtime->frame_count == 0) return PY68_STATUS_RUNTIME_ERROR;
    frame = &runtime->frames[runtime->frame_count - 1];
    if (slot >= frame->local_count) return PY68_STATUS_RUNTIME_ERROR;
    py68_value_retain(value);
    old = frame->locals[slot];
    frame->locals[slot] = value;
    py68_value_release(runtime, old);
    return PY68_STATUS_OK;
}