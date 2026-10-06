/* 2026 by Piotr Rozentreter (Rozsoft) */

#include "py68k_symbol.h"

#include <stddef.h>
#include <string.h>

static int py68_symbol_equal(const Py68Source *source,
                             Py68U32 left_offset, Py68U16 left_length,
                             Py68U32 right_offset, Py68U16 right_length)
{
    return left_length == right_length &&
           memcmp(source->data + left_offset, source->data + right_offset,
                  left_length) == 0;
}

static void py68_symbol_error(Py68Error *error, Py68ErrorKind kind,
                              const Py68Source *source,
                              Py68Location location, const char *message)
{
    py68_error_set(error, kind, location, source->filename, message);
}

void py68_symbol_analysis_initialize(Py68SymbolAnalysis *analysis)
{
    analysis->functions = NULL;
    analysis->function_count = 0;
    analysis->function_capacity = 0;
    analysis->globals = NULL;
    analysis->global_count = 0;
    analysis->global_capacity = 0;
}

void py68_symbol_analysis_destroy(Py68Allocator *allocator,
                                  Py68SymbolAnalysis *analysis)
{
    Py68U16 index;
    for (index = 0; index < analysis->function_count; ++index) {
        py68_free(allocator, PY68_MEM_SYMBOL,
                  analysis->functions[index].parameters,
                  (Py68U32)analysis->functions[index].parameter_capacity *
                  (Py68U32)sizeof(Py68Symbol));
        py68_free(allocator, PY68_MEM_SYMBOL,
                  analysis->functions[index].locals,
                  (Py68U32)analysis->functions[index].local_capacity *
                  (Py68U32)sizeof(Py68Symbol));
    }
    py68_free(allocator, PY68_MEM_SYMBOL, analysis->functions,
              (Py68U32)analysis->function_capacity *
              (Py68U32)sizeof(Py68FunctionSymbols));
    py68_free(allocator, PY68_MEM_SYMBOL, analysis->globals,
              (Py68U32)analysis->global_capacity *
              (Py68U32)sizeof(Py68Symbol));
    py68_symbol_analysis_initialize(analysis);
}

static Py68Status py68_add_function(Py68Allocator *allocator,
                                    Py68SymbolAnalysis *analysis,
                                    Py68AstNode *node,
                                    Py68FunctionSymbols **out)
{
    Py68FunctionSymbols *replacement;
    Py68U16 capacity;
    Py68U32 old_size;
    Py68U32 new_size;

    if (analysis->function_count == analysis->function_capacity) {
        capacity = analysis->function_capacity == 0 ? 4 :
                   (Py68U16)(analysis->function_capacity * 2);
        old_size = (Py68U32)analysis->function_capacity *
                   (Py68U32)sizeof(Py68FunctionSymbols);
        new_size = (Py68U32)capacity * (Py68U32)sizeof(Py68FunctionSymbols);
        replacement = (Py68FunctionSymbols *)py68_realloc(
            allocator, PY68_MEM_SYMBOL, analysis->functions,
            old_size, new_size);
        if (replacement == NULL) return PY68_STATUS_MEMORY_ERROR;
        analysis->functions = replacement;
        analysis->function_capacity = capacity;
    }
    *out = &analysis->functions[analysis->function_count++];
    (*out)->function = node;
    (*out)->parameters = NULL;
    (*out)->parameter_count = 0;
    (*out)->parameter_capacity = 0;
    (*out)->locals = NULL;
    (*out)->local_count = 0;
    (*out)->local_capacity = 0;
    return PY68_STATUS_OK;
}

static Py68Status py68_add_symbol(Py68Allocator *allocator,
                                  Py68Symbol **items, Py68U16 *count,
                                  Py68U16 *capacity, Py68Symbol symbol)
{
    Py68Symbol *replacement;
    Py68U16 new_capacity;
    Py68U32 old_size;
    Py68U32 new_size;

    if (*count == *capacity) {
        new_capacity = *capacity == 0 ? 4 : (Py68U16)(*capacity * 2);
        old_size = (Py68U32)*capacity * (Py68U32)sizeof(Py68Symbol);
        new_size = (Py68U32)new_capacity * (Py68U32)sizeof(Py68Symbol);
        replacement = (Py68Symbol *)py68_realloc(
            allocator, PY68_MEM_SYMBOL, *items, old_size, new_size);
        if (replacement == NULL) return PY68_STATUS_MEMORY_ERROR;
        *items = replacement;
        *capacity = new_capacity;
    }
    (*items)[(*count)++] = symbol;
    return PY68_STATUS_OK;
}

