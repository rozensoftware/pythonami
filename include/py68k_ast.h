/* 2026 by Piotr Rozentreter (Rozsoft) */

#ifndef PY68K_AST_H
#define PY68K_AST_H

#include "py68k_location.h"
#include "py68k_memory.h"
#include "py68k_types.h"

typedef enum Py68AstKind {
    PY68_AST_MODULE,
    PY68_AST_ASSIGN,
    PY68_AST_AUGMENTED_ASSIGN,
    PY68_AST_IF,
    PY68_AST_WHILE,
    PY68_AST_FOR,
    PY68_AST_FUNCTION_DEF,
    PY68_AST_CLASS_DEF,
    PY68_AST_RETURN,
    PY68_AST_YIELD,
    PY68_AST_BREAK,
    PY68_AST_CONTINUE,
    PY68_AST_PASS,
    PY68_AST_TRY,
    PY68_AST_EXCEPT_HANDLER,
    PY68_AST_RAISE,
    PY68_AST_WITH,
    PY68_AST_IMPORT,
    PY68_AST_IMPORT_FROM,
    PY68_AST_IMPORT_ALIAS,
    PY68_AST_EXPRESSION_STATEMENT,
    PY68_AST_INTEGER,
    PY68_AST_FLOAT,
    PY68_AST_STRING,
    PY68_AST_JOINED_STR,
    PY68_AST_FORMATTED_VALUE,
    PY68_AST_BOOL,
    PY68_AST_NONE,
    PY68_AST_NAME,
    PY68_AST_UNARY,
    PY68_AST_BINARY,
    PY68_AST_IF_EXP,
    PY68_AST_CALL,
    PY68_AST_INDEX,
    PY68_AST_SLICE,
    PY68_AST_ATTRIBUTE,
    PY68_AST_LIST,
    PY68_AST_TUPLE,
    PY68_AST_SET,
    PY68_AST_DICT,
    PY68_AST_LIST_COMP,
    PY68_AST_SET_COMP,
    PY68_AST_DICT_COMP,
    PY68_AST_GENERATOR_EXP,
    PY68_AST_COMP_FOR
} Py68AstKind;

typedef struct Py68AstNode Py68AstNode;

typedef struct Py68AstList {
    Py68AstNode **items;
    Py68U16 count;
    Py68U16 capacity;
} Py68AstList;

struct Py68AstNode {
    Py68U16 kind;
    Py68U16 flags;
    Py68Location location;
    union {
        struct { Py68AstList statements; } module;
        struct { Py68U32 name_offset; Py68U16 name_length;
                 Py68AstNode *target; /* NULL = simple name; INDEX/ATTR = store;
                                         TUPLE of NAME = unpack */
                 Py68AstNode *value; } assign;
        struct { Py68U16 operator_kind; Py68AstNode *target;
                 Py68AstNode *value; } augmented_assign;
        struct { Py68AstNode *condition; Py68AstList body;
                 Py68AstList else_body; } if_statement;
        struct { Py68AstNode *condition; Py68AstList body;
                 Py68AstList else_body; } while_statement;
        struct { Py68U32 name_offset; Py68U16 name_length;
                 Py68AstNode *target; /* NULL = simple NAME; TUPLE of NAME = unpack */
                 Py68AstNode *iterable; Py68AstList body;
                 Py68AstList else_body; } for_statement;
        struct { Py68U32 name_offset; Py68U16 name_length;
                 Py68AstList parameters; Py68AstList body; } function_def;
        struct { Py68U32 name_offset; Py68U16 name_length;
                 Py68AstNode *base; /* NULL = object */
                 Py68AstList body; } class_def;
        /* Shared by PY68_AST_RETURN and PY68_AST_YIELD; NULL value means a
           bare `return` or `yield`. */
        struct { Py68AstNode *value; } return_statement;
        struct { Py68AstList body; Py68AstList handlers;
                 Py68AstList finally_body; } try_statement;
        struct { Py68AstNode *type; Py68U32 as_offset; Py68U16 as_length;
                 Py68AstList body; } except_handler;
        struct { Py68AstNode *value; } raise_statement;
        struct { Py68AstNode *context; Py68U32 as_offset; Py68U16 as_length;
                 Py68AstList body; } with_statement;
        struct { Py68U32 name_offset; Py68U16 name_length;
                 Py68U32 as_offset; Py68U16 as_length; } import_statement;
        struct { Py68U32 module_offset; Py68U16 module_length;
                 Py68AstList names; } import_from;
        struct { Py68U32 name_offset; Py68U16 name_length;
                 Py68U32 as_offset; Py68U16 as_length; } import_alias;
        struct { Py68AstNode *value; } expression_statement;
        struct { Py68I32 value; } integer_literal;
        struct { Py68U32 bits; } float_literal;
        struct { Py68U32 offset; Py68U16 length; Py68U16 quote_flags; }
            string_literal;
        struct { Py68AstList parts; } joined_str;
        struct { Py68AstNode *value; Py68U16 conversion; /* 0, 's','r','a' */
                 Py68AstNode *format_spec; /* PY68_AST_STRING or NULL */ }
            formatted_value;
        struct { Py68U32 offset; Py68U16 length; } name;
        struct { Py68U16 value; } boolean_literal;
        struct { Py68U16 operator_kind; Py68AstNode *operand; } unary;
        struct { Py68U16 operator_kind; Py68AstNode *left;
                 Py68AstNode *right; } binary;
        struct { Py68AstNode *body; Py68AstNode *condition;
                 Py68AstNode *else_body; } if_exp;
        struct { Py68AstNode *callee; Py68AstList arguments; } call;
        struct { Py68AstNode *container; Py68AstNode *index; } index;
        struct { Py68AstNode *container; Py68AstNode *start;
                 Py68AstNode *end; } slice;
        struct { Py68AstNode *value; Py68U32 name_offset;
                 Py68U16 name_length; } attribute;
        struct { Py68AstList elements; } list_literal;
        struct { Py68AstList keys; Py68AstList values; } dict_literal;
        struct { Py68AstNode *elt; Py68AstNode *value;
                 Py68AstList generators; } comprehension;
        struct { Py68U32 name_offset; Py68U16 name_length;
                 Py68AstNode *iterable; Py68AstList ifs; } comprehension_for;
    } as;
};

void py68_ast_list_initialize(Py68AstList *list);
void py68_ast_list_destroy(Py68Allocator *allocator, Py68AstList *list);

#endif