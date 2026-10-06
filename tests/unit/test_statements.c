/* 2026 by Piotr Rozentreter (Rozsoft) */

#include "py68k_ast_arena.h"
#include "py68k_error.h"
#include "py68k_memory.h"
#include "py68k_parser.h"
#include "py68k_source.h"
#include "py68k_token.h"
#include "py68k_tokenizer.h"

#include <stdio.h>
#include <string.h>

static int check(int condition, const char *message)
{
    if (!condition) fprintf(stderr, "FAIL: %s\n", message);
    return condition;
}

static int parse_module(Py68Allocator *allocator, const char *text,
                        Py68AstArena *arena, Py68AstNode **module,
                        Py68Error *error)
{
    Py68Source source;
    Py68TokenArray *tokens;
    Py68StatementParser parser;
    Py68Status status;

    tokens = (Py68TokenArray *)py68_alloc(allocator, PY68_MEM_TEMP,
                                          (Py68U32)sizeof(Py68TokenArray));
    if (tokens == NULL) return 0;
    if (py68_source_initialize(allocator, &source, "statements.py",
        (const Py68U8 *)text, (Py68U32)strlen(text)) != PY68_STATUS_OK) {
        py68_free(allocator, PY68_MEM_TEMP, tokens,
                  (Py68U32)sizeof(Py68TokenArray));
        return 0;
    }
    status = py68_tokenize(allocator, &source, tokens, error);
    if (status == PY68_STATUS_OK) {
        py68_ast_arena_initialize(arena, allocator);
        parser.expression.allocator = allocator;
        parser.expression.source = &source;
        parser.expression.tokens = tokens;
        parser.expression.position = 0;
        parser.expression.arena = arena;
        parser.expression.error = error;
        parser.inside_function = 0;
        parser.loop_depth = 0;
        status = py68_parse_module(&parser, module);
    }
    py68_token_array_destroy(allocator, tokens);
    py68_source_destroy(allocator, &source);
    py68_free(allocator, PY68_MEM_TEMP, tokens,
              (Py68U32)sizeof(Py68TokenArray));
    return status == PY68_STATUS_OK;
}