static int py68_find_symbol(const Py68Source *source,
                            const Py68Symbol *items, Py68U16 count,
                            Py68U32 offset, Py68U16 length,
                            Py68U16 *index_out)
{
    Py68U16 index;
    for (index = 0; index < count; ++index) {
        if (py68_symbol_equal(source, items[index].offset, items[index].length,
                              offset, length)) {
            if (index_out != NULL) *index_out = index;
            return 1;
        }
    }
    return 0;
}

static Py68Status py68_add_parameter(Py68Allocator *allocator,
                                     const Py68Source *source,
                                     Py68FunctionSymbols *function,
                                     Py68AstNode *parameter,
                                     Py68Error *error)
{
    Py68Symbol symbol;
    Py68U16 index;
    if (py68_find_symbol(source, function->parameters,
                         function->parameter_count,
                         parameter->as.name.offset,
                         parameter->as.name.length, &index)) {
        py68_symbol_error(error, PY68_ERROR_SYNTAX, source,
                          parameter->location, "duplicate parameter name");
        return PY68_STATUS_SOURCE_ERROR;
    }
    symbol.offset = parameter->as.name.offset;
    symbol.length = parameter->as.name.length;
    symbol.slot = function->parameter_count;
    return py68_add_symbol(allocator, &function->parameters,
                           &function->parameter_count,
                           &function->parameter_capacity, symbol);
}

static Py68Status py68_add_local(Py68Allocator *allocator,
                                 const Py68Source *source,
                                 Py68FunctionSymbols *function,
                                 Py68U32 offset, Py68U16 length)
{
    Py68Symbol symbol;
    if (py68_find_symbol(source, function->parameters,
                         function->parameter_count, offset, length, NULL) ||
        py68_find_symbol(source, function->locals, function->local_count,
                         offset, length, NULL)) {
        return PY68_STATUS_OK;
    }
    if (function->parameter_count + function->local_count == 65535U) {
        return PY68_STATUS_MEMORY_ERROR;
    }
    symbol.offset = offset;
    symbol.length = length;
    symbol.slot = (Py68U16)(function->parameter_count + function->local_count);
    return py68_add_symbol(allocator, &function->locals,
                           &function->local_count,
                           &function->local_capacity, symbol);
}

static Py68Status py68_add_global(Py68Allocator *allocator,
                                  const Py68Source *source,
                                  Py68SymbolAnalysis *analysis,
                                  Py68U32 offset, Py68U16 length)
{
    Py68Symbol symbol;
    if (py68_find_symbol(source, analysis->globals, analysis->global_count,
                         offset, length, NULL)) {
        return PY68_STATUS_OK;
    }
    symbol.offset = offset;
    symbol.length = length;
    symbol.slot = analysis->global_count;
    return py68_add_symbol(allocator, &analysis->globals,
                           &analysis->global_count,
                           &analysis->global_capacity, symbol);
}

static Py68Status py68_bind_unpack_locals(Py68Allocator *allocator,
                                          const Py68Source *source,
                                          Py68FunctionSymbols *function,
                                          Py68AstNode *target)
{
    Py68U16 index;
    Py68AstNode *name;
    Py68Status status;
    if (target == NULL || target->kind != PY68_AST_TUPLE)
        return PY68_STATUS_OK;
    for (index = 0; index < target->as.list_literal.elements.count; ++index) {
        name = target->as.list_literal.elements.items[index];
        if (name == NULL || name->kind != PY68_AST_NAME) continue;
        status = py68_add_local(allocator, source, function,
                                name->as.name.offset, name->as.name.length);
        if (status != PY68_STATUS_OK) return status;
    }
    return PY68_STATUS_OK;
}

