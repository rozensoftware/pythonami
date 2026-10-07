/* 2026 by Piotr Rozentreter (Rozsoft) */

#include "py68k_runtime.h"
#include "py68k_source.h"
#include "py68k_token.h"
#include "py68k_tokenizer.h"
#include "py68k_ast_arena.h"
#include "py68k_parser.h"
#include "py68k_compiler.h"
#include "py68k_verify.h"
#include "py68k_vm.h"
#include "py68k_builtin.h"
#include "py68k_exception.h"
#include "py68k_global.h"
#include "py68k_import.h"
#include "py68k_native.h"
#include "py68k_string.h"

#include <string.h>

#ifdef PY68K_AMIGA
/* vbcc/SAS-C startup convention (targets/m68k-amigaos lib/startup.o): a
   global `long __stack` requests this process stack size in bytes instead
   of inheriting the caller's (often 4 KiB) stack. Recursive-descent
   tokenizing/parsing/compiling plus nested `import`-triggered re-entry into
   py68_vm_execute_module stack several such call chains on the C stack; the
   default stack silently corrupts memory that only faults later, after a
   script has already produced correct output (see docs/decisions.md). */
long __stack = 65536L;
#endif

#define PY68K_VERSION "Python68K 0.8.1\n"
#define PY68K_HELP \
    "Usage: pythonami [--debug] [--check] [-V|--help] [-c cmd | script.py]\n"

static Py68Status py68_write_literal(Py68Runtime *runtime, const char *text)
{
    return py68_platform_write_stdout(runtime, text, (Py68U32)strlen(text));
}

static Py68U32 py68_format_u32(char *buffer, Py68U32 capacity, Py68U32 value)
{
    char digits[10];
    Py68U32 digit_count = 0;
    Py68U32 written = 0;
    do {
        digits[digit_count++] = (char)('0' + (value % 10));
        value /= 10;
    } while (value != 0 && digit_count < sizeof(digits));
    while (digit_count != 0 && written < capacity) {
        buffer[written++] = digits[--digit_count];
    }
    return written;
}

static Py68U32 py68_source_line_count(const Py68U8 *data, Py68U32 length)
{
    Py68U32 index;
    Py68U32 lines = 0;
    int has_content = 0;

    for (index = 0; index < length; ++index) {
        if (data[index] == (Py68U8)'\r') {
            if (index + 1 < length && data[index + 1] == (Py68U8)'\n')
                ++index;
            ++lines;
            has_content = 0;
        } else if (data[index] == (Py68U8)'\n') {
            ++lines;
            has_content = 0;
        } else if (data[index] != (Py68U8)'\r') {
            has_content = 1;
        }
    }
    if (has_content) ++lines;
    return lines;
}

static Py68Status py68_append_text(char *buffer, Py68U32 capacity,
                                   Py68U32 *length, const char *text)
{
    Py68U32 index;
    for (index = 0; text[index] != '\0' && *length < capacity - 1; ++index)
        buffer[(*length)++] = text[index];
    return PY68_STATUS_OK;
}

static Py68Status py68_report_error(Py68Runtime *runtime, const char *path)
{
    /* Note: runtime->error.filename may already be a dangling pointer into
       a destroyed Py68Source by the time this runs, so always use the
       caller-owned `path` string instead. */
    char buffer[640];
    Py68U32 length = 0;
    const char *kind_name = py68_error_kind_name(runtime->error.kind);

    py68_append_text(buffer, sizeof(buffer), &length, kind_name);
    py68_append_text(buffer, sizeof(buffer), &length, ": ");
    py68_append_text(buffer, sizeof(buffer), &length, runtime->error.message);
    if (length < sizeof(buffer) - 1) buffer[length++] = '\n';

    if (runtime->error.location.line != 0) {
        py68_append_text(buffer, sizeof(buffer), &length, "  at ");
        py68_append_text(buffer, sizeof(buffer), &length,
                         path != NULL && path[0] != '\0' ? path : "<string>");
        if (length < sizeof(buffer) - 1) buffer[length++] = ':';
        length += py68_format_u32(buffer + length,
                                  (Py68U32)sizeof(buffer) - length,
                                  runtime->error.location.line);
        if (length < sizeof(buffer) - 1) buffer[length++] = '\n';
    }

    if (runtime->traceback_length != 0) {
        Py68U32 index;
        for (index = 0;
             index < runtime->traceback_length && length < sizeof(buffer) - 1;
             ++index)
            buffer[length++] = runtime->traceback[index];
    }

    return py68_platform_write_stderr(runtime, buffer, length);
}

static Py68U32 py68_live_object_count(const Py68Runtime *runtime)
{
    const Py68Object *object;
    Py68U32 count = 0;
    for (object = runtime->live_objects; object != NULL;
         object = object->next_object)
        ++count;
    return count;
}

