/* 2026 by Piotr Rozentreter (Rozsoft) */

#include "py68k_import.h"
#include "py68k_ast_arena.h"
#include "py68k_builtin.h"
#include "py68k_compiler.h"
#include "py68k_list.h"
#include "py68k_module.h"
#include "py68k_native.h"
#include "py68k_parser.h"
#include "py68k_platform.h"
#include "py68k_runtime.h"
#include "py68k_source.h"
#include "py68k_string.h"
#include "py68k_tokenizer.h"
#include "py68k_verify.h"
#include "py68k_vm.h"

#include <stddef.h>
#include <string.h>

static void py68_import_error(Py68Runtime *runtime, Py68ErrorKind kind,
                              const char *message)
{
    Py68Location location;
    location.offset = 0;
    location.line = 0;
    location.column = 0;
    location.length = 0;
    py68_error_set(&runtime->error, kind, location, NULL, message);
}

void py68_set_script_dir(Py68Runtime *runtime, const char *path)
{
    Py68U32 length;
    Py68I32 last = -1;
    runtime->script_dir[0] = '.';
    runtime->script_dir[1] = '\0';
    if (path == NULL || path[0] == '\0' || path[0] == '<') return;
    length = 0;
    while (path[length] != '\0' && length + 1 < PY68_PATH_MAX) {
        if (path[length] == '/' || path[length] == ':' || path[length] == '\\')
            last = (Py68I32)length;
        ++length;
    }
    if (last < 0) return;
    /*
     * Keep Amiga volume/assign colon in the prefix so join yields
     * "DH0:myclass.py" / "PROGDIR:myclass.py", not "DH0/myclass.py".
     * Unix/Windows directory separators are stripped as usual.
     */
    if (path[last] == ':') {
        if ((Py68U32)last + 2 > PY68_PATH_MAX) return;
        memcpy(runtime->script_dir, path, (Py68U32)last + 1);
        runtime->script_dir[last + 1] = '\0';
    } else {
        if (last == 0 && path[0] == '/') {
            runtime->script_dir[0] = '/';
            runtime->script_dir[1] = '\0';
            return;
        }
        memcpy(runtime->script_dir, path, (Py68U32)last);
        runtime->script_dir[last] = '\0';
    }
}

static void py68_join_path(char *out, const char *dir, const char *name)
{
    Py68U32 pos = 0;
    /*
     * Bare "." (default script_dir / sys.path entry) must not become "./file".
     * On AmigaDOS, "/" is parent-directory and "./name" often fails Lock().
     */
    if (dir != NULL && dir[0] != '\0' &&
        !(dir[0] == '.' && dir[1] == '\0')) {
        while (dir[pos] != '\0' && pos + 2 < PY68_PATH_MAX) {
            out[pos] = dir[pos];
            ++pos;
        }
        if (pos != 0 && out[pos - 1] != '/' && out[pos - 1] != ':' &&
            out[pos - 1] != '\\' && pos + 1 < PY68_PATH_MAX)
            out[pos++] = '/';
    }
    while (name[0] != '\0' && pos + 1 < PY68_PATH_MAX) {
        out[pos++] = *name++;
    }
    out[pos] = '\0';
}

static Py68Status py68_import_cache_add(Py68Runtime *runtime,
                                        Py68Module *module)
{
    Py68Object **items;
    Py68U16 capacity;
    if (runtime->import_count == runtime->import_capacity) {
        capacity = runtime->import_capacity == 0 ? 4 :
                   (Py68U16)(runtime->import_capacity * 2);
        items = (Py68Object **)py68_realloc(
            &runtime->allocator, PY68_MEM_MODULE, runtime->import_modules,
            (Py68U32)runtime->import_capacity * sizeof(Py68Object *),
            (Py68U32)capacity * sizeof(Py68Object *));
        if (items == NULL) return PY68_STATUS_MEMORY_ERROR;
        runtime->import_modules = items;
        runtime->import_capacity = capacity;
    }
    runtime->import_modules[runtime->import_count++] = &module->base;
    py68_object_retain(&module->base);
    return PY68_STATUS_OK;
}