static Py68Status py68_bind_unpack_globals(Py68Allocator *allocator,
                                           const Py68Source *source,
                                           Py68SymbolAnalysis *analysis,
                                           Py68AstNode *target)
{
    Py68U16 index;
    Py68AstNode *name;
    Py68Status status;
    if (target == NULL || target->kind != PY68_AST_TUPLE)
        return PY68_STATUS_OK;
    for (index = 0; index < target->as.list_literal.elements.count; ++index) {
        name = target->as.list_literal.elements.items[index];
        if (name == NULL || name->kind != PY68_AST_NAME) continue;
        status = py68_add_global(allocator, source, analysis,
                                 name->as.name.offset, name->as.name.length);
        if (status != PY68_STATUS_OK) return status;
    }
    return PY68_STATUS_OK;
}

static Py68Status py68_collect_expression(Py68Allocator *allocator,
                                          const Py68Source *source,
                                          Py68FunctionSymbols *function,
                                          Py68AstNode *node)
{
    Py68U16 index;
    Py68Status status;
    if (node == NULL) return PY68_STATUS_OK;
    switch ((Py68AstKind)node->kind) {
    case PY68_AST_NAME:
        return PY68_STATUS_OK;
    case PY68_AST_UNARY:
        return py68_collect_expression(allocator, source, function,
                                       node->as.unary.operand);
    case PY68_AST_BINARY:
        status = py68_collect_expression(allocator, source, function,
                                          node->as.binary.left);
        if (status != PY68_STATUS_OK) return status;
        return py68_collect_expression(allocator, source, function,
                                       node->as.binary.right);
    case PY68_AST_IF_EXP:
        status = py68_collect_expression(allocator, source, function,
                                         node->as.if_exp.body);
        if (status != PY68_STATUS_OK) return status;
        status = py68_collect_expression(allocator, source, function,
                                         node->as.if_exp.condition);
        if (status != PY68_STATUS_OK) return status;
        return py68_collect_expression(allocator, source, function,
                                       node->as.if_exp.else_body);
    case PY68_AST_CALL:
        status = py68_collect_expression(allocator, source, function,
                                         node->as.call.callee);
        if (status != PY68_STATUS_OK) return status;
        for (index = 0; index < node->as.call.arguments.count; ++index) {
            status = py68_collect_expression(allocator, source, function,
                                             node->as.call.arguments.items[index]);
            if (status != PY68_STATUS_OK) return status;
        }
        return PY68_STATUS_OK;
    case PY68_AST_INDEX:
        status = py68_collect_expression(allocator, source, function,
                                          node->as.index.container);
        if (status != PY68_STATUS_OK) return status;
        return py68_collect_expression(allocator, source, function,
                                       node->as.index.index);
    case PY68_AST_SLICE:
        status = py68_collect_expression(allocator, source, function,
                                          node->as.slice.container);
        if (status != PY68_STATUS_OK) return status;
        status = py68_collect_expression(allocator, source, function,
                                          node->as.slice.start);
        if (status != PY68_STATUS_OK) return status;
        return py68_collect_expression(allocator, source, function,
                                       node->as.slice.end);
    case PY68_AST_LIST:
        for (index = 0; index < node->as.list_literal.elements.count; ++index) {
            status = py68_collect_expression(
                allocator, source, function,
                node->as.list_literal.elements.items[index]);
            if (status != PY68_STATUS_OK) return status;
        }
        return PY68_STATUS_OK;
    case PY68_AST_TUPLE:
    case PY68_AST_SET:
        for (index = 0; index < node->as.list_literal.elements.count; ++index) {
            status = py68_collect_expression(
                allocator, source, function,
                node->as.list_literal.elements.items[index]);
            if (status != PY68_STATUS_OK) return status;
        }
        return PY68_STATUS_OK;
    case PY68_AST_DICT:
        for (index = 0; index < node->as.dict_literal.keys.count; ++index) {
            status = py68_collect_expression(
                allocator, source, function,
                node->as.dict_literal.keys.items[index]);
            if (status != PY68_STATUS_OK) return status;
            status = py68_collect_expression(
                allocator, source, function,
                node->as.dict_literal.values.items[index]);
            if (status != PY68_STATUS_OK) return status;
        }
        return PY68_STATUS_OK;
    case PY68_AST_ATTRIBUTE:
        return py68_collect_expression(allocator, source, function,
                                       node->as.attribute.value);
    case PY68_AST_JOINED_STR:
        for (index = 0; index < node->as.joined_str.parts.count; ++index) {
            status = py68_collect_expression(
                allocator, source, function,
                node->as.joined_str.parts.items[index]);
            if (status != PY68_STATUS_OK) return status;
        }
        return PY68_STATUS_OK;
    case PY68_AST_FORMATTED_VALUE:
        status = py68_collect_expression(allocator, source, function,
                                         node->as.formatted_value.value);
        if (status != PY68_STATUS_OK) return status;
        return py68_collect_expression(allocator, source, function,
                                       node->as.formatted_value.format_spec);
    case PY68_AST_GENERATOR_EXP:
        /* A generator expression compiles to its own code object, so its loop
           variables never bind in this scope. Only the outermost iterable is
           evaluated here, at creation time (D-0046). */
        if (node->as.comprehension.generators.count == 0)
            return PY68_STATUS_OK;
        return py68_collect_expression(
            allocator, source, function,
            node->as.comprehension.generators.items[0]
                ->as.comprehension_for.iterable);
    case PY68_AST_LIST_COMP:
    case PY68_AST_SET_COMP:
    case PY68_AST_DICT_COMP: {
        Py68U16 generator_index;
        Py68U16 filter_index;
        status = py68_collect_expression(allocator, source, function,
                                         node->as.comprehension.elt);
        if (status != PY68_STATUS_OK) return status;
        status = py68_collect_expression(allocator, source, function,
                                         node->as.comprehension.value);
        if (status != PY68_STATUS_OK) return status;
        for (generator_index = 0;
             generator_index < node->as.comprehension.generators.count;
             ++generator_index) {
            Py68AstNode *clause =
                node->as.comprehension.generators.items[generator_index];
            if (function != NULL) {
                status = py68_add_local(
                    allocator, source, function,
                    clause->as.comprehension_for.name_offset,
                    clause->as.comprehension_for.name_length);
                if (status != PY68_STATUS_OK) return status;
            }
            status = py68_collect_expression(
                allocator, source, function,
                clause->as.comprehension_for.iterable);
            if (status != PY68_STATUS_OK) return status;
            for (filter_index = 0;
                 filter_index < clause->as.comprehension_for.ifs.count;
                 ++filter_index) {
                status = py68_collect_expression(
                    allocator, source, function,
                    clause->as.comprehension_for.ifs.items[filter_index]);
                if (status != PY68_STATUS_OK) return status;
            }
        }
        return PY68_STATUS_OK;
    }
    default:
        return PY68_STATUS_OK;
    }
}

