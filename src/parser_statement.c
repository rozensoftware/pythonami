/* 2026 by Piotr Rozentreter (Rozsoft) */

#include "py68k_parser.h"

#include <stddef.h>
#include <string.h>

static Py68Token *py68_statement_current(Py68StatementParser *parser)
{
    if (parser->expression.position >= parser->expression.tokens->count) {
        return NULL;
    }
    return &parser->expression.tokens->items[parser->expression.position];
}

static Py68Status py68_statement_error(Py68StatementParser *parser,
                                       Py68Token *token,
                                       const char *message)
{
    Py68Location location;
    if (token == NULL) {
        location.offset = parser->expression.source->length;
        location.line = 0;
        location.column = 0;
        location.length = 0;
    } else {
        location = token->location;
    }
    py68_error_set(parser->expression.error, PY68_ERROR_SYNTAX, location,
                   parser->expression.source->filename, message);
    return PY68_STATUS_SOURCE_ERROR;
}

static int py68_statement_accept(Py68StatementParser *parser,
                                 Py68TokenKind kind)
{
    Py68Token *token = py68_statement_current(parser);
    if (token != NULL && token->kind == kind) {
        ++parser->expression.position;
        return 1;
    }
    return 0;
}

static int py68_is_augassign_kind(Py68TokenKind kind)
{
    return kind == PY68_TOKEN_PLUS_ASSIGN ||
           kind == PY68_TOKEN_MINUS_ASSIGN ||
           kind == PY68_TOKEN_STAR_ASSIGN ||
           kind == PY68_TOKEN_SLASH_ASSIGN ||
           kind == PY68_TOKEN_FLOOR_DIVIDE_ASSIGN ||
           kind == PY68_TOKEN_PERCENT_ASSIGN;
}

static Py68Status py68_statement_new(Py68StatementParser *parser,
                                     Py68AstKind kind, Py68Token *token,
                                     Py68AstNode **node_out)
{
    return py68_ast_arena_new(parser->expression.arena, kind,
                              token->location, node_out);
}

static int py68_statement_needs_newline(Py68AstKind kind)
{
    return kind == PY68_AST_ASSIGN || kind == PY68_AST_AUGMENTED_ASSIGN ||
           kind == PY68_AST_RETURN || kind == PY68_AST_BREAK ||
           kind == PY68_AST_CONTINUE || kind == PY68_AST_PASS ||
           kind == PY68_AST_RAISE || kind == PY68_AST_IMPORT ||
           kind == PY68_AST_IMPORT_FROM || kind == PY68_AST_EXPRESSION_STATEMENT;
}

static int py68_statement_at_line_end(Py68StatementParser *parser)
{
    Py68Token *token = py68_statement_current(parser);
    return token == NULL || token->kind == PY68_TOKEN_NEWLINE ||
           token->kind == PY68_TOKEN_DEDENT || token->kind == PY68_TOKEN_EOF;
}

static int py68_ast_is_name_tuple(Py68AstNode *node)
{
    Py68U16 index;
    Py68AstNode *element;
    if (node == NULL || node->kind != PY68_AST_TUPLE) return 0;
    if (node->as.list_literal.elements.count == 0) return 0;
    for (index = 0; index < node->as.list_literal.elements.count; ++index) {
        element = node->as.list_literal.elements.items[index];
        if (element == NULL || element->kind != PY68_AST_NAME) return 0;
    }
    return 1;
}

static Py68Status py68_parse_name_node(Py68StatementParser *parser,
                                       Py68Token *token,
                                       Py68AstNode **node_out)
{
    Py68Status status = py68_statement_new(parser, PY68_AST_NAME, token,
                                           node_out);
    if (status != PY68_STATUS_OK) return status;
    (*node_out)->as.name.offset = token->location.offset;
    (*node_out)->as.name.length = token->location.length;
    return PY68_STATUS_OK;
}

static int py68_lookahead_unpack_assign(Py68StatementParser *parser)
{
    Py68U32 position = parser->expression.position;
    const Py68TokenArray *tokens = parser->expression.tokens;
    Py68TokenKind kind;
    if (position >= tokens->count) return 0;
    if (tokens->items[position].kind != PY68_TOKEN_NAME) return 0;
    ++position;
    if (position >= tokens->count ||
        tokens->items[position].kind != PY68_TOKEN_COMMA) return 0;
    while (position < tokens->count) {
        kind = tokens->items[position].kind;
        if (kind == PY68_TOKEN_COMMA) {
            ++position;
            continue;
        }
        if (kind == PY68_TOKEN_NAME) {
            ++position;
            continue;
        }
        if (kind == PY68_TOKEN_STAR)
            return 0;
        return kind == PY68_TOKEN_ASSIGN;
    }
    return 0;
}

static Py68Status py68_parse_unpack_name_tuple(Py68StatementParser *parser,
                                               Py68Token *first_token,
                                               Py68AstNode **node_out)
{
    Py68AstNode *tuple;
    Py68AstNode *name;
    Py68Token *token;
    Py68Status status;
    status = py68_statement_new(parser, PY68_AST_TUPLE, first_token, &tuple);
    if (status != PY68_STATUS_OK) return status;
    py68_ast_list_initialize(&tuple->as.list_literal.elements);
    status = py68_parse_name_node(parser, first_token, &name);
    if (status != PY68_STATUS_OK) return status;
    status = py68_ast_list_append(parser->expression.arena,
                                  &tuple->as.list_literal.elements, name);
    if (status != PY68_STATUS_OK) return status;
    while (py68_statement_accept(parser, PY68_TOKEN_COMMA)) {
        token = py68_statement_current(parser);
        if (token != NULL && token->kind == PY68_TOKEN_STAR)
            return py68_statement_error(parser, token,
                "starred unpacking is not supported by Python68K Language Level 0.6");
        if (token == NULL || token->kind != PY68_TOKEN_NAME) break;
        ++parser->expression.position;
        status = py68_parse_name_node(parser, token, &name);
        if (status != PY68_STATUS_OK) return status;
        status = py68_ast_list_append(parser->expression.arena,
                                      &tuple->as.list_literal.elements, name);
        if (status != PY68_STATUS_OK) return status;
    }
    if (tuple->as.list_literal.elements.count == 0)
        return py68_statement_error(parser, first_token,
                                    "invalid assignment target");
    *node_out = tuple;
    return PY68_STATUS_OK;
}

