/* 2026 by Piotr Rozentreter (Rozsoft) */

#include "py68k_ast_arena.h"
#include "py68k_builtin.h"
#include "py68k_compiler.h"
#include "py68k_error.h"
#include "py68k_exception.h"
#include "py68k_global.h"
#include "py68k_parser.h"
#include "py68k_runtime.h"
#include "py68k_source.h"
#include "py68k_tokenizer.h"
#include "py68k_vm.h"

#include <stdio.h>
#include <string.h>

int main(void)
{
    Py68Runtime runtime;
    Py68Source source;
    Py68TokenArray tokens;
    Py68AstArena arena;
    Py68StatementParser parser;
    Py68AstNode *module;
    Py68Code code;
    Py68Error error;
    Py68Value value;
    Py68Value os_error;
    Py68Value io_error;
    Py68Exception *raised = NULL;
    int passed = 1;
    const char *text =
        "caught = 0\n"
        "try:\n"
        "    1 // 0\n"
        "except ZeroDivisionError:\n"
        "    caught = 1\n";

    passed &= strcmp(py68_error_kind_name(PY68_ERROR_IO), "OSError") == 0;
    passed &= py68_error_kind_from_name("OSError", 7) == PY68_ERROR_IO;
    passed &= py68_error_kind_from_name("IOError", 7) == PY68_ERROR_IO;

    passed &= py68_runtime_initialize(&runtime) == PY68_STATUS_OK;
    py68_token_array_initialize(&tokens);
    passed &= py68_source_initialize(&runtime.allocator, &source, "exc.py",
        (const Py68U8 *)text, (Py68U32)strlen(text)) == PY68_STATUS_OK;
    passed &= py68_tokenize(&runtime.allocator, &source, &tokens, &error) ==
              PY68_STATUS_OK;
    py68_ast_arena_initialize(&arena, &runtime.allocator);
    parser.expression.allocator = &runtime.allocator;
    parser.expression.source = &source;
    parser.expression.tokens = &tokens;
    parser.expression.position = 0;
    parser.expression.arena = &arena;
    parser.expression.error = &error;
    parser.inside_function = 0;
    parser.loop_depth = 0;
    passed &= py68_parse_module(&parser, &module) == PY68_STATUS_OK;
    passed &= py68_compile_module(&runtime.allocator, &source, module, &code,
                                  &error) == PY68_STATUS_OK;
    passed &= py68_builtins_install(&runtime) == PY68_STATUS_OK;
    passed &= py68_vm_execute(&runtime, &code) == PY68_STATUS_OK;
    passed &= py68_global_get_copy(&runtime, (const Py68U8 *)"caught", 6,
                                   &value) == PY68_STATUS_OK;
    passed &= value.type == PY68_VALUE_INT && value.as.integer == 1;
    py68_value_release(&runtime, value);

    passed &= py68_builtin_get_copy(&runtime, (const Py68U8 *)"OSError", 7,
                                    &os_error) == PY68_STATUS_OK;
    passed &= py68_builtin_get_copy(&runtime, (const Py68U8 *)"IOError", 7,
                                    &io_error) == PY68_STATUS_OK;
    passed &= py68_exception_new(&runtime, PY68_ERROR_IO, "disk", &raised) ==
              PY68_STATUS_OK;
    passed &= py68_exception_matches(&runtime, raised, os_error) != 0;
    passed &= py68_exception_matches(&runtime, raised, io_error) != 0;
    py68_object_release(&runtime, &raised->base);
    py68_value_release(&runtime, os_error);
    py68_value_release(&runtime, io_error);

    py68_code_destroy(&runtime.allocator, &code);
    py68_ast_arena_destroy(&arena);
    py68_token_array_destroy(&runtime.allocator, &tokens);
    py68_source_destroy(&runtime.allocator, &source);
    py68_runtime_shutdown(&runtime);
    passed &= runtime.allocator.stats.current_bytes == 0;
    if (passed) { puts("PASS: exception tests"); return 0; }
    return 1;
}