static Py68Status py68_collect_statements(Py68Allocator *allocator,
                                          const Py68Source *source,
                                          Py68FunctionSymbols *function,
                                          Py68AstList *statements,
                                          Py68Error *error)
{
    Py68U16 index;
    Py68AstNode *statement;
    Py68Status status;
    for (index = 0; index < statements->count; ++index) {
        statement = statements->items[index];
        switch ((Py68AstKind)statement->kind) {
        case PY68_AST_ASSIGN:
            if (statement->as.assign.target != NULL &&
                statement->as.assign.target->kind == PY68_AST_TUPLE) {
                status = py68_bind_unpack_locals(
                    allocator, source, function,
                    statement->as.assign.target);
            } else if (statement->as.assign.target != NULL) {
                status = py68_collect_expression(allocator, source, function,
                                                 statement->as.assign.target);
            } else {
                status = py68_add_local(allocator, source, function,
                                        statement->as.assign.name_offset,
                                        statement->as.assign.name_length);
            }
            if (status != PY68_STATUS_OK) return status;
            status = py68_collect_expression(allocator, source, function,
                                             statement->as.assign.value);
            break;
        case PY68_AST_AUGMENTED_ASSIGN:
            if (statement->as.augmented_assign.target != NULL &&
                statement->as.augmented_assign.target->kind == PY68_AST_NAME) {
                status = py68_add_local(
                    allocator, source, function,
                    statement->as.augmented_assign.target->as.name.offset,
                    statement->as.augmented_assign.target->as.name.length);
                if (status != PY68_STATUS_OK) return status;
            } else if (statement->as.augmented_assign.target != NULL) {
                status = py68_collect_expression(
                    allocator, source, function,
                    statement->as.augmented_assign.target);
                if (status != PY68_STATUS_OK) return status;
            }
            status = py68_collect_expression(allocator, source, function,
                                             statement->as.augmented_assign.value);
            break;
        case PY68_AST_FOR:
            if (statement->as.for_statement.target != NULL) {
                status = py68_bind_unpack_locals(
                    allocator, source, function,
                    statement->as.for_statement.target);
            } else {
                status = py68_add_local(allocator, source, function,
                                        statement->as.for_statement.name_offset,
                                        statement->as.for_statement.name_length);
            }
            if (status == PY68_STATUS_OK) {
                status = py68_collect_expression(allocator, source, function,
                                                 statement->as.for_statement.iterable);
            }
            if (status == PY68_STATUS_OK) {
                status = py68_collect_statements(
                    allocator, source, function,
                    &statement->as.for_statement.body, error);
            }
            if (status == PY68_STATUS_OK) {
                status = py68_collect_statements(
                    allocator, source, function,
                    &statement->as.for_statement.else_body, error);
            }
            break;
        case PY68_AST_IF:
            status = py68_collect_expression(allocator, source, function,
                                              statement->as.if_statement.condition);
            if (status == PY68_STATUS_OK) {
                status = py68_collect_statements(
                    allocator, source, function,
                    &statement->as.if_statement.body, error);
            }
            if (status == PY68_STATUS_OK) {
                status = py68_collect_statements(
                    allocator, source, function,
                    &statement->as.if_statement.else_body, error);
            }
            break;
        case PY68_AST_WHILE:
            status = py68_collect_expression(allocator, source, function,
                                              statement->as.while_statement.condition);
            if (status == PY68_STATUS_OK) {
                status = py68_collect_statements(
                    allocator, source, function,
                    &statement->as.while_statement.body, error);
            }
            if (status == PY68_STATUS_OK) {
                status = py68_collect_statements(
                    allocator, source, function,
                    &statement->as.while_statement.else_body, error);
            }
            break;
        case PY68_AST_FUNCTION_DEF:
            py68_symbol_error(error, PY68_ERROR_SYNTAX, source,
                              statement->location,
                              "nested functions are not supported");
            return PY68_STATUS_SOURCE_ERROR;
        case PY68_AST_CLASS_DEF:
            py68_symbol_error(error, PY68_ERROR_SYNTAX, source,
                              statement->location,
                              "class inside function is not supported");
            return PY68_STATUS_SOURCE_ERROR;
        case PY68_AST_RETURN:
        case PY68_AST_YIELD:
            status = py68_collect_expression(allocator, source, function,
                                              statement->as.return_statement.value);
            break;
        case PY68_AST_EXPRESSION_STATEMENT:
            status = py68_collect_expression(allocator, source, function,
                                              statement->as.expression_statement.value);
            break;
        case PY68_AST_RAISE:
            status = py68_collect_expression(allocator, source, function,
                                              statement->as.raise_statement.value);
            break;
        case PY68_AST_TRY: {
            Py68U16 handler_index;
            status = py68_collect_statements(allocator, source, function,
                                             &statement->as.try_statement.body,
                                             error);
            for (handler_index = 0;
                 status == PY68_STATUS_OK &&
                 handler_index < statement->as.try_statement.handlers.count;
                 ++handler_index) {
                Py68AstNode *handler =
                    statement->as.try_statement.handlers.items[handler_index];
                if (handler->as.except_handler.as_length != 0)
                    status = py68_add_local(
                        allocator, source, function,
                        handler->as.except_handler.as_offset,
                        handler->as.except_handler.as_length);
                if (status == PY68_STATUS_OK)
                    status = py68_collect_expression(
                        allocator, source, function,
                        handler->as.except_handler.type);
                if (status == PY68_STATUS_OK)
                    status = py68_collect_statements(
                        allocator, source, function,
                        &handler->as.except_handler.body, error);
            }
            if (status == PY68_STATUS_OK)
                status = py68_collect_statements(
                    allocator, source, function,
                    &statement->as.try_statement.finally_body, error);
            break;
        }
        case PY68_AST_WITH:
            status = py68_collect_expression(
                allocator, source, function,
                statement->as.with_statement.context);
            if (status == PY68_STATUS_OK &&
                statement->as.with_statement.as_length != 0)
                status = py68_add_local(allocator, source, function,
                                        statement->as.with_statement.as_offset,
                                        statement->as.with_statement.as_length);
            if (status == PY68_STATUS_OK)
                status = py68_collect_statements(
                    allocator, source, function,
                    &statement->as.with_statement.body, error);
            break;
        case PY68_AST_IMPORT:
            if (statement->as.import_statement.as_length != 0)
                status = py68_add_local(allocator, source, function,
                                        statement->as.import_statement.as_offset,
                                        statement->as.import_statement.as_length);
            else
                status = py68_add_local(
                    allocator, source, function,
                    statement->as.import_statement.name_offset,
                    statement->as.import_statement.name_length);
            break;
        case PY68_AST_IMPORT_FROM: {
            Py68U16 alias_index;
            status = PY68_STATUS_OK;
            for (alias_index = 0;
                 status == PY68_STATUS_OK &&
                 alias_index < statement->as.import_from.names.count;
                 ++alias_index) {
                Py68AstNode *alias =
                    statement->as.import_from.names.items[alias_index];
                if (alias->as.import_alias.as_length != 0)
                    status = py68_add_local(allocator, source, function,
                                            alias->as.import_alias.as_offset,
                                            alias->as.import_alias.as_length);
                else
                    status = py68_add_local(allocator, source, function,
                                            alias->as.import_alias.name_offset,
                                            alias->as.import_alias.name_length);
            }
            break;
        }
        default:
            status = PY68_STATUS_OK;
            break;
        }
        if (status != PY68_STATUS_OK) return status;
    }
    return PY68_STATUS_OK;
}