static Py68Status py68_parse_assignment_rhs(Py68StatementParser *parser,
                                            Py68AstNode **node_out)
{
    Py68AstNode *first;
    Py68AstNode *tuple;
    Py68AstNode *element;
    Py68Status status;
    status = py68_parse_expression(&parser->expression, &first);
    if (status != PY68_STATUS_OK) return status;
    if (!py68_statement_accept(parser, PY68_TOKEN_COMMA)) {
        *node_out = first;
        return PY68_STATUS_OK;
    }
    status = py68_ast_arena_new(parser->expression.arena, PY68_AST_TUPLE,
                                first->location, &tuple);
    if (status != PY68_STATUS_OK) return status;
    py68_ast_list_initialize(&tuple->as.list_literal.elements);
    status = py68_ast_list_append(parser->expression.arena,
                                  &tuple->as.list_literal.elements, first);
    if (status != PY68_STATUS_OK) return status;
    for (;;) {
        if (py68_statement_at_line_end(parser)) break;
        status = py68_parse_expression(&parser->expression, &element);
        if (status != PY68_STATUS_OK) return status;
        status = py68_ast_list_append(parser->expression.arena,
                                      &tuple->as.list_literal.elements,
                                      element);
        if (status != PY68_STATUS_OK) return status;
        if (!py68_statement_accept(parser, PY68_TOKEN_COMMA)) break;
    }
    *node_out = tuple;
    return PY68_STATUS_OK;
}

static Py68Status py68_parse_suite(Py68StatementParser *parser,
                                   Py68AstList *body);