Py68Status py68_os_install(Py68Runtime *runtime, Py68Value *result)
{
    Py68Module *module;
    Py68NativeFunction *function = NULL;
    Py68Status status;

    status = py68_module_new(runtime, "os", "os", &module);
    if (status != PY68_STATUS_OK) return status;
    status = py68_native_new(runtime, "system", 1, 1,
                             py68_builtin_system, &function);
    if (status == PY68_STATUS_OK)
        status = py68_module_set(runtime, module, (const Py68U8 *)"system", 6,
                                 py68_value_from_object(&function->base));
    if (function != NULL)
        py68_object_release(runtime, &function->base);
    if (status != PY68_STATUS_OK) {
        py68_object_release(runtime, &module->base);
        return status;
    }
    function = NULL;
    status = py68_native_new(runtime, "popen", 1, 1,
                             py68_builtin_popen, &function);
    if (status == PY68_STATUS_OK)
        status = py68_module_set(runtime, module, (const Py68U8 *)"popen", 5,
                                 py68_value_from_object(&function->base));
    if (function != NULL)
        py68_object_release(runtime, &function->base);
    if (status != PY68_STATUS_OK) {
        py68_object_release(runtime, &module->base);
        return status;
    }
    status = py68_import_cache_add(runtime, module);
    if (status != PY68_STATUS_OK) {
        py68_object_release(runtime, &module->base);
        return status;
    }
    *result = py68_value_from_object(&module->base);
    py68_value_retain(*result);
    py68_object_release(runtime, &module->base);
    return PY68_STATUS_OK;
}

static void py68_import_cache_remove(Py68Runtime *runtime,
                                     Py68Module *module)
{
    Py68U16 index;
    for (index = 0; index < runtime->import_count; ++index) {
        if (runtime->import_modules[index] == &module->base) {
            py68_object_release(runtime, runtime->import_modules[index]);
            --runtime->import_count;
            while (index < runtime->import_count) {
                runtime->import_modules[index] =
                    runtime->import_modules[index + 1];
                ++index;
            }
            return;
        }
    }
}

static Py68Module *py68_import_cache_find(Py68Runtime *runtime,
                                          const char *path)
{
    Py68U16 index;
    for (index = 0; index < runtime->import_count; ++index) {
        Py68Module *module = (Py68Module *)runtime->import_modules[index];
        if (strcmp(module->path, path) == 0 || strcmp(module->name, path) == 0)
            return module;
    }
    return NULL;
}

static Py68Status py68_import_execute_file(Py68Runtime *runtime,
                                           const char *name,
                                           const char *path,
                                           Py68Module **result)
{
    Py68U8 *file_data;
    Py68U32 file_length;
    Py68Source source;
    Py68TokenArray tokens;
    Py68AstArena arena;
    Py68StatementParser parser;
    Py68AstNode *tree;
    Py68Code code;
    Py68Module *module;
    Py68String *module_name;
    Py68GlobalEntry *saved_globals;
    Py68U16 saved_count;
    Py68U16 saved_capacity;
    Py68Module *saved_executing;
    Py68Status status;

    status = py68_platform_read_file(runtime, path, &file_data, &file_length);
    if (status != PY68_STATUS_OK) {
        py68_import_error(runtime, PY68_ERROR_IMPORT, "module file not found");
        return PY68_STATUS_RUNTIME_ERROR;
    }
    status = py68_source_initialize(&runtime->allocator, &source, path,
                                    file_data, file_length);
    py68_free(&runtime->allocator, PY68_MEM_SOURCE, file_data, file_length + 1);
    if (status != PY68_STATUS_OK) return status;
    py68_token_array_initialize(&tokens);
    py68_error_clear(&runtime->error);
    status = py68_tokenize(&runtime->allocator, &source, &tokens,
                           &runtime->error);
    if (status != PY68_STATUS_OK) goto cleanup_source;
    py68_ast_arena_initialize(&arena, &runtime->allocator);
    parser.expression.allocator = &runtime->allocator;
    parser.expression.source = &source;
    parser.expression.tokens = &tokens;
    parser.expression.position = 0;
    parser.expression.arena = &arena;
    parser.expression.error = &runtime->error;
    parser.inside_function = 0;
    parser.loop_depth = 0;
    status = py68_parse_module(&parser, &tree);
    if (status != PY68_STATUS_OK) goto cleanup_arena;
    status = py68_compile_module(&runtime->allocator, &source, tree, &code,
                                 &runtime->error);
    if (status != PY68_STATUS_OK) goto cleanup_arena;
    status = py68_module_new(runtime, name, path, &module);
    if (status != PY68_STATUS_OK) goto cleanup_code;
    module_name = NULL;
    status = py68_string_new_copy(runtime, name, (Py68U32)strlen(name),
                                  &module_name);
    if (status == PY68_STATUS_OK)
        status = py68_module_set(runtime, module,
                                 (const Py68U8 *)"__name__", 8,
                                 py68_value_from_object(&module_name->base));
    if (module_name != NULL)
        py68_object_release(runtime, &module_name->base);
    if (status != PY68_STATUS_OK) {
        py68_object_release(runtime, &module->base);
        goto cleanup_code;
    }
    module->base.flags |= 1;
    status = py68_import_cache_add(runtime, module);
    if (status != PY68_STATUS_OK) {
        py68_object_release(runtime, &module->base);
        goto cleanup_code;
    }
    saved_globals = runtime->globals;
    saved_count = runtime->global_count;
    saved_capacity = runtime->global_capacity;
    saved_executing = runtime->executing_module;
    runtime->globals = module->globals;
    runtime->global_count = module->global_count;
    runtime->global_capacity = module->global_capacity;
    runtime->executing_module = module;
    status = py68_vm_execute_module(runtime, &code);
    runtime->executing_module = saved_executing;
    module->globals = runtime->globals;
    module->global_count = runtime->global_count;
    module->global_capacity = runtime->global_capacity;
    runtime->globals = saved_globals;
    runtime->global_count = saved_count;
    runtime->global_capacity = saved_capacity;
    if (status != PY68_STATUS_OK) {
        py68_import_cache_remove(runtime, module);
        py68_object_release(runtime, &module->base);
        goto cleanup_code;
    }
    module->base.flags &= (Py68U16)~1;
    module->owned_source = source.data;
    module->owned_source_length = source.length;
    source.data = NULL;
    source.length = 0;
    /* Transfer compiled code ownership so imported defs remain callable. */
    module->owned_code = code;
    py68_code_initialize(&code);
    *result = module;
cleanup_code:
    py68_code_destroy(&runtime->allocator, &code);
cleanup_arena:
    py68_ast_arena_destroy(&arena);
    py68_token_array_destroy(&runtime->allocator, &tokens);
cleanup_source:
    py68_source_destroy(&runtime->allocator, &source);
    return status;
}