static Py68Status py68_analyze_statement(Py68Allocator *allocator,
                                         const Py68Source *source,
                                         Py68AstNode *statement,
                                         Py68SymbolAnalysis *analysis,
                                         Py68Error *error)
{
    Py68FunctionSymbols *function;
    Py68U16 index;
    Py68Status status;
    if (statement->kind == PY68_AST_FUNCTION_DEF) {
        status = py68_add_global(allocator, source, analysis,
                                 statement->as.function_def.name_offset,
                                 statement->as.function_def.name_length);
        if (status != PY68_STATUS_OK) return status;
        status = py68_add_function(allocator, analysis, statement, &function);
        if (status != PY68_STATUS_OK) return status;
        for (index = 0; index < statement->as.function_def.parameters.count;
             ++index) {
            status = py68_add_parameter(
                allocator, source, function,
                statement->as.function_def.parameters.items[index], error);
            if (status != PY68_STATUS_OK) return status;
        }
        return py68_collect_statements(allocator, source, function,
                                       &statement->as.function_def.body, error);
    }
    if (statement->kind == PY68_AST_CLASS_DEF) {
        status = py68_add_global(allocator, source, analysis,
                                 statement->as.class_def.name_offset,
                                 statement->as.class_def.name_length);
        if (status != PY68_STATUS_OK) return status;
        for (index = 0; index < statement->as.class_def.body.count; ++index) {
            Py68AstNode *body_stmt = statement->as.class_def.body.items[index];
            if (body_stmt->kind == PY68_AST_PASS)
                continue;
            if (body_stmt->kind == PY68_AST_ASSIGN)
                continue;
            if (body_stmt->kind != PY68_AST_FUNCTION_DEF) {
                py68_symbol_error(error, PY68_ERROR_SYNTAX, source,
                                  body_stmt->location,
                                  "class body only allows pass, assignment, and def");
                return PY68_STATUS_SOURCE_ERROR;
            }
            /* Method names live in the class namespace, not module globals. */
            status = py68_add_function(allocator, analysis, body_stmt,
                                       &function);
            if (status != PY68_STATUS_OK) return status;
            {
                Py68U16 param_index;
                for (param_index = 0;
                     param_index < body_stmt->as.function_def.parameters.count;
                     ++param_index) {
                    status = py68_add_parameter(
                        allocator, source, function,
                        body_stmt->as.function_def.parameters.items[param_index],
                        error);
                    if (status != PY68_STATUS_OK) return status;
                }
            }
            status = py68_collect_statements(
                allocator, source, function,
                &body_stmt->as.function_def.body, error);
            if (status != PY68_STATUS_OK) return status;
        }
        return PY68_STATUS_OK;
    }
    if (statement->kind == PY68_AST_ASSIGN) {
        if (statement->as.assign.target != NULL &&
            statement->as.assign.target->kind == PY68_AST_TUPLE)
            return py68_bind_unpack_globals(allocator, source, analysis,
                                            statement->as.assign.target);
        if (statement->as.assign.target != NULL)
            return PY68_STATUS_OK;
        return py68_add_global(allocator, source, analysis,
                               statement->as.assign.name_offset,
                               statement->as.assign.name_length);
    }
    if (statement->kind == PY68_AST_AUGMENTED_ASSIGN &&
        statement->as.augmented_assign.target != NULL) {
        if (statement->as.augmented_assign.target->kind == PY68_AST_NAME)
            return py68_add_global(
                allocator, source, analysis,
                statement->as.augmented_assign.target->as.name.offset,
                statement->as.augmented_assign.target->as.name.length);
        return PY68_STATUS_OK;
    }
    if (statement->kind == PY68_AST_IMPORT) {
        if (statement->as.import_statement.as_length != 0)
            return py68_add_global(allocator, source, analysis,
                                   statement->as.import_statement.as_offset,
                                   statement->as.import_statement.as_length);
        return py68_add_global(allocator, source, analysis,
                               statement->as.import_statement.name_offset,
                               statement->as.import_statement.name_length);
    }
    if (statement->kind == PY68_AST_IMPORT_FROM) {
        Py68U16 alias_index;
        for (alias_index = 0;
             alias_index < statement->as.import_from.names.count;
             ++alias_index) {
            Py68AstNode *alias =
                statement->as.import_from.names.items[alias_index];
            if (alias->as.import_alias.as_length != 0)
                status = py68_add_global(allocator, source, analysis,
                                         alias->as.import_alias.as_offset,
                                         alias->as.import_alias.as_length);
            else
                status = py68_add_global(allocator, source, analysis,
                                         alias->as.import_alias.name_offset,
                                         alias->as.import_alias.name_length);
            if (status != PY68_STATUS_OK) return status;
        }
        return PY68_STATUS_OK;
    }
    if (statement->kind == PY68_AST_WITH &&
        statement->as.with_statement.as_length != 0)
        return py68_add_global(allocator, source, analysis,
                               statement->as.with_statement.as_offset,
                               statement->as.with_statement.as_length);
    if (statement->kind == PY68_AST_TRY) {
        Py68U16 handler_index;
        for (handler_index = 0;
             handler_index < statement->as.try_statement.handlers.count;
             ++handler_index) {
            Py68AstNode *handler =
                statement->as.try_statement.handlers.items[handler_index];
            if (handler->as.except_handler.as_length != 0) {
                status = py68_add_global(allocator, source, analysis,
                                         handler->as.except_handler.as_offset,
                                         handler->as.except_handler.as_length);
                if (status != PY68_STATUS_OK) return status;
            }
        }
    }
    return PY68_STATUS_OK;
}

