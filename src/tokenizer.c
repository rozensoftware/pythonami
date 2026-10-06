/* 2026 by Piotr Rozentreter (Rozsoft) */

#include "py68k_tokenizer.h"

#include <stddef.h>

static int py68_is_digit(Py68U8 character)
{
    return character >= (Py68U8)'0' && character <= (Py68U8)'9';
}

static int py68_is_hex_digit(Py68U8 character)
{
    return (character >= (Py68U8)'0' && character <= (Py68U8)'9') ||
           (character >= (Py68U8)'a' && character <= (Py68U8)'f') ||
           (character >= (Py68U8)'A' && character <= (Py68U8)'F');
}

static int py68_is_name_start(Py68U8 character)
{
    return (character >= (Py68U8)'a' && character <= (Py68U8)'z') ||
           (character >= (Py68U8)'A' && character <= (Py68U8)'Z') ||
           character == (Py68U8)'_';
}

static int py68_is_name_continue(Py68U8 character)
{
    return py68_is_name_start(character) || py68_is_digit(character);
}

static Py68U32 py68_token_size(Py68U32 count)
{
    if (count > (Py68U32)~(Py68U32)0 / (Py68U32)sizeof(Py68Token)) {
        return 0;
    }
    return count * (Py68U32)sizeof(Py68Token);
}

static Py68Status py68_emit(Py68Allocator *allocator,
                            Py68TokenArray *tokens,
                            Py68TokenKind kind,
                            Py68Location location)
{
    Py68Token *replacement;
    Py68U32 old_size;
    Py68U32 new_capacity;
    Py68U32 new_size;

    if (tokens->count == tokens->capacity) {
        new_capacity = tokens->capacity == 0 ? 32 : tokens->capacity * 2;
        if (new_capacity < tokens->capacity ||
            (new_size = py68_token_size(new_capacity)) == 0) {
            return PY68_STATUS_MEMORY_ERROR;
        }
        old_size = py68_token_size(tokens->capacity);
        replacement = (Py68Token *)py68_realloc(
            allocator, PY68_MEM_TOKEN, tokens->items, old_size, new_size);
        if (replacement == NULL) {
            return PY68_STATUS_MEMORY_ERROR;
        }
        tokens->items = replacement;
        tokens->capacity = new_capacity;
    }
    tokens->items[tokens->count].kind = (Py68U16)kind;
    tokens->items[tokens->count].flags = 0;
    tokens->items[tokens->count].location = location;
    ++tokens->count;
    return PY68_STATUS_OK;
}

static void py68_advance(Py68U32 *position, Py68U16 *column)
{
    ++*position;
    ++*column;
}

static void py68_advance_newline(const Py68U8 *data, Py68U32 length,
                                 Py68U32 *position, Py68U32 *line,
                                 Py68U16 *column)
{
    if (data[*position] == (Py68U8)'\r' &&
        *position + 1 < length && data[*position + 1] == (Py68U8)'\n') {
        *position += 2;
    } else {
        ++*position;
    }
    ++*line;
    *column = 1;
}

static void py68_set_token_error(Py68Error *error, Py68ErrorKind kind,
                                 Py68Location location,
                                 const Py68Source *source,
                                 const char *message)
{
    py68_error_set(error, kind, location, source->filename, message);
}

