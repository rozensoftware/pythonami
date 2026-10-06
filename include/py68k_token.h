/* 2026 by Piotr Rozentreter (Rozsoft) */

#ifndef PY68K_TOKEN_H
#define PY68K_TOKEN_H

#include "py68k_location.h"
#include "py68k_memory.h"

typedef enum Py68TokenKind {
    PY68_TOKEN_EOF = 0,
    PY68_TOKEN_NEWLINE,
    PY68_TOKEN_INDENT,
    PY68_TOKEN_DEDENT,
    PY68_TOKEN_NAME,
    PY68_TOKEN_INTEGER,
    PY68_TOKEN_FLOAT,
    PY68_TOKEN_STRING,
    PY68_TOKEN_FSTRING,
    PY68_TOKEN_LEFT_PAREN,
    PY68_TOKEN_RIGHT_PAREN,
    PY68_TOKEN_LEFT_BRACKET,
    PY68_TOKEN_RIGHT_BRACKET,
    PY68_TOKEN_LEFT_BRACE,
    PY68_TOKEN_RIGHT_BRACE,
    PY68_TOKEN_DOT,
    PY68_TOKEN_COLON,
    PY68_TOKEN_COMMA,
    PY68_TOKEN_PLUS,
    PY68_TOKEN_MINUS,
    PY68_TOKEN_STAR,
    PY68_TOKEN_SLASH,
    PY68_TOKEN_FLOOR_DIVIDE,
    PY68_TOKEN_PERCENT,
    PY68_TOKEN_ASSIGN,
    PY68_TOKEN_PLUS_ASSIGN,
    PY68_TOKEN_MINUS_ASSIGN,
    PY68_TOKEN_STAR_ASSIGN,
    PY68_TOKEN_SLASH_ASSIGN,
    PY68_TOKEN_FLOOR_DIVIDE_ASSIGN,
    PY68_TOKEN_PERCENT_ASSIGN,
    PY68_TOKEN_EQUAL,
    PY68_TOKEN_NOT_EQUAL,
    PY68_TOKEN_LESS,
    PY68_TOKEN_LESS_EQUAL,
    PY68_TOKEN_GREATER,
    PY68_TOKEN_GREATER_EQUAL,
    PY68_TOKEN_IF,
    PY68_TOKEN_ELIF,
    PY68_TOKEN_ELSE,
    PY68_TOKEN_WHILE,
    PY68_TOKEN_FOR,
    PY68_TOKEN_IN,
    /* Parser-only compound comparison; tokenizer never emits this. */
    PY68_TOKEN_NOT_IN,
    PY68_TOKEN_IS,
    /* Parser-only compound comparison; tokenizer never emits this. */
    PY68_TOKEN_IS_NOT,
    PY68_TOKEN_DEF,
    PY68_TOKEN_RETURN,
    PY68_TOKEN_BREAK,
    PY68_TOKEN_CONTINUE,
    PY68_TOKEN_PASS,
    PY68_TOKEN_AND,
    PY68_TOKEN_OR,
    PY68_TOKEN_NOT,
    PY68_TOKEN_TRUE,
    PY68_TOKEN_FALSE,
    PY68_TOKEN_NONE,
    PY68_TOKEN_TRY,
    PY68_TOKEN_EXCEPT,
    PY68_TOKEN_FINALLY,
    PY68_TOKEN_RAISE,
    PY68_TOKEN_WITH,
    PY68_TOKEN_IMPORT,
    PY68_TOKEN_FROM,
    PY68_TOKEN_AS,
    PY68_TOKEN_YIELD,
    PY68_TOKEN_CLASS,
    PY68_TOKEN_UNSUPPORTED_KEYWORD
} Py68TokenKind;

typedef struct Py68Token {
    Py68U16 kind;
    Py68U16 flags;
    Py68Location location;
} Py68Token;

typedef struct Py68TokenArray {
    Py68Token *items;
    Py68U32 count;
    Py68U32 capacity;
} Py68TokenArray;

void py68_token_array_initialize(Py68TokenArray *tokens);
void py68_token_array_destroy(Py68Allocator *allocator,
                              Py68TokenArray *tokens);

#endif