Py68Status py68_symbol_analyze(Py68Allocator *allocator,
                               const Py68Source *source,
                               Py68AstNode *module,
                               Py68SymbolAnalysis *analysis,
                               Py68Error *error)
{
    Py68U16 index;
    Py68Status status;
    py68_symbol_analysis_initialize(analysis);
    py68_error_clear(error);
    for (index = 0; index < module->as.module.statements.count; ++index) {
        status = py68_analyze_statement(allocator, source,
                                        module->as.module.statements.items[index],
                                        analysis, error);
        if (status != PY68_STATUS_OK) return status;
    }
    return PY68_STATUS_OK;
}

Py68SymbolKind py68_symbol_classify(const Py68Source *source,
                                    const Py68FunctionSymbols *function,
                                    const Py68SymbolAnalysis *analysis,
                                    Py68U32 offset, Py68U16 length,
                                    const char *const *builtins,
                                    Py68U16 builtin_count)
{
    Py68U16 index;
    if (function != NULL &&
        (py68_find_symbol(source, function->parameters,
                          function->parameter_count, offset, length, &index) ||
         py68_find_symbol(source, function->locals, function->local_count,
                          offset, length, &index))) {
        return PY68_SYMBOL_LOCAL;
    }
    if (analysis != NULL &&
        py68_find_symbol(source, analysis->globals, analysis->global_count,
                         offset, length, &index)) {
        return PY68_SYMBOL_GLOBAL;
    }
    for (index = 0; index < builtin_count; ++index) {
        Py68U16 builtin_length = 0;
        while (builtins[index][builtin_length] != '\0') ++builtin_length;
        if (builtin_length == length &&
            memcmp(source->data + offset, builtins[index], length) == 0) {
            return PY68_SYMBOL_BUILTIN;
        }
    }
    return PY68_SYMBOL_UNDEFINED;
}