static int py68_build_py_name(char *out, const Py68U8 *name, Py68U16 length)
{
    Py68U16 index;
    if ((Py68U32)length + 4 >= PY68_PATH_MAX) return 0;
    for (index = 0; index < length; ++index) {
        if (name[index] == '.' || name[index] == '/' || name[index] == '\\')
            return 0;
        out[index] = (char)name[index];
    }
    out[length] = '.';
    out[length + 1] = 'p';
    out[length + 2] = 'y';
    out[length + 3] = '\0';
    return 1;
}

Py68Status py68_import_name(Py68Runtime *runtime, const Py68U8 *name,
                            Py68U16 name_length, Py68Value *result)
{
    char module_name[64];
    char file_name[PY68_PATH_MAX];
    char path[PY68_PATH_MAX];
    Py68Module *module;
    Py68U16 index;
    Py68Value item;
    const char *dir;
    char dirbuf[PY68_PATH_MAX];

    if (name_length == 0 || name_length >= 63) {
        py68_import_error(runtime, PY68_ERROR_IMPORT, "invalid module name");
        return PY68_STATUS_RUNTIME_ERROR;
    }
    memcpy(module_name, name, name_length);
    module_name[name_length] = '\0';
    if (name_length == 3 && memcmp(name, "sys", 3) == 0 &&
        runtime->sys_module != NULL) {
        *result = py68_value_from_object(runtime->sys_module);
        py68_value_retain(*result);
        return PY68_STATUS_OK;
    }
    module = py68_import_cache_find(runtime, module_name);
    if (module != NULL) {
        if ((module->base.flags & 1) != 0) {
            py68_import_error(runtime, PY68_ERROR_IMPORT,
                              "import cycle detected");
            return PY68_STATUS_RUNTIME_ERROR;
        }
        *result = py68_value_from_object(&module->base);
        py68_value_retain(*result);
        return PY68_STATUS_OK;
    }
    if (name_length == 2 && memcmp(name, "os", 2) == 0)
        return py68_os_install(runtime, result);
    if (!py68_build_py_name(file_name, name, name_length)) {
        py68_import_error(runtime, PY68_ERROR_IMPORT,
                          "relative imports are not supported");
        return PY68_STATUS_RUNTIME_ERROR;
    }
    py68_join_path(path, runtime->script_dir, file_name);
    if (py68_platform_path_exists(path)) {
        if (py68_import_execute_file(runtime, module_name, path, &module) !=
            PY68_STATUS_OK)
            return runtime->error.kind == PY68_ERROR_SYNTAX ?
                PY68_STATUS_SOURCE_ERROR : PY68_STATUS_RUNTIME_ERROR;
        *result = py68_value_from_object(&module->base);
        return PY68_STATUS_OK;
    }
    if (runtime->sys_path != NULL) {
        for (index = 0; index < runtime->sys_path->count; ++index) {
            item = runtime->sys_path->items[index];
            dir = ".";
            if (item.type == PY68_VALUE_OBJECT && item.as.object != NULL &&
                item.as.object->type == PY68_OBJECT_STRING) {
                Py68String *string = (Py68String *)item.as.object;
                Py68U32 copy = string->length;
                if (copy >= PY68_PATH_MAX) copy = PY68_PATH_MAX - 1;
                memcpy(dirbuf, string->data, copy);
                dirbuf[copy] = '\0';
                dir = dirbuf;
            }
            py68_join_path(path, dir, file_name);
            if (py68_platform_path_exists(path)) {
                if (py68_import_execute_file(runtime, module_name, path,
                                             &module) != PY68_STATUS_OK)
                    return runtime->error.kind == PY68_ERROR_SYNTAX ?
                        PY68_STATUS_SOURCE_ERROR : PY68_STATUS_RUNTIME_ERROR;
                *result = py68_value_from_object(&module->base);
                return PY68_STATUS_OK;
            }
        }
    }
    py68_import_error(runtime, PY68_ERROR_IMPORT, "module not found");
    return PY68_STATUS_RUNTIME_ERROR;
}