static void py68_report_debug_stats(Py68Runtime *runtime,
                                    const Py68Source *source,
                                    const Py68TokenArray *tokens,
                                    const Py68Code *code,
                                    int source_file_count)
{
    char buffer[2048];
    Py68U32 length = 0;
    Py68U32 source_bytes = source != NULL ? source->length : 0;
    Py68U32 source_lines = source != NULL ?
        py68_source_line_count(source->data, source->length) : 0;
    Py68U32 token_count = tokens != NULL ? tokens->count : 0;
    Py68U32 bytecode_bytes = code != NULL ? code->bytecode_length : 0;
    Py68U32 maximum_stack = code != NULL ? code->maximum_stack : 0;

    py68_append_text(buffer, sizeof(buffer), &length,
                     "--- Python68K debug statistics (top-level source) ---\n");
#define PY68_DEBUG_FIELD(name, value) \
    py68_append_text(buffer, sizeof(buffer), &length, name "="); \
    length += py68_format_u32(buffer + length, \
                              (Py68U32)sizeof(buffer) - length, value); \
    if (length < (Py68U32)sizeof(buffer) - 1) buffer[length++] = '\n'
    PY68_DEBUG_FIELD("memory_current_bytes", runtime->allocator.stats.current_bytes);
    PY68_DEBUG_FIELD("memory_peak_bytes", runtime->allocator.stats.peak_bytes);
    PY68_DEBUG_FIELD("allocation_count", runtime->allocator.stats.allocation_count);
    PY68_DEBUG_FIELD("free_count", runtime->allocator.stats.free_count);
    PY68_DEBUG_FIELD("failed_count", runtime->allocator.stats.failed_count);
    PY68_DEBUG_FIELD("global_count", runtime->global_count);
    PY68_DEBUG_FIELD("builtin_count", runtime->builtin_count);
    PY68_DEBUG_FIELD("source_file_count", (Py68U32)source_file_count);
    PY68_DEBUG_FIELD("source_bytes", source_bytes);
    PY68_DEBUG_FIELD("source_lines", source_lines);
    PY68_DEBUG_FIELD("token_count", token_count);
    PY68_DEBUG_FIELD("bytecode_bytes", bytecode_bytes);
    PY68_DEBUG_FIELD("maximum_stack", maximum_stack);
    PY68_DEBUG_FIELD("frame_count", runtime->frame_count);
    PY68_DEBUG_FIELD("value_stack_count", runtime->value_stack_count);
    PY68_DEBUG_FIELD("live_object_count", py68_live_object_count(runtime));
    py68_append_text(buffer, sizeof(buffer), &length,
                     "--- end Python68K debug statistics ---\n");
#undef PY68_DEBUG_FIELD
    (void)py68_platform_write_stderr(runtime, buffer, length);
}

static Py68Status py68_execute_source(Py68Runtime *runtime, const char *path,
                                      const Py68U8 *data, Py68U32 length,
                                      int debug_enabled, int check_only)
{
    Py68Source source;
    Py68TokenArray tokens;
    Py68AstArena arena;
    Py68StatementParser parser;
    Py68AstNode *module;
    Py68Code code;
    Py68Status status;
    int code_ready = 0;
    int report_emitted = 0;

    status = py68_source_initialize(&runtime->allocator, &source, path,
                                    data, length);
    if (status != PY68_STATUS_OK) {
        if (debug_enabled) {
            py68_report_debug_stats(runtime, NULL, NULL, NULL, 0);
            report_emitted = 1;
        }
        return status;
    }
    py68_token_array_initialize(&tokens);
    py68_error_clear(&runtime->error);
    runtime->traceback_length = 0;
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
    status = py68_parse_module(&parser, &module);
    if (status != PY68_STATUS_OK) goto cleanup_arena;
    status = py68_compile_module(&runtime->allocator, &source, module,
                                 &code, &runtime->error);
    if (status != PY68_STATUS_OK) goto cleanup_arena;
    code_ready = 1;
    status = py68_verify_code(&code, &runtime->error);
    if (status != PY68_STATUS_OK) goto cleanup_code;
    if (!check_only) {
        Py68String *module_name = NULL;
        status = py68_builtins_install(runtime);
        if (status != PY68_STATUS_OK) goto cleanup_code;
        status = py68_sys_install(runtime);
        if (status != PY68_STATUS_OK) goto cleanup_code;
        status = py68_string_new_copy(runtime, "__main__", 8,
                                      &module_name);
        if (status == PY68_STATUS_OK)
            status = py68_global_set_copy(
                runtime, (const Py68U8 *)"__name__", 8,
                py68_value_from_object(&module_name->base));
        if (module_name != NULL)
            py68_object_release(runtime, &module_name->base);
        if (status != PY68_STATUS_OK) goto cleanup_code;
        status = py68_vm_execute(runtime, &code);
    }
cleanup_code:
    if (debug_enabled)
        py68_report_debug_stats(runtime, &source, &tokens,
                                code_ready ? &code : NULL, 1);
    report_emitted = debug_enabled;
    py68_code_destroy(&runtime->allocator, &code);
cleanup_arena:
    py68_ast_arena_destroy(&arena);
    py68_token_array_destroy(&runtime->allocator, &tokens);
cleanup_source:
    if (debug_enabled && !report_emitted)
        py68_report_debug_stats(runtime, &source, &tokens,
                                code_ready ? &code : NULL, 1);
    py68_source_destroy(&runtime->allocator, &source);
    if (status != PY68_STATUS_OK && status != PY68_STATUS_EXIT) {
        py68_report_error(runtime, path);
    }
    return status;
}