static Py68Status py68_parse_statement(Py68StatementParser *parser,
                                        Py68AstNode **node_out)
{
    Py68Token *token = py68_statement_current(parser);
    Py68AstNode *node;
    Py68AstNode *chain;
    Py68AstNode *value;
    Py68Status status;

    if (token == NULL) return py68_statement_error(parser, token,
                                                   "expected statement");
    if (token->kind == PY68_TOKEN_UNSUPPORTED_KEYWORD) {
        const Py68U8 *text = parser->expression.source->data +
                             token->location.offset;
        if (token->location.length == 6 && memcmp(text, "lambda", 6) == 0)
            return py68_statement_error(parser, token,
                "lambda is not supported by Python68K Language Level 0.8");
        if (token->location.length == 6 && memcmp(text, "global", 6) == 0)
            return py68_statement_error(parser, token,
                "global is not supported by Python68K Language Level 0.8");
        if (token->location.length == 8 && memcmp(text, "nonlocal", 8) == 0)
            return py68_statement_error(parser, token,
                "nonlocal is not supported by Python68K Language Level 0.8");
        if (token->location.length == 5 && memcmp(text, "async", 5) == 0)
            return py68_statement_error(parser, token,
                "async is not supported by Python68K Language Level 0.8");
        if (token->location.length == 5 && memcmp(text, "await", 5) == 0)
            return py68_statement_error(parser, token,
                "await is not supported by Python68K Language Level 0.8");
        if (token->location.length == 5 && memcmp(text, "match", 5) == 0)
            return py68_statement_error(parser, token,
                "match is not supported by Python68K Language Level 0.8");
        if (token->location.length == 4 && memcmp(text, "case", 4) == 0)
            return py68_statement_error(parser, token,
                "case is not supported by Python68K Language Level 0.8");
        return py68_statement_error(parser, token,
            "this keyword is not supported by Python68K Language Level 0.8");
    }
    if (token->kind == PY68_TOKEN_IMPORT) {
        Py68Token *name_token;
        ++parser->expression.position;
        name_token = py68_statement_current(parser);
        if (name_token == NULL || name_token->kind != PY68_TOKEN_NAME)
            return py68_statement_error(parser, name_token,
                                        "expected module name");
        ++parser->expression.position;
        status = py68_statement_new(parser, PY68_AST_IMPORT, token, &node);
        if (status != PY68_STATUS_OK) return status;
        node->as.import_statement.name_offset = name_token->location.offset;
        node->as.import_statement.name_length = name_token->location.length;
        node->as.import_statement.as_offset = 0;
        node->as.import_statement.as_length = 0;
        if (py68_statement_accept(parser, PY68_TOKEN_AS)) {
            Py68Token *as_token = py68_statement_current(parser);
            if (as_token == NULL || as_token->kind != PY68_TOKEN_NAME)
                return py68_statement_error(parser, as_token,
                                            "expected import alias");
            ++parser->expression.position;
            node->as.import_statement.as_offset = as_token->location.offset;
            node->as.import_statement.as_length = as_token->location.length;
        }
        *node_out = node;
        return PY68_STATUS_OK;
    }
    if (token->kind == PY68_TOKEN_FROM) {
        Py68Token *module_token;
        ++parser->expression.position;
        if (py68_statement_current(parser) != NULL &&
            py68_statement_current(parser)->kind == PY68_TOKEN_DOT)
            return py68_statement_error(parser, py68_statement_current(parser),
                "relative imports are not supported by Python68K Language Level 0.6");
        module_token = py68_statement_current(parser);
        if (module_token == NULL || module_token->kind != PY68_TOKEN_NAME)
            return py68_statement_error(parser, module_token,
                                        "expected module name");
        ++parser->expression.position;
        if (!py68_statement_accept(parser, PY68_TOKEN_IMPORT))
            return py68_statement_error(parser, py68_statement_current(parser),
                                        "expected import");
        if (py68_statement_current(parser) != NULL &&
            py68_statement_current(parser)->kind == PY68_TOKEN_STAR)
            return py68_statement_error(parser, py68_statement_current(parser),
                "star import is not supported by Python68K Language Level 0.6");
        status = py68_statement_new(parser, PY68_AST_IMPORT_FROM, token, &node);
        if (status != PY68_STATUS_OK) return status;
        node->as.import_from.module_offset = module_token->location.offset;
        node->as.import_from.module_length = module_token->location.length;
        py68_ast_list_initialize(&node->as.import_from.names);
        for (;;) {
            Py68AstNode *alias;
            Py68Token *import_name = py68_statement_current(parser);
            if (import_name == NULL || import_name->kind != PY68_TOKEN_NAME)
                return py68_statement_error(parser, import_name,
                                            "expected imported name");
            ++parser->expression.position;
            status = py68_statement_new(parser, PY68_AST_IMPORT_ALIAS,
                                        import_name, &alias);
            if (status != PY68_STATUS_OK) return status;
            alias->as.import_alias.name_offset = import_name->location.offset;
            alias->as.import_alias.name_length = import_name->location.length;
            alias->as.import_alias.as_offset = 0;
            alias->as.import_alias.as_length = 0;
            if (py68_statement_accept(parser, PY68_TOKEN_AS)) {
                Py68Token *as_token = py68_statement_current(parser);
                if (as_token == NULL || as_token->kind != PY68_TOKEN_NAME)
                    return py68_statement_error(parser, as_token,
                                                "expected import alias");
                ++parser->expression.position;
                alias->as.import_alias.as_offset = as_token->location.offset;
                alias->as.import_alias.as_length = as_token->location.length;
            }
            status = py68_ast_list_append(parser->expression.arena,
                                          &node->as.import_from.names, alias);
            if (status != PY68_STATUS_OK) return status;
            if (!py68_statement_accept(parser, PY68_TOKEN_COMMA)) break;
        }
        *node_out = node;
        return PY68_STATUS_OK;
    }
    if (token->kind == PY68_TOKEN_RAISE) {
        ++parser->expression.position;
        status = py68_statement_new(parser, PY68_AST_RAISE, token, &node);
        if (status != PY68_STATUS_OK) return status;
        node->as.raise_statement.value = NULL;
        if (py68_statement_current(parser) != NULL &&
            py68_statement_current(parser)->kind != PY68_TOKEN_NEWLINE &&
            py68_statement_current(parser)->kind != PY68_TOKEN_DEDENT &&
            py68_statement_current(parser)->kind != PY68_TOKEN_EOF) {
            status = py68_parse_expression(&parser->expression,
                                           &node->as.raise_statement.value);
            if (status != PY68_STATUS_OK) return status;
        }
        *node_out = node;
        return PY68_STATUS_OK;
    }
    if (token->kind == PY68_TOKEN_WITH) {
        ++parser->expression.position;
        status = py68_statement_new(parser, PY68_AST_WITH, token, &node);
        if (status != PY68_STATUS_OK) return status;
        status = py68_parse_expression(&parser->expression,
                                       &node->as.with_statement.context);
        if (status != PY68_STATUS_OK) return status;
        node->as.with_statement.as_offset = 0;
        node->as.with_statement.as_length = 0;
        if (py68_statement_accept(parser, PY68_TOKEN_AS)) {
            Py68Token *as_token = py68_statement_current(parser);
            if (as_token == NULL || as_token->kind != PY68_TOKEN_NAME)
                return py68_statement_error(parser, as_token,
                                            "expected with target");
            ++parser->expression.position;
            node->as.with_statement.as_offset = as_token->location.offset;
            node->as.with_statement.as_length = as_token->location.length;
        }
        if (!py68_statement_accept(parser, PY68_TOKEN_COLON))
            return py68_statement_error(parser, py68_statement_current(parser),
                                        "expected with colon");
        py68_ast_list_initialize(&node->as.with_statement.body);
        status = py68_parse_suite(parser, &node->as.with_statement.body);
        if (status != PY68_STATUS_OK) return status;
        *node_out = node;
        return PY68_STATUS_OK;
    }
    if (token->kind == PY68_TOKEN_TRY) {
        ++parser->expression.position;
        if (!py68_statement_accept(parser, PY68_TOKEN_COLON))
            return py68_statement_error(parser, py68_statement_current(parser),
                                        "expected try colon");
        status = py68_statement_new(parser, PY68_AST_TRY, token, &node);
        if (status != PY68_STATUS_OK) return status;
        py68_ast_list_initialize(&node->as.try_statement.body);
        py68_ast_list_initialize(&node->as.try_statement.handlers);
        py68_ast_list_initialize(&node->as.try_statement.finally_body);
        status = py68_parse_suite(parser, &node->as.try_statement.body);
        if (status != PY68_STATUS_OK) return status;
        while (py68_statement_accept(parser, PY68_TOKEN_EXCEPT)) {
            Py68AstNode *handler;
            Py68Token *except_token = &parser->expression.tokens->items[
                parser->expression.position - 1];
            status = py68_statement_new(parser, PY68_AST_EXCEPT_HANDLER,
                                        except_token, &handler);
            if (status != PY68_STATUS_OK) return status;
            handler->as.except_handler.type = NULL;
            handler->as.except_handler.as_offset = 0;
            handler->as.except_handler.as_length = 0;
            py68_ast_list_initialize(&handler->as.except_handler.body);
            if (!py68_statement_accept(parser, PY68_TOKEN_COLON)) {
                status = py68_parse_expression(&parser->expression,
                    &handler->as.except_handler.type);
                if (status != PY68_STATUS_OK) return status;
                if (py68_statement_accept(parser, PY68_TOKEN_AS)) {
                    Py68Token *as_token = py68_statement_current(parser);
                    if (as_token == NULL || as_token->kind != PY68_TOKEN_NAME)
                        return py68_statement_error(parser, as_token,
                                                    "expected except name");
                    ++parser->expression.position;
                    handler->as.except_handler.as_offset =
                        as_token->location.offset;
                    handler->as.except_handler.as_length =
                        as_token->location.length;
                }
                if (!py68_statement_accept(parser, PY68_TOKEN_COLON))
                    return py68_statement_error(parser,
                        py68_statement_current(parser),
                        "expected except colon");
            }
            status = py68_parse_suite(parser, &handler->as.except_handler.body);
            if (status != PY68_STATUS_OK) return status;
            status = py68_ast_list_append(parser->expression.arena,
                                          &node->as.try_statement.handlers,
                                          handler);
            if (status != PY68_STATUS_OK) return status;
        }
        if (py68_statement_accept(parser, PY68_TOKEN_FINALLY)) {
            if (!py68_statement_accept(parser, PY68_TOKEN_COLON))
                return py68_statement_error(parser,
                    py68_statement_current(parser), "expected finally colon");
            status = py68_parse_suite(parser,
                                      &node->as.try_statement.finally_body);
            if (status != PY68_STATUS_OK) return status;
        }
        if (node->as.try_statement.handlers.count == 0 &&
            node->as.try_statement.finally_body.count == 0)
            return py68_statement_error(parser, token,
                                        "expected except or finally");
        *node_out = node;
        return PY68_STATUS_OK;
    }
    if (token->kind == PY68_TOKEN_IF) {
        Py68AstNode *condition;
        ++parser->expression.position;
        status = py68_parse_expression(&parser->expression, &condition);
        if (status != PY68_STATUS_OK || !py68_statement_accept(parser,
                                                               PY68_TOKEN_COLON)) {
            return py68_statement_error(parser, py68_statement_current(parser),
                                        "expected condition and colon");
        }
        status = py68_statement_new(parser, PY68_AST_IF, token, &node);
        if (status != PY68_STATUS_OK) return status;
        node->as.if_statement.condition = condition;
        py68_ast_list_initialize(&node->as.if_statement.body);
        py68_ast_list_initialize(&node->as.if_statement.else_body);
        chain = node;
        status = py68_parse_suite(parser, &node->as.if_statement.body);
        if (status != PY68_STATUS_OK) return status;
        while (py68_statement_accept(parser, PY68_TOKEN_ELIF)) {
            Py68AstNode *elif_node;
            Py68AstNode *elif_condition;
            Py68Token *elif_token = &parser->expression.tokens->items[
                parser->expression.position - 1];
            status = py68_parse_expression(&parser->expression,
                                           &elif_condition);
            if (status != PY68_STATUS_OK ||
                !py68_statement_accept(parser, PY68_TOKEN_COLON)) {
                return py68_statement_error(parser,
                                            py68_statement_current(parser),
                                            "expected elif condition and colon");
            }
            status = py68_statement_new(parser, PY68_AST_IF, elif_token,
                                        &elif_node);
            if (status != PY68_STATUS_OK) return status;
            elif_node->as.if_statement.condition = elif_condition;
            py68_ast_list_initialize(&elif_node->as.if_statement.body);
            py68_ast_list_initialize(&elif_node->as.if_statement.else_body);
            status = py68_parse_suite(parser, &elif_node->as.if_statement.body);
            if (status != PY68_STATUS_OK) return status;
            status = py68_ast_list_append(parser->expression.arena,
                                          &chain->as.if_statement.else_body,
                                          elif_node);
            if (status != PY68_STATUS_OK) return status;
            chain = elif_node;
        }
        if (py68_statement_accept(parser, PY68_TOKEN_ELSE)) {
            if (!py68_statement_accept(parser, PY68_TOKEN_COLON)) {
                return py68_statement_error(parser,
                                            py68_statement_current(parser),
                                            "expected else colon");
            }
            status = py68_parse_suite(parser, &chain->as.if_statement.else_body);
            if (status != PY68_STATUS_OK) return status;
        }
        *node_out = node;
        return PY68_STATUS_OK;
    }
    if (token->kind == PY68_TOKEN_WHILE) {
        ++parser->expression.position;
        status = py68_parse_expression(&parser->expression, &value);
        if (status != PY68_STATUS_OK || !py68_statement_accept(parser,
                                                               PY68_TOKEN_COLON)) {
            return py68_statement_error(parser, py68_statement_current(parser),
                                        "expected while condition and colon");
        }
        status = py68_statement_new(parser, PY68_AST_WHILE, token, &node);
        if (status != PY68_STATUS_OK) return status;
        node->as.while_statement.condition = value;
        py68_ast_list_initialize(&node->as.while_statement.body);
        py68_ast_list_initialize(&node->as.while_statement.else_body);
        ++parser->loop_depth;
        status = py68_parse_suite(parser, &node->as.while_statement.body);
        --parser->loop_depth;
        if (status != PY68_STATUS_OK) return status;
        if (py68_statement_accept(parser, PY68_TOKEN_ELSE)) {
            if (!py68_statement_accept(parser, PY68_TOKEN_COLON)) {
                return py68_statement_error(parser,
                                            py68_statement_current(parser),
                                            "expected else colon");
            }
            status = py68_parse_suite(parser, &node->as.while_statement.else_body);
            if (status != PY68_STATUS_OK) return status;
        }
        *node_out = node;
        return PY68_STATUS_OK;
    }
    if (token->kind == PY68_TOKEN_FOR) {
        Py68Token *name_token;
        Py68AstNode *for_target = NULL;
        Py68U32 name_offset = 0;
        Py68U16 name_length = 0;
        ++parser->expression.position;
        name_token = py68_statement_current(parser);
        if (name_token != NULL && name_token->kind == PY68_TOKEN_LEFT_PAREN) {
            /* Parse the parenthesized name list locally. Using the expression
               parser would consume `in` as a membership operator. */
            ++parser->expression.position;
            name_token = py68_statement_current(parser);
            if (name_token == NULL || name_token->kind != PY68_TOKEN_NAME)
                return py68_statement_error(parser, name_token,
                                            "invalid for target");
            ++parser->expression.position;
            if (py68_statement_current(parser) != NULL &&
                py68_statement_current(parser)->kind == PY68_TOKEN_COMMA) {
                status = py68_parse_unpack_name_tuple(parser, name_token,
                                                      &for_target);
                if (status != PY68_STATUS_OK) return status;
                name_offset = for_target->as.list_literal.elements.items[0]
                                  ->as.name.offset;
                name_length = for_target->as.list_literal.elements.items[0]
                                  ->as.name.length;
            } else {
                name_offset = name_token->location.offset;
                name_length = name_token->location.length;
            }
            if (!py68_statement_accept(parser, PY68_TOKEN_RIGHT_PAREN))
                return py68_statement_error(parser,
                                            py68_statement_current(parser),
                                            "expected closing parenthesis");
        } else if (name_token == NULL || name_token->kind != PY68_TOKEN_NAME) {
            return py68_statement_error(parser, name_token,
                                        "expected loop variable");
        } else {
            ++parser->expression.position;
            if (py68_statement_current(parser) != NULL &&
                py68_statement_current(parser)->kind == PY68_TOKEN_COMMA) {
                status = py68_parse_unpack_name_tuple(parser, name_token,
                                                      &for_target);
                if (status != PY68_STATUS_OK) return status;
                name_offset = for_target->as.list_literal.elements.items[0]
                                  ->as.name.offset;
                name_length = for_target->as.list_literal.elements.items[0]
                                  ->as.name.length;
            } else {
                name_offset = name_token->location.offset;
                name_length = name_token->location.length;
            }
        }
        if (!py68_statement_accept(parser, PY68_TOKEN_IN)) {
            return py68_statement_error(parser, py68_statement_current(parser),
                                        "expected in after loop variable");
        }
        status = py68_parse_expression(&parser->expression, &value);
        if (status != PY68_STATUS_OK || !py68_statement_accept(parser,
                                                               PY68_TOKEN_COLON)) {
            return py68_statement_error(parser, py68_statement_current(parser),
                                        "expected iterable and colon");
        }
        status = py68_statement_new(parser, PY68_AST_FOR, token, &node);
        if (status != PY68_STATUS_OK) return status;
        node->as.for_statement.name_offset = name_offset;
        node->as.for_statement.name_length = name_length;
        node->as.for_statement.target = for_target;
        node->as.for_statement.iterable = value;
        py68_ast_list_initialize(&node->as.for_statement.body);
        py68_ast_list_initialize(&node->as.for_statement.else_body);
        ++parser->loop_depth;
        status = py68_parse_suite(parser, &node->as.for_statement.body);
        --parser->loop_depth;
        if (status != PY68_STATUS_OK) return status;
        if (py68_statement_accept(parser, PY68_TOKEN_ELSE)) {
            if (!py68_statement_accept(parser, PY68_TOKEN_COLON)) {
                return py68_statement_error(parser,
                                            py68_statement_current(parser),
                                            "expected else colon");
            }
            status = py68_parse_suite(parser, &node->as.for_statement.else_body);
            if (status != PY68_STATUS_OK) return status;
        }
        *node_out = node;
        return PY68_STATUS_OK;
    }
    if (token->kind == PY68_TOKEN_DEF) {
        Py68Token *name_token;
        ++parser->expression.position;
        name_token = py68_statement_current(parser);
        if (name_token == NULL || name_token->kind != PY68_TOKEN_NAME) {
            return py68_statement_error(parser, name_token,
                                        "expected function name");
        }
        ++parser->expression.position;
        if (!py68_statement_accept(parser, PY68_TOKEN_LEFT_PAREN)) {
            return py68_statement_error(parser, py68_statement_current(parser),
                                        "expected opening parenthesis");
        }
        status = py68_statement_new(parser, PY68_AST_FUNCTION_DEF, token, &node);
        if (status != PY68_STATUS_OK) return status;
        node->as.function_def.name_offset = name_token->location.offset;
        node->as.function_def.name_length = name_token->location.length;
        py68_ast_list_initialize(&node->as.function_def.parameters);
        py68_ast_list_initialize(&node->as.function_def.body);
        if (!py68_statement_accept(parser, PY68_TOKEN_RIGHT_PAREN)) {
            for (;;) {
                Py68Token *parameter = py68_statement_current(parser);
                Py68AstNode *parameter_node;
                if (parameter == NULL || parameter->kind != PY68_TOKEN_NAME) {
                    return py68_statement_error(parser, parameter,
                                                "expected parameter name");
                }
                status = py68_ast_arena_new(parser->expression.arena,
                                            PY68_AST_NAME,
                                            parameter->location,
                                            &parameter_node);
                if (status != PY68_STATUS_OK) return status;
                parameter_node->as.name.offset = parameter->location.offset;
                parameter_node->as.name.length = parameter->location.length;
                ++parser->expression.position;
                status = py68_ast_list_append(parser->expression.arena,
                                              &node->as.function_def.parameters,
                                              parameter_node);
                if (status != PY68_STATUS_OK) return status;
                if (py68_statement_accept(parser, PY68_TOKEN_RIGHT_PAREN)) break;
                if (!py68_statement_accept(parser, PY68_TOKEN_COMMA)) {
                    return py68_statement_error(parser,
                                                py68_statement_current(parser),
                                                "expected parameter comma");
                }
                if (py68_statement_accept(parser, PY68_TOKEN_RIGHT_PAREN)) break;
            }
        }
        if (!py68_statement_accept(parser, PY68_TOKEN_COLON)) {
            return py68_statement_error(parser, py68_statement_current(parser),
                                        "expected function colon");
        }
        {
            Py68U16 old_inside = parser->inside_function;
            Py68U16 old_loop = parser->loop_depth;
            parser->inside_function = 1;
            parser->loop_depth = 0;
            status = py68_parse_suite(parser, &node->as.function_def.body);
            parser->inside_function = old_inside;
            parser->loop_depth = old_loop;
        }
        if (status != PY68_STATUS_OK) return status;
        *node_out = node;
        return PY68_STATUS_OK;
    }
    if (token->kind == PY68_TOKEN_CLASS) {
        Py68Token *name_token;
        Py68AstNode *base = NULL;
        Py68U16 body_index;
        ++parser->expression.position;
        name_token = py68_statement_current(parser);
        if (name_token == NULL || name_token->kind != PY68_TOKEN_NAME) {
            return py68_statement_error(parser, name_token,
                                        "expected class name");
        }
        ++parser->expression.position;
        if (py68_statement_accept(parser, PY68_TOKEN_LEFT_PAREN)) {
            if (!py68_statement_accept(parser, PY68_TOKEN_RIGHT_PAREN)) {
                status = py68_parse_expression(&parser->expression, &base);
                if (status != PY68_STATUS_OK) return status;
                if (py68_statement_accept(parser, PY68_TOKEN_COMMA)) {
                    return py68_statement_error(
                        parser, py68_statement_current(parser),
                        "multiple base classes are not supported");
                }
                if (!py68_statement_accept(parser, PY68_TOKEN_RIGHT_PAREN)) {
                    return py68_statement_error(
                        parser, py68_statement_current(parser),
                        "expected closing parenthesis");
                }
            }
        }
        if (!py68_statement_accept(parser, PY68_TOKEN_COLON)) {
            return py68_statement_error(parser, py68_statement_current(parser),
                                        "expected class colon");
        }
        status = py68_statement_new(parser, PY68_AST_CLASS_DEF, token, &node);
        if (status != PY68_STATUS_OK) return status;
        node->as.class_def.name_offset = name_token->location.offset;
        node->as.class_def.name_length = name_token->location.length;
        node->as.class_def.base = base;
        py68_ast_list_initialize(&node->as.class_def.body);
        status = py68_parse_suite(parser, &node->as.class_def.body);
        if (status != PY68_STATUS_OK) return status;
        for (body_index = 0; body_index < node->as.class_def.body.count;
             ++body_index) {
            Py68AstNode *body_stmt = node->as.class_def.body.items[body_index];
            if (body_stmt->kind == PY68_AST_PASS)
                continue;
            if (body_stmt->kind == PY68_AST_FUNCTION_DEF)
                continue;
            if (body_stmt->kind == PY68_AST_ASSIGN &&
                body_stmt->as.assign.target == NULL)
                continue;
            py68_error_set(parser->expression.error, PY68_ERROR_SYNTAX,
                           body_stmt->location,
                           parser->expression.source->filename,
                           "class body only allows pass, assignment, and def");
            return PY68_STATUS_SOURCE_ERROR;
        }
        *node_out = node;
        return PY68_STATUS_OK;
    }
    if (token->kind == PY68_TOKEN_RETURN) {
        ++parser->expression.position;
        if (!parser->inside_function) {
            return py68_statement_error(parser, token,
                                        "return outside function");
        }
        status = py68_statement_new(parser, PY68_AST_RETURN, token, &node);
        if (status != PY68_STATUS_OK) return status;
        if (py68_statement_current(parser)->kind == PY68_TOKEN_NEWLINE) {
            value = NULL;
        } else {
            status = py68_parse_assignment_rhs(parser, &value);
            if (status != PY68_STATUS_OK) return status;
        }
        node->as.return_statement.value = value;
        *node_out = node;
        return PY68_STATUS_OK;
    }
    if (token->kind == PY68_TOKEN_YIELD) {
        ++parser->expression.position;
        if (!parser->inside_function) {
            return py68_statement_error(parser, token,
                                        "yield outside function");
        }
        status = py68_statement_new(parser, PY68_AST_YIELD, token, &node);
        if (status != PY68_STATUS_OK) return status;
        if (py68_statement_current(parser)->kind == PY68_TOKEN_NEWLINE) {
            value = NULL;
        } else {
            status = py68_parse_expression(&parser->expression, &value);
            if (status != PY68_STATUS_OK) return status;
        }
        node->as.return_statement.value = value;
        *node_out = node;
        return PY68_STATUS_OK;
    }
    if (token->kind == PY68_TOKEN_BREAK || token->kind == PY68_TOKEN_CONTINUE) {
        Py68AstKind kind = token->kind == PY68_TOKEN_BREAK ?
                           PY68_AST_BREAK : PY68_AST_CONTINUE;
        if (parser->loop_depth == 0) {
            return py68_statement_error(parser, token,
                                        "loop control outside loop");
        }
        ++parser->expression.position;
        status = py68_statement_new(parser, kind, token, &node);
        *node_out = node;
        return status;
    }
    if (token->kind == PY68_TOKEN_PASS) {
        ++parser->expression.position;
        status = py68_statement_new(parser, PY68_AST_PASS, token, &node);
        *node_out = node;
        return status;
    }
    if (token->kind == PY68_TOKEN_NAME &&
        parser->expression.position + 1 < parser->expression.tokens->count) {
        Py68TokenKind next_kind = parser->expression.tokens->items[
            parser->expression.position + 1].kind;
        if (next_kind == PY68_TOKEN_ASSIGN ||
            next_kind == PY68_TOKEN_PLUS_ASSIGN ||
            next_kind == PY68_TOKEN_MINUS_ASSIGN ||
            next_kind == PY68_TOKEN_STAR_ASSIGN ||
            next_kind == PY68_TOKEN_SLASH_ASSIGN ||
            next_kind == PY68_TOKEN_FLOOR_DIVIDE_ASSIGN ||
            next_kind == PY68_TOKEN_PERCENT_ASSIGN) {
            Py68Token target_token = *token;
            ++parser->expression.position;
            ++parser->expression.position;
            if (next_kind == PY68_TOKEN_ASSIGN) {
                status = py68_parse_assignment_rhs(parser, &value);
                if (status != PY68_STATUS_OK) return status;
                status = py68_statement_new(parser, PY68_AST_ASSIGN,
                                            &target_token, &node);
                if (status != PY68_STATUS_OK) return status;
                node->as.assign.name_offset = target_token.location.offset;
                node->as.assign.name_length = target_token.location.length;
                node->as.assign.target = NULL;
                node->as.assign.value = value;
            } else {
                Py68AstNode *target;
                status = py68_parse_expression(&parser->expression, &value);
                if (status != PY68_STATUS_OK) return status;
                status = py68_statement_new(parser, PY68_AST_AUGMENTED_ASSIGN,
                                            &target_token, &node);
                if (status != PY68_STATUS_OK) return status;
                status = py68_ast_arena_new(parser->expression.arena,
                                            PY68_AST_NAME,
                                            target_token.location, &target);
                if (status != PY68_STATUS_OK) return status;
                target->as.name.offset = target_token.location.offset;
                target->as.name.length = target_token.location.length;
                node->as.augmented_assign.operator_kind = next_kind;
                node->as.augmented_assign.target = target;
                node->as.augmented_assign.value = value;
            }
            *node_out = node;
            return PY68_STATUS_OK;
        }
        if (next_kind == PY68_TOKEN_COMMA &&
            py68_lookahead_unpack_assign(parser)) {
            Py68Token first_token = *token;
            Py68AstNode *target;
            ++parser->expression.position;
            status = py68_parse_unpack_name_tuple(parser, &first_token,
                                                  &target);
            if (status != PY68_STATUS_OK) return status;
            if (!py68_statement_accept(parser, PY68_TOKEN_ASSIGN))
                return py68_statement_error(parser,
                                            py68_statement_current(parser),
                                            "expected assignment");
            status = py68_parse_assignment_rhs(parser, &value);
            if (status != PY68_STATUS_OK) return status;
            status = py68_statement_new(parser, PY68_AST_ASSIGN,
                                        &first_token, &node);
            if (status != PY68_STATUS_OK) return status;
            node->as.assign.name_offset = 0;
            node->as.assign.name_length = 0;
            node->as.assign.target = target;
            node->as.assign.value = value;
            *node_out = node;
            return PY68_STATUS_OK;
        }
        if (next_kind == PY68_TOKEN_COMMA) {
            Py68Token *star;
            Py68U32 position = parser->expression.position + 1;
            Py68TokenKind scan;
            while (position < parser->expression.tokens->count) {
                scan = parser->expression.tokens->items[position].kind;
                if (scan == PY68_TOKEN_STAR) {
                    star = &parser->expression.tokens->items[position];
                    return py68_statement_error(parser, star,
                        "starred unpacking is not supported by Python68K Language Level 0.6");
                }
                if (scan == PY68_TOKEN_NEWLINE || scan == PY68_TOKEN_DEDENT ||
                    scan == PY68_TOKEN_EOF || scan == PY68_TOKEN_ASSIGN)
                    break;
                ++position;
            }
        }
    }
    status = py68_parse_expression(&parser->expression, &value);
    if (status != PY68_STATUS_OK) return status;
    if (py68_statement_accept(parser, PY68_TOKEN_ASSIGN)) {
        if (value->kind == PY68_AST_NAME) {
            status = py68_statement_new(parser, PY68_AST_ASSIGN, token, &node);
            if (status != PY68_STATUS_OK) return status;
            node->as.assign.name_offset = value->as.name.offset;
            node->as.assign.name_length = value->as.name.length;
            node->as.assign.target = NULL;
            node->as.assign.value = NULL;
            status = py68_parse_assignment_rhs(parser, &node->as.assign.value);
            if (status != PY68_STATUS_OK) return status;
            *node_out = node;
            return PY68_STATUS_OK;
        }
        if (value->kind == PY68_AST_ATTRIBUTE ||
            value->kind == PY68_AST_INDEX) {
            status = py68_statement_new(parser, PY68_AST_ASSIGN, token, &node);
            if (status != PY68_STATUS_OK) return status;
            node->as.assign.name_offset = 0;
            node->as.assign.name_length = 0;
            node->as.assign.target = value;
            status = py68_parse_assignment_rhs(parser, &node->as.assign.value);
            if (status != PY68_STATUS_OK) return status;
            *node_out = node;
            return PY68_STATUS_OK;
        }
        if (py68_ast_is_name_tuple(value)) {
            status = py68_statement_new(parser, PY68_AST_ASSIGN, token, &node);
            if (status != PY68_STATUS_OK) return status;
            node->as.assign.name_offset = 0;
            node->as.assign.name_length = 0;
            node->as.assign.target = value;
            status = py68_parse_assignment_rhs(parser, &node->as.assign.value);
            if (status != PY68_STATUS_OK) return status;
            *node_out = node;
            return PY68_STATUS_OK;
        }
        return py68_statement_error(parser, py68_statement_current(parser),
                                    "invalid assignment target");
    }
    {
        Py68Token *current = py68_statement_current(parser);
        if (current != NULL && py68_is_augassign_kind(current->kind)) {
            Py68TokenKind operator_kind = current->kind;
            Py68AstNode *rhs;
            if (value->kind != PY68_AST_NAME &&
                value->kind != PY68_AST_INDEX &&
                value->kind != PY68_AST_ATTRIBUTE) {
                return py68_statement_error(parser, current,
                    "invalid augmented assignment target");
            }
            ++parser->expression.position;
            status = py68_parse_expression(&parser->expression, &rhs);
            if (status != PY68_STATUS_OK) return status;
            status = py68_statement_new(parser, PY68_AST_AUGMENTED_ASSIGN,
                                        token, &node);
            if (status != PY68_STATUS_OK) return status;
            node->as.augmented_assign.operator_kind = (Py68U16)operator_kind;
            node->as.augmented_assign.target = value;
            node->as.augmented_assign.value = rhs;
            *node_out = node;
            return PY68_STATUS_OK;
        }
    }
    status = py68_statement_new(parser, PY68_AST_EXPRESSION_STATEMENT,
                                token, &node);
    if (status == PY68_STATUS_OK) node->as.expression_statement.value = value;
    *node_out = node;
    return status;
}