Py68Status py68_sys_install(Py68Runtime *runtime)
{
    Py68Module *sys;
    Py68List *path;
    Py68List *modules;
    Py68String *dot;
    Py68Status status;
    status = py68_module_new(runtime, "sys", "sys", &sys);
    if (status != PY68_STATUS_OK) return status;
    status = py68_list_new(runtime, &path);
    if (status != PY68_STATUS_OK) return status;
    status = py68_string_new_copy(runtime, ".", 1, &dot);
    if (status != PY68_STATUS_OK) return status;
    status = py68_list_append_copy(runtime, path,
                                   py68_value_from_object(&dot->base));
    py68_object_release(runtime, &dot->base);
    if (status != PY68_STATUS_OK) return status;
    status = py68_list_new(runtime, &modules);
    if (status != PY68_STATUS_OK) return status;
    runtime->sys_path = path;
    runtime->sys_module = &sys->base;
    status = py68_module_set(runtime, sys, (const Py68U8 *)"path", 4,
                             py68_value_from_object(&path->base));
    if (status == PY68_STATUS_OK)
        status = py68_module_set(runtime, sys, (const Py68U8 *)"modules", 7,
                                 py68_value_from_object(&modules->base));
    py68_object_release(runtime, &modules->base);
    if (status == PY68_STATUS_OK)
        py68_object_release(runtime, &path->base);
    if (status == PY68_STATUS_OK)
        status = py68_import_cache_add(runtime, sys);
    if (status == PY68_STATUS_OK && runtime->sys_argv != NULL)
        status = py68_module_set(runtime, sys, (const Py68U8 *)"argv", 4,
                                 py68_value_from_object(&runtime->sys_argv->base));
    return status;
}

Py68Status py68_sys_set_argv(Py68Runtime *runtime, int argc, char **argv)
{
    Py68List *list;
    int index;
    Py68Status status = py68_list_new(runtime, &list);
    if (status != PY68_STATUS_OK) return status;
    for (index = 0; index < argc; ++index) {
        Py68String *string;
        status = py68_string_new_copy(runtime, argv[index],
                                      (Py68U32)strlen(argv[index]), &string);
        if (status != PY68_STATUS_OK) {
            py68_object_release(runtime, &list->base);
            return status;
        }
        status = py68_list_append_copy(runtime, list,
                                       py68_value_from_object(&string->base));
        py68_object_release(runtime, &string->base);
        if (status != PY68_STATUS_OK) {
            py68_object_release(runtime, &list->base);
            return status;
        }
    }
    if (runtime->sys_argv != NULL)
        py68_object_release(runtime, &runtime->sys_argv->base);
    runtime->sys_argv = list;
    return PY68_STATUS_OK;
}
