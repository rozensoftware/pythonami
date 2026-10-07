/* 2026 by Piotr Rozentreter (Rozsoft) */

#include "py68k_ast_arena.h"
#include "py68k_builtin.h"
#include "py68k_compiler.h"
#include "py68k_global.h"
#include "py68k_import.h"
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
    int passed = 1;
    const char *text =
        "import mod_a\n"
        "got = mod_a.value\n";

    passed &= py68_runtime_initialize(&runtime) == PY68_STATUS_OK;
    /* Amiga volume/assign paths must keep the trailing colon. */
    py68_set_script_dir(&runtime, "DH0:scripts/test1.py");
    passed &= strcmp(runtime.script_dir, "DH0:scripts") == 0;
    py68_set_script_dir(&runtime, "PROGDIR:test1.py");
    passed &= strcmp(runtime.script_dir, "PROGDIR:") == 0;
    /* Bare script name leaves script_dir "."; import still finds siblings. */
    py68_set_script_dir(&runtime, "test1.py");
    passed &= strcmp(runtime.script_dir, ".") == 0;
    py68_set_script_dir(&runtime, "tests/language/import/driver.py");
    passed &= py68_builtins_install(&runtime) == PY68_STATUS_OK;
    passed &= py68_sys_install(&runtime) == PY68_STATUS_OK;
    py68_token_array_initialize(&tokens);
    passed &= py68_source_initialize(&runtime.allocator, &source, "imp.py",
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
    passed &= py68_vm_execute(&runtime, &code) == PY68_STATUS_OK;
    passed &= py68_global_get_copy(&runtime, (const Py68U8 *)"got", 3,
                                   &value) == PY68_STATUS_OK;
    passed &= value.type == PY68_VALUE_INT && value.as.integer == 41;
    py68_value_release(&runtime, value);
    passed &= runtime.import_count == 2;
    py68_code_destroy(&runtime.allocator, &code);
    py68_ast_arena_destroy(&arena);
    py68_token_array_destroy(&runtime.allocator, &tokens);
    py68_source_destroy(&runtime.allocator, &source);
    py68_runtime_shutdown(&runtime);
    passed &= runtime.allocator.stats.current_bytes == 0;
    if (passed) { puts("PASS: import tests"); return 0; }
    return 1;
}