static Py68Status py68_parse_suite(Py68StatementParser *parser,
                                   Py68AstList *body)
{
    Py68AstNode *statement;
    Py68Status status;
    if (py68_statement_accept(parser, PY68_TOKEN_NEWLINE)) {
        if (!py68_statement_accept(parser, PY68_TOKEN_INDENT)) {
            return py68_statement_error(parser, py68_statement_current(parser),
                                        "expected indented suite");
        }
        while (py68_statement_current(parser) != NULL &&
               py68_statement_current(parser)->kind != PY68_TOKEN_DEDENT &&
               py68_statement_current(parser)->kind != PY68_TOKEN_EOF) {
            if (py68_statement_accept(parser, PY68_TOKEN_NEWLINE)) continue;
            status = py68_parse_statement(parser, &statement);
            if (status != PY68_STATUS_OK) return status;
            status = py68_ast_list_append(parser->expression.arena, body,
                                          statement);
            if (status != PY68_STATUS_OK) return status;
            if (!py68_statement_accept(parser, PY68_TOKEN_NEWLINE) &&
                py68_statement_current(parser) != NULL &&
                py68_statement_current(parser)->kind != PY68_TOKEN_DEDENT &&
                py68_statement_needs_newline(statement->kind)) {
                return py68_statement_error(parser,
                                            py68_statement_current(parser),
                                            "expected statement newline");
            }
        }
        if (!py68_statement_accept(parser, PY68_TOKEN_DEDENT)) {
            return py68_statement_error(parser, py68_statement_current(parser),
                                        "expected dedent");
        }
        return PY68_STATUS_OK;
    }
    status = py68_parse_statement(parser, &statement);
    if (status != PY68_STATUS_OK) return status;
    status = py68_ast_list_append(parser->expression.arena, body, statement);
    if (status != PY68_STATUS_OK) return status;
    if (py68_statement_needs_newline(statement->kind) &&
        !py68_statement_accept(parser, PY68_TOKEN_NEWLINE)) {
        return py68_statement_error(parser, py68_statement_current(parser),
                                    "expected suite newline");
    }
    return PY68_STATUS_OK;
}