int main(void)
{
    const char *program =
        "x = 1\n"
        "if x:\n"
        "    y = 2\n"
        "elif y:\n"
        "    y += 1\n"
        "else:\n"
        "    pass\n"
        "while x:\n"
        "    break\n"
        "for item in items:\n"
        "    continue\n"
        "def add(a, b):\n"
        "    return a, b\n";
    Py68Allocator allocator;
    Py68AstArena arena;
    Py68AstNode *module = NULL;
    Py68Error error;
    int passed = 1;

    py68_allocator_initialize(&allocator);
    passed &= check(parse_module(&allocator, program, &arena, &module, &error),
                    "statement program parses");
    if (module != NULL) {
        passed &= check(module->kind == PY68_AST_MODULE,
                        "module node is created");
        passed &= check(module->as.module.statements.count == 5,
                        "module statement count is stable");
        passed &= check(module->as.module.statements.items[0]->kind ==
                        PY68_AST_ASSIGN, "assignment node is created");
        passed &= check(module->as.module.statements.items[1]->kind ==
                        PY68_AST_IF, "if node is created");
        passed &= check(module->as.module.statements.items[2]->kind ==
                        PY68_AST_WHILE, "while node is created");
        passed &= check(module->as.module.statements.items[3]->kind ==
                        PY68_AST_FOR, "for node is created");
        passed &= check(module->as.module.statements.items[4]->kind ==
                        PY68_AST_FUNCTION_DEF, "function node is created");
        passed &= check(module->as.module.statements.items[4]
                            ->as.function_def.body.items[0]
                            ->as.return_statement.value->kind == PY68_AST_TUPLE &&
                        module->as.module.statements.items[4]
                            ->as.function_def.body.items[0]
                            ->as.return_statement.value
                            ->as.list_literal.elements.count == 2,
                        "return tuple value is created");
        py68_ast_arena_destroy(&arena);
    }
    check(!parse_module(&allocator, "return 1\n", &arena,
                        &module, &error),
          "return outside function is rejected");
    py68_ast_arena_destroy(&arena);
    check(!parse_module(&allocator, "break\n", &arena,
                        &module, &error),
          "break outside loop is rejected");
    py68_ast_arena_destroy(&arena);
    passed &= check(parse_module(&allocator, "class Thing:\n    pass\n",
                                  &arena, &module, &error),
                    "empty class parses");
    if (module != NULL) {
        passed &= check(module->as.module.statements.count == 1 &&
                        module->as.module.statements.items[0]->kind ==
                            PY68_AST_CLASS_DEF,
                        "class node is created");
        passed &= check(module->as.module.statements.items[0]
                            ->as.class_def.base == NULL,
                        "class without base has NULL base");
        passed &= check(module->as.module.statements.items[0]
                            ->as.class_def.body.count == 1 &&
                        module->as.module.statements.items[0]
                            ->as.class_def.body.items[0]->kind == PY68_AST_PASS,
                        "class body is pass");
    }
    py68_ast_arena_destroy(&arena);
    passed &= check(!parse_module(&allocator, "lambda x: x\n",
                                  &arena, &module, &error),
                    "lambda is rejected");
    passed &= check(error.kind == PY68_ERROR_SYNTAX &&
                    strstr(error.message, "lambda is not supported") != NULL,
                    "lambda diagnostic names the keyword");
    py68_ast_arena_destroy(&arena);
    passed &= check(!parse_module(&allocator, "from .x import y\n",
                                  &arena, &module, &error),
                    "relative import is rejected");
    passed &= check(strstr(error.message, "relative imports") != NULL,
                    "relative import diagnostic is targeted");
    py68_ast_arena_destroy(&arena);
    passed &= check(parse_module(&allocator, "ys = (x for x in xs)\n",
                                 &arena, &module, &error),
                    "generator expression parses as an assignment value");
    if (module != NULL) {
        passed &= check(module->as.module.statements.count == 1 &&
                        module->as.module.statements.items[0]->kind ==
                            PY68_AST_ASSIGN &&
                        module->as.module.statements.items[0]
                            ->as.assign.value->kind == PY68_AST_GENERATOR_EXP,
                        "assignment value is a GENERATOR_EXP node");
    }
    py68_ast_arena_destroy(&arena);
    passed &= check(!parse_module(&allocator, "def f():\n    x = yield 1\n",
                                  &arena, &module, &error),
                    "yield as an expression is rejected");
    passed &= check(strstr(error.message, "yield is a statement") != NULL,
                    "yield-expression diagnostic is targeted");
    py68_ast_arena_destroy(&arena);
    passed &= check(parse_module(&allocator, "def f():\n    yield 1\n    yield\n",
                                 &arena, &module, &error),
                    "yield statement parses inside a function");
    if (module != NULL) {
        Py68AstNode *body_statement =
            module->as.module.statements.items[0]->as.function_def.body.items[0];
        passed &= check(body_statement->kind == PY68_AST_YIELD &&
                        body_statement->as.return_statement.value != NULL,
                        "yield with a value builds a YIELD node");
        body_statement =
            module->as.module.statements.items[0]->as.function_def.body.items[1];
        passed &= check(body_statement->kind == PY68_AST_YIELD &&
                        body_statement->as.return_statement.value == NULL,
                        "bare yield builds a YIELD node with no value");
    }
    py68_ast_arena_destroy(&arena);
    passed &= check(!parse_module(&allocator, "yield 1\n",
                                  &arena, &module, &error),
                    "yield outside a function is rejected");
    passed &= check(strstr(error.message, "yield outside function") != NULL,
                    "yield-outside-function diagnostic is targeted");
    py68_ast_arena_destroy(&arena);
    passed &= check(parse_module(&allocator, "a, b = (1, 2)\n",
                                 &arena, &module, &error),
                    "unpack assignment parses");
    if (module != NULL) {
        passed &= check(module->as.module.statements.count == 1 &&
                        module->as.module.statements.items[0]->kind ==
                            PY68_AST_ASSIGN &&
                        module->as.module.statements.items[0]
                            ->as.assign.target != NULL &&
                        module->as.module.statements.items[0]
                            ->as.assign.target->kind == PY68_AST_TUPLE &&
                        module->as.module.statements.items[0]
                            ->as.assign.target->as.list_literal.elements.count == 2,
                        "unpack assignment stores a name tuple target");
        py68_ast_arena_destroy(&arena);
    }
    passed &= check(parse_module(&allocator, "for a, b in pairs:\n    pass\n",
                                 &arena, &module, &error),
                    "for unpack parses");
    if (module != NULL) {
        passed &= check(module->as.module.statements.count == 1 &&
                        module->as.module.statements.items[0]->kind ==
                            PY68_AST_FOR &&
                        module->as.module.statements.items[0]
                            ->as.for_statement.target != NULL &&
                        module->as.module.statements.items[0]
                            ->as.for_statement.target->kind == PY68_AST_TUPLE,
                        "for unpack stores a name tuple target");
        py68_ast_arena_destroy(&arena);
    }
    passed &= check(parse_module(&allocator,
                                 "for (a, b) in pairs:\n    pass\n",
                                 &arena, &module, &error),
                    "parenthesized for unpack parses");
    if (module != NULL) {
        passed &= check(module->as.module.statements.items[0]
                            ->as.for_statement.target != NULL &&
                        module->as.module.statements.items[0]
                            ->as.for_statement.target->as.list_literal
                            .elements.count == 2,
                        "parenthesized for unpack has two names");
        py68_ast_arena_destroy(&arena);
    }
    passed &= check(!parse_module(&allocator, "a, *rest = xs\n",
                                  &arena, &module, &error),
                    "starred unpacking is rejected");
    passed &= check(strstr(error.message, "starred unpacking") != NULL,
                    "starred-unpacking diagnostic is targeted");
    py68_ast_arena_destroy(&arena);
    passed &= check(allocator.stats.current_bytes == 0,
                    "statement parser releases all allocations");
    if (passed) {
        puts("PASS: statement parser tests");
        return 0;
    }
    return 1;
}