static Py68TokenKind py68_keyword(const Py68U8 *data, Py68U32 start,
                                  Py68U32 length)
{
    static const char *names[] = {
        "if", "elif", "else", "while", "for", "in", "def",
        "return", "break", "continue", "pass", "and", "or", "not",
        "True", "False", "None", "try", "except", "finally", "raise",
        "with", "import", "from", "as", "is", "yield",
        "class", "lambda", "nonlocal",
        "async", "await", "match", "case", "global", "eval",
        "exec"
    };
    static const Py68TokenKind kinds[] = {
        PY68_TOKEN_IF, PY68_TOKEN_ELIF, PY68_TOKEN_ELSE,
        PY68_TOKEN_WHILE, PY68_TOKEN_FOR, PY68_TOKEN_IN, PY68_TOKEN_DEF,
        PY68_TOKEN_RETURN, PY68_TOKEN_BREAK, PY68_TOKEN_CONTINUE,
        PY68_TOKEN_PASS, PY68_TOKEN_AND, PY68_TOKEN_OR, PY68_TOKEN_NOT,
        PY68_TOKEN_TRUE, PY68_TOKEN_FALSE, PY68_TOKEN_NONE,
        PY68_TOKEN_TRY, PY68_TOKEN_EXCEPT, PY68_TOKEN_FINALLY,
        PY68_TOKEN_RAISE, PY68_TOKEN_WITH, PY68_TOKEN_IMPORT,
        PY68_TOKEN_FROM, PY68_TOKEN_AS, PY68_TOKEN_IS, PY68_TOKEN_YIELD,
        PY68_TOKEN_CLASS, PY68_TOKEN_UNSUPPORTED_KEYWORD,
        PY68_TOKEN_UNSUPPORTED_KEYWORD, PY68_TOKEN_UNSUPPORTED_KEYWORD,
        PY68_TOKEN_UNSUPPORTED_KEYWORD, PY68_TOKEN_UNSUPPORTED_KEYWORD,
        PY68_TOKEN_UNSUPPORTED_KEYWORD, PY68_TOKEN_UNSUPPORTED_KEYWORD,
        PY68_TOKEN_UNSUPPORTED_KEYWORD, PY68_TOKEN_UNSUPPORTED_KEYWORD
    };
    Py68U16 index;

    for (index = 0; index < (Py68U16)(sizeof(names) / sizeof(names[0]));
         ++index) {
        Py68U32 candidate_length = 0;
        while (names[index][candidate_length] != '\0') {
            ++candidate_length;
        }
        if (candidate_length == length) {
            Py68U32 offset;
            int equal = 1;
            for (offset = 0; offset < length; ++offset) {
                if (data[start + offset] != (Py68U8)names[index][offset]) {
                    equal = 0;
                    break;
                }
            }
            if (equal) {
                return kinds[index];
            }
        }
    }
    return PY68_TOKEN_NAME;
}

static int py68_validate_integer(const Py68U8 *data, Py68U32 start,
                                 Py68U32 end)
{
    Py68U32 position = start;
    Py68U32 base = 10;
    Py68U32 value = 0;
    Py68U32 digit;
    Py68U32 limit = 2147483648UL;
    int previous_underscore = 0;
    int has_digit = 0;

    if (end - start >= 2 && data[start] == (Py68U8)'0') {
        if (data[start + 1] == (Py68U8)'x' ||
            data[start + 1] == (Py68U8)'X') {
            base = 16;
            position += 2;
        } else if (data[start + 1] == (Py68U8)'b' ||
                   data[start + 1] == (Py68U8)'B') {
            base = 2;
            position += 2;
        } else if (data[start + 1] == (Py68U8)'o' ||
                   data[start + 1] == (Py68U8)'O') {
            base = 8;
            position += 2;
        } else if (py68_is_digit(data[start + 1])) {
            return 0;
        }
    }
    while (position < end) {
        if (data[position] == (Py68U8)'_') {
            if (!has_digit || previous_underscore) {
                return 0;
            }
            previous_underscore = 1;
            ++position;
            continue;
        }
        if (data[position] >= (Py68U8)'0' &&
            data[position] <= (Py68U8)'9') {
            digit = (Py68U32)(data[position] - (Py68U8)'0');
        } else if (data[position] >= (Py68U8)'a' &&
                   data[position] <= (Py68U8)'f') {
            digit = (Py68U32)(data[position] - (Py68U8)'a') + 10;
        } else if (data[position] >= (Py68U8)'A' &&
                   data[position] <= (Py68U8)'F') {
            digit = (Py68U32)(data[position] - (Py68U8)'A') + 10;
        } else {
            return 0;
        }
        if (digit >= base || value > (limit - digit) / base) {
            return 0;
        }
        value = value * base + digit;
        has_digit = 1;
        previous_underscore = 0;
        ++position;
    }
    return has_digit && !previous_underscore;
}

static int py68_is_valid_escape(Py68U8 character)
{
    return character == (Py68U8)'\\' || character == (Py68U8)'\'' ||
           character == (Py68U8)'"' || character == (Py68U8)'n' ||
           character == (Py68U8)'r' || character == (Py68U8)'t' ||
           character == (Py68U8)'x';
}

/* On entry, *position is at the opening quote. On success, *position is past
   the closing quote. Returns 1 on success, 0 on unterminated/invalid content. */