Py68Status py68_parse_module(Py68StatementParser *parser,
                             Py68AstNode **module_out)
{
    Py68AstNode *module;
    Py68AstNode *statement;
    Py68Token token;
    Py68Status status;

    token = *py68_statement_current(parser);
    status = py68_ast_arena_new(parser->expression.arena, PY68_AST_MODULE,
                                token.location, &module);
    if (status != PY68_STATUS_OK) return status;
    py68_ast_list_initialize(&module->as.module.statements);
    while (py68_statement_current(parser) != NULL &&
           py68_statement_current(parser)->kind != PY68_TOKEN_EOF) {
        if (py68_statement_accept(parser, PY68_TOKEN_NEWLINE)) continue;
        status = py68_parse_statement(parser, &statement);
        if (status != PY68_STATUS_OK) return status;
        status = py68_ast_list_append(parser->expression.arena,
                                      &module->as.module.statements,
                                      statement);
        if (status != PY68_STATUS_OK) return status;
        if (py68_statement_needs_newline(statement->kind) &&
            !py68_statement_accept(parser, PY68_TOKEN_NEWLINE)) {
            return py68_statement_error(parser,
                                        py68_statement_current(parser),
                                        "expected statement newline");
        }
    }
    *module_out = module;
    return PY68_STATUS_OK;
}