static Py68Status py68_execute_file(Py68Runtime *runtime, const char *path,
                                    int debug_enabled, int check_only)
{
    Py68U8 *file_data;
    Py68U32 file_length;
    Py68Status status;

    status = py68_platform_read_file(runtime, path, &file_data, &file_length);
    if (status != PY68_STATUS_OK) {
        if (debug_enabled)
            py68_report_debug_stats(runtime, NULL, NULL, NULL, 0);
        return status;
    }
    status = py68_execute_source(runtime, path, file_data, file_length,
                                 debug_enabled, check_only);
    py68_free(&runtime->allocator, PY68_MEM_SOURCE, file_data, file_length + 1);
    return status;
}

static Py68Status py68_execute_command(Py68Runtime *runtime, const char *command,
                                       int debug_enabled, int check_only)
{
    return py68_execute_source(runtime, "<string>",
                               (const Py68U8 *)command,
                               (Py68U32)strlen(command), debug_enabled,
                               check_only);
}

static int py68_exit_status(Py68Runtime *runtime, Py68Status status)
{
    Py68I32 code;
    if (status == PY68_STATUS_OK) return 0;
    if (status == PY68_STATUS_EXIT) {
        code = runtime->requested_exit_code;
        if (code < 0) code = 1;
        if (code > 255) code = 255;
        return (int)code;
    }
    /* AmigaDOS RETURN_ERROR for a user break. */
    if (runtime->error.kind == PY68_ERROR_INTERRUPT) return 10;
    return (int)status;
}

int main(int argc, char **argv)
{
    Py68Runtime runtime;
    Py68Status status;
    int debug_enabled = 0;
    int check_only = 0;

    status = py68_runtime_initialize(&runtime);
    if (status != PY68_STATUS_OK) {
        return (int)status;
    }

    if (argc > 1 && strcmp(argv[1], "--debug") == 0) {
        debug_enabled = 1;
        --argc;
        ++argv;
    }
    if (argc > 1 && strcmp(argv[1], "--check") == 0) {
        check_only = 1;
        --argc;
        ++argv;
    }

    if (argc == 2 && (strcmp(argv[1], "-V") == 0 ||
                      strcmp(argv[1], "--version") == 0)) {
        status = py68_write_literal(&runtime, PY68K_VERSION);
    } else if (argc == 2 && strcmp(argv[1], "--help") == 0) {
        status = py68_write_literal(&runtime, PY68K_HELP);
    } else if (argc == 1) {
        status = py68_write_literal(&runtime, PY68K_HELP);
    } else if (argc == 3 && strcmp(argv[1], "-c") == 0) {
        status = py68_sys_set_argv(&runtime, 1, argv + 1);
        if (status == PY68_STATUS_OK) {
            status = py68_execute_command(&runtime, argv[2], debug_enabled,
                                           check_only);
        } else if (debug_enabled) {
            py68_report_debug_stats(&runtime, NULL, NULL, NULL, 0);
        }
    } else if (argc >= 2 && argv[1][0] != '-') {
        py68_set_script_dir(&runtime, argv[1]);
        status = py68_sys_set_argv(&runtime, argc - 1, argv + 1);
        if (status == PY68_STATUS_OK) {
            status = py68_execute_file(&runtime, argv[1], debug_enabled,
                                        check_only);
        } else if (debug_enabled) {
            py68_report_debug_stats(&runtime, NULL, NULL, NULL, 0);
        }
    } else {
        status = py68_platform_write_stderr(
            &runtime, "error: invalid arguments\n", 25);
        if (status == PY68_STATUS_OK) status = PY68_STATUS_SOURCE_ERROR;
    }

    {
        int exit_code = py68_exit_status(&runtime, status);
        py68_runtime_shutdown(&runtime);
        return exit_code;
    }
}