static int py68_scan_quoted_string(const Py68U8 *data, Py68U32 length,
                                   Py68U32 *position, Py68U16 *column)
{
    Py68U8 quote = data[*position];
    py68_advance(position, column);
    while (*position < length && data[*position] != quote) {
        if (data[*position] == (Py68U8)'\n' ||
            data[*position] == (Py68U8)'\r') {
            return 0;
        }
        if (data[*position] == (Py68U8)'\\') {
            py68_advance(position, column);
            if (*position == length ||
                !py68_is_valid_escape(data[*position])) {
                return 0;
            }
            if (data[*position] == (Py68U8)'x') {
                py68_advance(position, column);
                if (*position + 1 >= length ||
                    !py68_is_hex_digit(data[*position]) ||
                    !py68_is_hex_digit(data[*position + 1])) {
                    return 0;
                }
                py68_advance(position, column);
            }
        }
        py68_advance(position, column);
    }
    if (*position == length || data[*position] != quote) {
        return 0;
    }
    py68_advance(position, column);
    return 1;
}

static Py68Status py68_grow_bytes(Py68Allocator *allocator, void **items,
                                   Py68U32 *capacity, Py68U32 item_size)
{
    Py68U32 old_size = *capacity * item_size;
    Py68U32 new_capacity = *capacity == 0 ? 16 : *capacity * 2;
    Py68U32 new_size;
    void *replacement;

    if (new_capacity < *capacity ||
        new_capacity > (Py68U32)~(Py68U32)0 / item_size) {
        return PY68_STATUS_MEMORY_ERROR;
    }
    new_size = new_capacity * item_size;
    replacement = py68_realloc(allocator, PY68_MEM_TOKEN, *items,
                               old_size, new_size);
    if (replacement == NULL) {
        return PY68_STATUS_MEMORY_ERROR;
    }
    *items = replacement;
    *capacity = new_capacity;
    return PY68_STATUS_OK;
}

