/* 2026 by Piotr Rozentreter (Rozsoft) */

#include "py68k_object.h"
#include "py68k_runtime.h"
#include "py68k_list.h"
#include "py68k_string.h"
#include "py68k_function.h"
#include "py68k_native.h"
#include "py68k_range.h"
#include "py68k_file.h"
#include "py68k_tuple.h"
#include "py68k_dict.h"
#include "py68k_set.h"
#include "py68k_attr.h"
#include "py68k_exception.h"
#include "py68k_generator.h"
#include "py68k_module.h"
#include "py68k_time.h"
#include "py68k_type.h"
#include "py68k_native.h"
#include "py68k_platform.h"

#include <stddef.h>

void py68_object_retain(Py68Object *object)
{
    if (object == NULL) return;
    if (object->reference_count != (Py68U32)~(Py68U32)0) {
        ++object->reference_count;
    }
}

static void py68_object_unlink(Py68Runtime *runtime, Py68Object *object)
{
    Py68Object **cursor = &runtime->live_objects;
    while (*cursor != NULL) {
        if (*cursor == object) {
            *cursor = object->next_object;
            return;
        }
        cursor = &(*cursor)->next_object;
    }
}

void py68_object_release(Py68Runtime *runtime, Py68Object *object)
{
    if (object == NULL || object->reference_count == 0) return;
    --object->reference_count;
    if (object->reference_count != 0) return;
    py68_object_unlink(runtime, object);
    if (object->type == PY68_OBJECT_STRING) {
        Py68String *string = (Py68String *)object;
        py68_free(&runtime->allocator, PY68_MEM_STRING, string,
                  (Py68U32)sizeof(Py68String) + string->length);
    } else if (object->type == PY68_OBJECT_LIST) {
        Py68List *list = (Py68List *)object;
        Py68U32 index;
        for (index = 0; index < list->count; ++index)
            py68_value_release(runtime, list->items[index]);
        py68_free(&runtime->allocator, PY68_MEM_LIST, list->items,
                  list->capacity * sizeof(Py68Value));
        py68_free(&runtime->allocator, PY68_MEM_LIST, list,
                  sizeof(Py68List));
    } else if (object->type == PY68_OBJECT_FUNCTION) {
        Py68Function *function = (Py68Function *)object;
        py68_free(&runtime->allocator, PY68_MEM_FUNCTION, function,
                  sizeof(Py68Function));
    } else if (object->type == PY68_OBJECT_NATIVE_FUNCTION) {
        Py68NativeFunction *function = (Py68NativeFunction *)object;
        py68_free(&runtime->allocator, PY68_MEM_FUNCTION, function,
                  sizeof(Py68NativeFunction));
    } else if (object->type == PY68_OBJECT_RANGE) {
        Py68Range *range = (Py68Range *)object;
        if (range->list != NULL) {
            py68_object_release(runtime, &range->list->base);
        }
        py68_free(&runtime->allocator, PY68_MEM_RUNTIME, range,
                  sizeof(Py68Range));
    } else if (object->type == PY68_OBJECT_FILE) {
        py68_file_destroy(runtime, (Py68File *)object);
    } else if (object->type == PY68_OBJECT_TUPLE) {
        Py68Tuple *tuple = (Py68Tuple *)object;
        Py68U32 index;
        for (index = 0; index < tuple->count; ++index)
            py68_value_release(runtime, tuple->items[index]);
        py68_free(&runtime->allocator, PY68_MEM_TUPLE, tuple->items,
                  tuple->count * sizeof(Py68Value));
        py68_free(&runtime->allocator, PY68_MEM_TUPLE, tuple,
                  sizeof(Py68Tuple));
    } else if (object->type == PY68_OBJECT_DICT) {
        Py68Dict *dict = (Py68Dict *)object;
        Py68U32 index;
        for (index = 0; index < dict->capacity; ++index) {
            if (dict->entries[index].used == 1) {
                py68_value_release(runtime, dict->entries[index].key);
                py68_value_release(runtime, dict->entries[index].value);
            }
        }
        py68_free(&runtime->allocator, PY68_MEM_DICT, dict->entries,
                  dict->capacity * sizeof(Py68DictEntry));
        py68_free(&runtime->allocator, PY68_MEM_DICT, dict, sizeof(Py68Dict));
    } else if (object->type == PY68_OBJECT_SET) {
        Py68Set *set = (Py68Set *)object;
        Py68U32 index;
        for (index = 0; index < set->capacity; ++index) {
            if (set->entries[index].used == 1)
                py68_value_release(runtime, set->entries[index].value);
        }
        py68_free(&runtime->allocator, PY68_MEM_SET, set->entries,
                  set->capacity * sizeof(Py68SetEntry));
        py68_free(&runtime->allocator, PY68_MEM_SET, set, sizeof(Py68Set));
    } else if (object->type == PY68_OBJECT_BOUND_METHOD) {
        Py68BoundMethod *method = (Py68BoundMethod *)object;
        py68_value_release(runtime, method->self);
        if (method->callable != NULL)
            py68_object_release(runtime, method->callable);
        py68_free(&runtime->allocator, PY68_MEM_FUNCTION, method,
                  sizeof(Py68BoundMethod));
    } else if (object->type == PY68_OBJECT_EXCEPTION) {
        Py68Exception *exception = (Py68Exception *)object;
        if (exception->type != NULL)
            py68_object_release(runtime, &exception->type->base);
        py68_free(&runtime->allocator, PY68_MEM_RUNTIME, object,
                  sizeof(Py68Exception));
    } else if (object->type == PY68_OBJECT_MODULE) {
        Py68Module *module = (Py68Module *)object;
        void *seg = module->native_seg;
        module->native_seg = NULL;
        py68_module_clear(runtime, module);
        py68_platform_unload_seg(seg);
        py68_free(&runtime->allocator, PY68_MEM_MODULE, module,
                  sizeof(Py68Module));
    } else if (object->type == PY68_OBJECT_STRUCT_TIME) {
        py68_free(&runtime->allocator, PY68_MEM_RUNTIME, object,
                  sizeof(Py68StructTime));
    } else if (object->type == PY68_OBJECT_GENERATOR) {
        /* Close on release: state is freed deterministically, but the
           script's finally blocks do not run (D-0045). */
        py68_generator_destroy(runtime, (Py68Generator *)object);
    } else if (object->type == PY68_OBJECT_TYPE) {
        Py68Type *type = (Py68Type *)object;
        if (type->name != NULL)
            py68_object_release(runtime, &type->name->base);
        if (type->base_type != NULL)
            py68_object_release(runtime, &type->base_type->base);
        if (type->dict != NULL)
            py68_object_release(runtime, &type->dict->base);
        py68_free(&runtime->allocator, PY68_MEM_RUNTIME, type,
                  sizeof(Py68Type));
    } else if (object->type == PY68_OBJECT_INSTANCE) {
        Py68Instance *instance = (Py68Instance *)object;
        if (instance->type != NULL)
            py68_object_release(runtime, &instance->type->base);
        if (instance->dict != NULL)
            py68_object_release(runtime, &instance->dict->base);
        py68_free(&runtime->allocator, PY68_MEM_RUNTIME, instance,
                  sizeof(Py68Instance));
    } else {
        py68_free(&runtime->allocator, PY68_MEM_RUNTIME, object,
                  (Py68U32)sizeof(Py68Object));
    }
}
