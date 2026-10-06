/* 2026 by Piotr Rozentreter (Rozsoft) */

#include "py68k_error.h"
#include "py68k_memory.h"
#include "py68k_source.h"
#include "py68k_token.h"
#include "py68k_tokenizer.h"

#include <stdio.h>
#include <string.h>

static int check(int condition, const char *message)
{
    if (!condition) {
        fprintf(stderr, "FAIL: %s\n", message);
        return 0;
    }
    return 1;
}

static int tokenize(Py68Allocator *allocator, const char *text,
                    Py68TokenArray *tokens, Py68Error *error)
{
    Py68Source source;
    Py68Status status;

    if (py68_source_initialize(allocator, &source, "test.py",
                               (const Py68U8 *)text,
                               (Py68U32)strlen(text)) != PY68_STATUS_OK) {
        return 0;
    }
    status = py68_tokenize(allocator, &source, tokens, error);
    py68_source_destroy(allocator, &source);
    return status == PY68_STATUS_OK;
}

int main(void)
{
    Py68Allocator allocator;
    Py68TokenArray tokens;
    Py68Error error;
    Py68TokenKind expected[] = {
        PY68_TOKEN_IF, PY68_TOKEN_NAME, PY68_TOKEN_COLON,
        PY68_TOKEN_NEWLINE, PY68_TOKEN_INDENT, PY68_TOKEN_NAME,
        PY68_TOKEN_ASSIGN, PY68_TOKEN_INTEGER, PY68_TOKEN_NEWLINE,
        PY68_TOKEN_NAME, PY68_TOKEN_ASSIGN, PY68_TOKEN_STRING,
        PY68_TOKEN_NEWLINE, PY68_TOKEN_DEDENT, PY68_TOKEN_EOF
    };
    Py68U16 index;
    int passed = 1;

    py68_allocator_initialize(&allocator);
    passed &= check(tokenize(&allocator,
        "if ready:\n    value = 0x10\n    name = 'a\\n'\n# comment\n",
        &tokens, &error), "basic source tokenizes");
    if (tokens.count == sizeof(expected) / sizeof(expected[0])) {
        for (index = 0; index < tokens.count; ++index) {
            passed &= check(tokens.items[index].kind == expected[index],
                            "token kind matches expected sequence");
        }
    } else {
        passed &= check(0, "token count matches expected sequence");
    }
    passed &= check(tokens.items[0].location.line == 1 &&
                    tokens.items[0].location.column == 1,
                    "first token location is tracked");
    passed &= check(tokens.items[7].location.length == 4,
                    "integer token span is tracked");
    py68_token_array_destroy(&allocator, &tokens);

    passed &= check(!tokenize(&allocator, "\tvalue = 1\n", &tokens, &error),
                    "leading tabs are rejected");
    passed &= check(error.kind == PY68_ERROR_TOKEN,
                    "tab rejection reports token error");
    py68_token_array_destroy(&allocator, &tokens);
    passed &= check(!tokenize(&allocator, "if x:\n    y = 1\n  z = 2\n",
                              &tokens, &error),
                    "inconsistent dedentation is rejected");
    py68_token_array_destroy(&allocator, &tokens);
    passed &= check(!tokenize(&allocator, "(1 + 2\n", &tokens, &error),
                    "unmatched delimiters are rejected");
    py68_token_array_destroy(&allocator, &tokens);
    passed &= check(tokenize(&allocator, "value = (1 +\n 2)\n",
                             &tokens, &error),
                    "newlines inside delimiters are ignored");
    passed &= check(tokens.items[0].kind == PY68_TOKEN_NAME &&
                    tokens.items[tokens.count - 1].kind == PY68_TOKEN_EOF,
                    "grouped expression has valid boundary tokens");
    py68_token_array_destroy(&allocator, &tokens);
    passed &= check(!tokenize(&allocator, ")\n", &tokens, &error),
                    "unmatched closing delimiters are rejected");
    py68_token_array_destroy(&allocator, &tokens);
    passed &= check(!tokenize(&allocator, "'\\q'\n", &tokens, &error),
                    "unknown escapes are rejected");
    py68_token_array_destroy(&allocator, &tokens);
    passed &= check(!tokenize(&allocator, "0x\n", &tokens, &error),
                    "malformed integer literals are rejected");
    py68_token_array_destroy(&allocator, &tokens);
    passed &= check(tokenize(&allocator, "class Thing:\n", &tokens, &error),
                    "class keyword tokenizes");
    passed &= check(tokens.items[0].kind == PY68_TOKEN_CLASS,
                    "class token is emitted");
    py68_token_array_destroy(&allocator, &tokens);
    passed &= check(tokenize(&allocator, "lambda x: x\n", &tokens, &error),
                    "unsupported keywords are classified, not discarded");
    passed &= check(tokens.items[0].kind == PY68_TOKEN_UNSUPPORTED_KEYWORD,
                    "unsupported keyword token is emitted");
    py68_token_array_destroy(&allocator, &tokens);
    passed &= check(tokenize(&allocator, "x is not None\n", &tokens, &error),
                    "is keyword tokenizes");
    passed &= check(tokens.items[1].kind == PY68_TOKEN_IS, "is token");
    passed &= check(tokens.items[2].kind == PY68_TOKEN_NOT,
                    "not remains a separate token after is");
    passed &= check(tokens.items[3].kind == PY68_TOKEN_NONE, "None token");
    py68_token_array_destroy(&allocator, &tokens);
    passed &= check(tokenize(&allocator, "f\"x={y}\"\n", &tokens, &error),
                    "f-string tokenizes");
    passed &= check(tokens.items[0].kind == PY68_TOKEN_FSTRING,
                    "f-string token kind");
    py68_token_array_destroy(&allocator, &tokens);
    passed &= check(tokenize(&allocator, "f = 1\n", &tokens, &error),
                    "bare f remains a name");
    passed &= check(tokens.items[0].kind == PY68_TOKEN_NAME,
                    "bare f is NAME");
    py68_token_array_destroy(&allocator, &tokens);
    passed &= check(!tokenize(&allocator, "fr\"x\"\n", &tokens, &error),
                    "fr prefix is rejected");
    passed &= check(error.kind == PY68_ERROR_TOKEN,
                    "fr rejection is a token error");
    py68_token_array_destroy(&allocator, &tokens);
    passed &= check(!tokenize(&allocator, "b\"x\"\n", &tokens, &error),
                    "b prefix is rejected");
    py68_token_array_destroy(&allocator, &tokens);
    passed &= check(tokenize(&allocator, "a.b + 1.5 / 2 // 3 {1}\n",
                             &tokens, &error),
                    "dot, float, slash, floor-div, and braces tokenize");
    passed &= check(tokens.items[1].kind == PY68_TOKEN_DOT, "dot token");
    passed &= check(tokens.items[4].kind == PY68_TOKEN_FLOAT, "float token");
    passed &= check(tokens.items[5].kind == PY68_TOKEN_SLASH, "slash token");
    passed &= check(tokens.items[7].kind == PY68_TOKEN_FLOOR_DIVIDE,
                    "floor divide token");
    passed &= check(tokens.items[9].kind == PY68_TOKEN_LEFT_BRACE,
                    "left brace token");
    py68_token_array_destroy(&allocator, &tokens);
    py68_allocator_initialize(&allocator);
    allocator.fail_after_allocation = 3;
    passed &= check(!tokenize(&allocator, "value = 1\n", &tokens, &error),
                    "token allocation failure is controlled");
    py68_token_array_destroy(&allocator, &tokens);
    passed &= check(allocator.stats.current_bytes == 0,
                    "tokenizer tests release all allocations");

    if (passed) {
        puts("PASS: tokenizer tests");
        return 0;
    }
    return 1;
}