int py68_symbol_is_unbound(Py68U16 slot, Py68U16 parameter_count)
{
    return slot >= parameter_count;
}

int py68_symbol_lookup_local(const Py68Source *source,
                             const Py68FunctionSymbols *function,
                             Py68U32 offset, Py68U16 length,
                             Py68U16 *slot_out)
{
    Py68U16 index;
    if (function == NULL) return 0;
    if (py68_find_symbol(source, function->parameters,
                         function->parameter_count, offset, length,
                         &index)) {
        *slot_out = function->parameters[index].slot;
        return 1;
    }
    if (py68_find_symbol(source, function->locals, function->local_count,
                         offset, length, &index)) {
        *slot_out = function->locals[index].slot;
        return 1;
    }
    return 0;
}

void py68_symbol_scope_initialize(Py68FunctionSymbols *scope,
                                 Py68AstNode *node)
{
    scope->function = node;
    scope->parameters = NULL;
    scope->parameter_count = 0;
    scope->parameter_capacity = 0;
    scope->locals = NULL;
    scope->local_count = 0;
    scope->local_capacity = 0;
}

void py68_symbol_scope_destroy(Py68Allocator *allocator,
                               Py68FunctionSymbols *scope)
{
    py68_free(allocator, PY68_MEM_SYMBOL, scope->parameters,
              (Py68U32)scope->parameter_capacity * (Py68U32)sizeof(Py68Symbol));
    py68_free(allocator, PY68_MEM_SYMBOL, scope->locals,
              (Py68U32)scope->local_capacity * (Py68U32)sizeof(Py68Symbol));
    py68_symbol_scope_initialize(scope, scope->function);
}