Py68Status py68_tokenize(Py68Allocator *allocator,
                         const Py68Source *source,
                         Py68TokenArray *tokens,
                         Py68Error *error)
{
    Py68U16 *indents = NULL;
    Py68U32 indent_capacity = 0;
    Py68U32 indent_count = 0;
    Py68U8 *groups = NULL;
    Py68U32 group_capacity = 0;
    Py68U32 group_count = 0;
    Py68U32 position = 0;
    Py68U32 line = 1;
    Py68U16 column = 1;
    int at_line_start = 1;
    int content_since_newline = 0;
    Py68Status status = PY68_STATUS_OK;

    py68_token_array_initialize(tokens);
    py68_error_clear(error);
    if (py68_grow_bytes(allocator, (void **)&indents, &indent_capacity,
                        (Py68U32)sizeof(Py68U16)) != PY68_STATUS_OK) {
        return PY68_STATUS_MEMORY_ERROR;
    }
    indents[indent_count++] = 0;

    while (position < source->length) {
        Py68U8 character = source->data[position];
        Py68Location location;
        Py68U32 start;
        Py68U32 start_line;
        Py68U16 start_column;
        Py68TokenKind kind;

        if (at_line_start && group_count == 0) {
            Py68U32 width = 0;
            while (position < source->length &&
                   source->data[position] == (Py68U8)' ') {
                ++position;
                ++column;
                ++width;
            }
            if (width > 65535UL) {
                location.offset = position;
                location.line = line;
                location.column = column;
                location.length = 0;
                py68_set_token_error(error, PY68_ERROR_TOKEN, location, source,
                                     "indentation is too wide");
                status = PY68_STATUS_SOURCE_ERROR;
                break;
            }
            if (position == source->length) {
                break;
            }
            if (source->data[position] == (Py68U8)'\t') {
                location.offset = position;
                location.line = line;
                location.column = column;
                location.length = 1;
                py68_set_token_error(error, PY68_ERROR_TOKEN, location, source,
                                     "tabs are not allowed in indentation");
                status = PY68_STATUS_SOURCE_ERROR;
                break;
            }
            if (source->data[position] == (Py68U8)'#') {
                while (position < source->length &&
                       source->data[position] != (Py68U8)'\n' &&
                       source->data[position] != (Py68U8)'\r') {
                    ++position;
                    ++column;
                }
                continue;
            }
            if (source->data[position] == (Py68U8)'\n' ||
                source->data[position] == (Py68U8)'\r') {
                py68_advance_newline(source->data, source->length,
                                     &position, &line, &column);
                continue;
            }
            if (width > indents[indent_count - 1]) {
                if (indent_count == indent_capacity &&
                    py68_grow_bytes(allocator, (void **)&indents,
                                    &indent_capacity,
                                    (Py68U32)sizeof(Py68U16)) !=
                        PY68_STATUS_OK) {
                    status = PY68_STATUS_MEMORY_ERROR;
                    break;
                }
                indents[indent_count++] = (Py68U16)width;
                location.offset = position;
                location.line = line;
                location.column = 1;
                location.length = 0;
                status = py68_emit(allocator, tokens, PY68_TOKEN_INDENT,
                                   location);
                if (status != PY68_STATUS_OK) {
                    break;
                }
            } else {
                while (width < indents[indent_count - 1]) {
                    --indent_count;
                    location.offset = position;
                    location.line = line;
                    location.column = 1;
                    location.length = 0;
                    status = py68_emit(allocator, tokens, PY68_TOKEN_DEDENT,
                                       location);
                    if (status != PY68_STATUS_OK) {
                        break;
                    }
                }
                if (status != PY68_STATUS_OK ||
                    width != indents[indent_count - 1]) {
                    location.offset = position;
                    location.line = line;
                    location.column = 1;
                    location.length = 0;
                    py68_set_token_error(error, PY68_ERROR_TOKEN, location,
                                         source, "inconsistent dedentation");
                    status = PY68_STATUS_SOURCE_ERROR;
                    break;
                }
            }
            at_line_start = 0;
            content_since_newline = 0;
            continue;
        }

        if (character == (Py68U8)'\n' || character == (Py68U8)'\r') {
            if (group_count == 0) {
                location.offset = position;
                location.line = line;
                location.column = column;
                location.length = character == (Py68U8)'\r' &&
                                   position + 1 < source->length &&
                                   source->data[position + 1] == (Py68U8)'\n'
                                   ? 2 : 1;
                if (content_since_newline) {
                    status = py68_emit(allocator, tokens,
                                       PY68_TOKEN_NEWLINE, location);
                }
                content_since_newline = 0;
            }
            py68_advance_newline(source->data, source->length,
                                 &position, &line, &column);
            at_line_start = 1;
            if (status != PY68_STATUS_OK) {
                break;
            }
            continue;
        }
        if (character == (Py68U8)' ' || character == (Py68U8)'\t') {
            py68_advance(&position, &column);
            continue;
        }
        if (character == (Py68U8)'#') {
            while (position < source->length &&
                   source->data[position] != (Py68U8)'\n' &&
                   source->data[position] != (Py68U8)'\r') {
                py68_advance(&position, &column);
            }
            continue;
        }

        start = position;
        start_line = line;
        start_column = column;
        kind = PY68_TOKEN_EOF;
        if (py68_is_name_start(character)) {
            Py68U8 next;
            Py68U8 next2;
            int handled_string_prefix = 0;

            if (position + 1 < source->length) {
                next = source->data[position + 1];
                if ((character == (Py68U8)'f' || character == (Py68U8)'F') &&
                    (next == (Py68U8)'\'' || next == (Py68U8)'"')) {
                    /* f"..." / f'...' — scan like a normal string. */
                    py68_advance(&position, &column); /* past f/F */
                    if (!py68_scan_quoted_string(source->data, source->length,
                                                 &position, &column)) {
                        location.offset = start;
                        location.line = start_line;
                        location.column = start_column;
                        location.length = (Py68U16)(position - start);
                        py68_set_token_error(
                            error, PY68_ERROR_TOKEN, location, source,
                            "unterminated or invalid string literal");
                        status = PY68_STATUS_SOURCE_ERROR;
                        break;
                    }
                    kind = PY68_TOKEN_FSTRING;
                    handled_string_prefix = 1;
                } else {
                    int unsupported = 0;
                    if ((character == (Py68U8)'f' ||
                         character == (Py68U8)'F') &&
                        (next == (Py68U8)'r' || next == (Py68U8)'R') &&
                        position + 2 < source->length) {
                        next2 = source->data[position + 2];
                        if (next2 == (Py68U8)'\'' || next2 == (Py68U8)'"') {
                            unsupported = 1;
                        }
                    } else if ((character == (Py68U8)'r' ||
                                character == (Py68U8)'R') &&
                               (next == (Py68U8)'f' ||
                                next == (Py68U8)'F') &&
                               position + 2 < source->length) {
                        next2 = source->data[position + 2];
                        if (next2 == (Py68U8)'\'' || next2 == (Py68U8)'"') {
                            unsupported = 1;
                        }
                    } else if ((character == (Py68U8)'b' ||
                                character == (Py68U8)'B' ||
                                character == (Py68U8)'u' ||
                                character == (Py68U8)'U') &&
                               (next == (Py68U8)'\'' ||
                                next == (Py68U8)'"')) {
                        unsupported = 1;
                    }
                    if (unsupported) {
                        location.offset = start;
                        location.line = start_line;
                        location.column = start_column;
                        location.length = 1;
                        py68_set_token_error(error, PY68_ERROR_TOKEN,
                                             location, source,
                                             "unsupported string prefix");
                        status = PY68_STATUS_SOURCE_ERROR;
                        break;
                    }
                }
            }
            if (!handled_string_prefix) {
                while (position < source->length &&
                       py68_is_name_continue(source->data[position])) {
                    py68_advance(&position, &column);
                }
                kind = py68_keyword(source->data, start, position - start);
            }
        } else if (py68_is_digit(character) ||
                   (character == (Py68U8)'.' && position + 1 < source->length &&
                    py68_is_digit(source->data[position + 1]))) {
            int is_float = 0;
            int is_prefixed = 0;
            if (character == (Py68U8)'.') {
                is_float = 1;
                py68_advance(&position, &column);
                while (position < source->length &&
                       (py68_is_digit(source->data[position]) ||
                        source->data[position] == (Py68U8)'_')) {
                    py68_advance(&position, &column);
                }
            } else {
                if (character == (Py68U8)'0' && position + 1 < source->length) {
                    Py68U8 next = source->data[position + 1];
                    if (next == (Py68U8)'x' || next == (Py68U8)'X' ||
                        next == (Py68U8)'b' || next == (Py68U8)'B' ||
                        next == (Py68U8)'o' || next == (Py68U8)'O') {
                        is_prefixed = 1;
                    }
                }
                if (is_prefixed) {
                    while (position < source->length &&
                           (py68_is_name_continue(source->data[position]) ||
                            source->data[position] == (Py68U8)'_')) {
                        py68_advance(&position, &column);
                    }
                } else {
                    while (position < source->length &&
                           (py68_is_digit(source->data[position]) ||
                            source->data[position] == (Py68U8)'_')) {
                        py68_advance(&position, &column);
                    }
                    if (position < source->length &&
                        source->data[position] == (Py68U8)'.' &&
                        position + 1 < source->length &&
                        py68_is_digit(source->data[position + 1])) {
                        is_float = 1;
                        py68_advance(&position, &column);
                        while (position < source->length &&
                               (py68_is_digit(source->data[position]) ||
                                source->data[position] == (Py68U8)'_')) {
                            py68_advance(&position, &column);
                        }
                    }
                }
            }
            if (!is_prefixed && position < source->length &&
                (source->data[position] == (Py68U8)'e' ||
                 source->data[position] == (Py68U8)'E')) {
                is_float = 1;
                py68_advance(&position, &column);
                if (position < source->length &&
                    (source->data[position] == (Py68U8)'+' ||
                     source->data[position] == (Py68U8)'-'))
                    py68_advance(&position, &column);
                while (position < source->length &&
                       py68_is_digit(source->data[position]))
                    py68_advance(&position, &column);
            }
            if (is_float) {
                kind = PY68_TOKEN_FLOAT;
            } else if (!py68_validate_integer(source->data, start, position)) {
                location.offset = start;
                location.line = start_line;
                location.column = start_column;
                location.length = (Py68U16)(position - start);
                py68_set_token_error(error, PY68_ERROR_TOKEN, location, source,
                                     "invalid or overflowing integer literal");
                status = PY68_STATUS_SOURCE_ERROR;
                break;
            } else {
                kind = PY68_TOKEN_INTEGER;
            }
        } else if (character == (Py68U8)'\'' ||
                   character == (Py68U8)'"') {
            if (!py68_scan_quoted_string(source->data, source->length,
                                         &position, &column)) {
                location.offset = start;
                location.line = start_line;
                location.column = start_column;
                location.length = (Py68U16)(position - start);
                py68_set_token_error(error, PY68_ERROR_TOKEN, location, source,
                                     "unterminated or invalid string literal");
                status = PY68_STATUS_SOURCE_ERROR;
                break;
            }
            kind = PY68_TOKEN_STRING;
        } else {
            Py68U32 width = 1;
            if (character == (Py68U8)'/' && position + 2 < source->length &&
                source->data[position + 1] == (Py68U8)'/' &&
                source->data[position + 2] == (Py68U8)'=') {
                kind = PY68_TOKEN_FLOOR_DIVIDE_ASSIGN; width = 3;
            } else if (character == (Py68U8)'/' && position + 1 < source->length &&
                source->data[position + 1] == (Py68U8)'/') {
                kind = PY68_TOKEN_FLOOR_DIVIDE;
                width = 2;
            } else if (character == (Py68U8)'/' && position + 1 < source->length &&
                       source->data[position + 1] == (Py68U8)'=') {
                kind = PY68_TOKEN_SLASH_ASSIGN; width = 2;
            } else if (character == (Py68U8)'+' && position + 1 < source->length &&
                       source->data[position + 1] == (Py68U8)'=') {
                kind = PY68_TOKEN_PLUS_ASSIGN; width = 2;
            } else if (character == (Py68U8)'-' && position + 1 < source->length &&
                       source->data[position + 1] == (Py68U8)'=') {
                kind = PY68_TOKEN_MINUS_ASSIGN; width = 2;
            } else if (character == (Py68U8)'*' && position + 1 < source->length &&
                       source->data[position + 1] == (Py68U8)'=') {
                kind = PY68_TOKEN_STAR_ASSIGN; width = 2;
            } else if (character == (Py68U8)'%' && position + 1 < source->length &&
                       source->data[position + 1] == (Py68U8)'=') {
                kind = PY68_TOKEN_PERCENT_ASSIGN; width = 2;
            } else if (character == (Py68U8)'=' && position + 1 < source->length &&
                       source->data[position + 1] == (Py68U8)'=') {
                kind = PY68_TOKEN_EQUAL; width = 2;
            } else if (character == (Py68U8)'!' && position + 1 < source->length &&
                       source->data[position + 1] == (Py68U8)'=') {
                kind = PY68_TOKEN_NOT_EQUAL; width = 2;
            } else if (character == (Py68U8)'<' && position + 1 < source->length &&
                       source->data[position + 1] == (Py68U8)'=') {
                kind = PY68_TOKEN_LESS_EQUAL; width = 2;
            } else if (character == (Py68U8)'>' && position + 1 < source->length &&
                       source->data[position + 1] == (Py68U8)'=') {
                kind = PY68_TOKEN_GREATER_EQUAL; width = 2;
            } else if (character == (Py68U8)'(') { kind = PY68_TOKEN_LEFT_PAREN; }
            else if (character == (Py68U8)')') { kind = PY68_TOKEN_RIGHT_PAREN; }
            else if (character == (Py68U8)'[') { kind = PY68_TOKEN_LEFT_BRACKET; }
            else if (character == (Py68U8)']') { kind = PY68_TOKEN_RIGHT_BRACKET; }
            else if (character == (Py68U8)'{') { kind = PY68_TOKEN_LEFT_BRACE; }
            else if (character == (Py68U8)'}') { kind = PY68_TOKEN_RIGHT_BRACE; }
            else if (character == (Py68U8)'.') { kind = PY68_TOKEN_DOT; }
            else if (character == (Py68U8)':') { kind = PY68_TOKEN_COLON; }
            else if (character == (Py68U8)',') { kind = PY68_TOKEN_COMMA; }
            else if (character == (Py68U8)'+') { kind = PY68_TOKEN_PLUS; }
            else if (character == (Py68U8)'-') { kind = PY68_TOKEN_MINUS; }
            else if (character == (Py68U8)'*') { kind = PY68_TOKEN_STAR; }
            else if (character == (Py68U8)'/') { kind = PY68_TOKEN_SLASH; }
            else if (character == (Py68U8)'%') { kind = PY68_TOKEN_PERCENT; }
            else if (character == (Py68U8)'=') { kind = PY68_TOKEN_ASSIGN; }
            else if (character == (Py68U8)'<') { kind = PY68_TOKEN_LESS; }
            else if (character == (Py68U8)'>') { kind = PY68_TOKEN_GREATER; }
            else {
                location.offset = start;
                location.line = start_line;
                location.column = start_column;
                location.length = 1;
                py68_set_token_error(error, PY68_ERROR_TOKEN, location, source,
                                     "unexpected character");
                status = PY68_STATUS_SOURCE_ERROR;
                break;
            }
            while (width != 0) {
                py68_advance(&position, &column);
                --width;
            }
            if (kind == PY68_TOKEN_LEFT_PAREN ||
                kind == PY68_TOKEN_LEFT_BRACKET ||
                kind == PY68_TOKEN_LEFT_BRACE) {
                if (group_count == group_capacity &&
                    py68_grow_bytes(allocator, (void **)&groups,
                                    &group_capacity, (Py68U32)sizeof(Py68U8)) !=
                        PY68_STATUS_OK) {
                    status = PY68_STATUS_MEMORY_ERROR;
                    break;
                }
                groups[group_count++] = kind == PY68_TOKEN_LEFT_PAREN
                                        ? (Py68U8)'('
                                        : kind == PY68_TOKEN_LEFT_BRACKET
                                          ? (Py68U8)'['
                                          : (Py68U8)'{';
            } else if (kind == PY68_TOKEN_RIGHT_PAREN ||
                       kind == PY68_TOKEN_RIGHT_BRACKET ||
                       kind == PY68_TOKEN_RIGHT_BRACE) {
                if (group_count == 0 ||
                    (kind == PY68_TOKEN_RIGHT_PAREN &&
                     groups[group_count - 1] != (Py68U8)'(') ||
                    (kind == PY68_TOKEN_RIGHT_BRACKET &&
                     groups[group_count - 1] != (Py68U8)'[') ||
                    (kind == PY68_TOKEN_RIGHT_BRACE &&
                     groups[group_count - 1] != (Py68U8)'{')) {
                    location.offset = start;
                    location.line = start_line;
                    location.column = start_column;
                    location.length = (Py68U16)(position - start);
                    py68_set_token_error(error, PY68_ERROR_TOKEN, location,
                                         source, "unmatched closing delimiter");
                    status = PY68_STATUS_SOURCE_ERROR;
                    break;
                }
                --group_count;
            }
        }

        if (status != PY68_STATUS_OK) {
            break;
        }
        location.offset = start;
        location.line = start_line;
        location.column = start_column;
        location.length = (Py68U16)(position - start);
        status = py68_emit(allocator, tokens, kind, location);
        if (status != PY68_STATUS_OK) {
            break;
        }
        content_since_newline = 1;
    }

    if (status == PY68_STATUS_OK && group_count != 0) {
        Py68Location location;
        location.offset = position;
        location.line = line;
        location.column = column;
        location.length = 0;
        py68_set_token_error(error, PY68_ERROR_TOKEN, location, source,
                             "unmatched opening delimiter");
        status = PY68_STATUS_SOURCE_ERROR;
    }
    if (status == PY68_STATUS_OK && content_since_newline) {
        Py68Location location;
        location.offset = position;
        location.line = line;
        location.column = column;
        location.length = 0;
        status = py68_emit(allocator, tokens, PY68_TOKEN_NEWLINE, location);
    }
    while (status == PY68_STATUS_OK && indent_count > 1) {
        Py68Location location;
        --indent_count;
        location.offset = position;
        location.line = line;
        location.column = column;
        location.length = 0;
        status = py68_emit(allocator, tokens, PY68_TOKEN_DEDENT, location);
    }
    if (status == PY68_STATUS_OK) {
        Py68Location location;
        location.offset = position;
        location.line = line;
        location.column = column;
        location.length = 0;
        status = py68_emit(allocator, tokens, PY68_TOKEN_EOF, location);
    }
    py68_free(allocator, PY68_MEM_TOKEN, indents,
              indent_capacity * (Py68U32)sizeof(Py68U16));
    py68_free(allocator, PY68_MEM_TOKEN, groups,
              group_capacity * (Py68U32)sizeof(Py68U8));
    return status;
}