Py68Status py68_symbol_scope_add_parameter(Py68Allocator *allocator,
                                          Py68FunctionSymbols *scope,
                                          Py68U32 offset, Py68U16 length)
{
    Py68Symbol symbol;
    if (scope->local_count != 0) return PY68_STATUS_INTERNAL_ERROR;
    if (scope->parameter_count == 65535U) return PY68_STATUS_MEMORY_ERROR;
    symbol.offset = offset;
    symbol.length = length;
    symbol.slot = scope->parameter_count;
    return py68_add_symbol(allocator, &scope->parameters,
                           &scope->parameter_count,
                           &scope->parameter_capacity, symbol);
}

Py68Status py68_symbol_scope_add_local(Py68Allocator *allocator,
                                      const Py68Source *source,
                                      Py68FunctionSymbols *scope,
                                      Py68U32 offset, Py68U16 length)
{
    return py68_add_local(allocator, source, scope, offset, length);
}

const Py68FunctionSymbols *py68_symbol_find_function(
    const Py68SymbolAnalysis *analysis, const Py68AstNode *function_def)
{
    Py68U16 index;
    if (analysis == NULL) return NULL;
    for (index = 0; index < analysis->function_count; ++index) {
        if (analysis->functions[index].function == function_def) {
            return &analysis->functions[index];
        }
    }
    return NULL;
}