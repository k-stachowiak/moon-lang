/* Copyright (C) 2014-2016 Krzysztof Stachowiak */

#include <stdio.h>
#include <inttypes.h>

#include "eval.h"
#include "memory.h"
#include "error.h"
#include "rt_val.h"
#include "bif.h"
#include "bif_detail.h"
#include "parse.h"

static void bif_text_error_arg(int arg, char *func, char *condition)
{
    err_push("BIF", "Argument %d of _%s_ %s", arg, func, condition);
}

static void bif_text_error_wc_mismatch(void)
{
    err_push("BIF", "Wildcard type mismatched argument");
}

static void bif_text_error_parse(void)
{
    err_push("BIF", "Failed parsing string literal");
}

static void bif_text_error_parse_homo(void)
{
    err_push("BIF", "Failed parsing non-homogenous array");
}

static char *bif_text_find_format(char *str)
{
    while (*str != '\0') {
        if (*str == '%') {
            if (*(str + 1) == '%') {
                ++str;
            } else {
                return str;
            }
        }
        ++str;
    }
    return str;
}

static void bif_format_try_appending_arg(
        struct Runtime *rt,
        char **result,
        char wc,
        VAL_LOC_T loc)
{
    enum ValueType type = rt_val_peek_type(&rt->stack, loc);

    switch (wc) {
    case 'b':
        if (type != VAL_BOOL) {
            bif_text_error_wc_mismatch();
        } else {
            str_append(*result, "%s", rt_val_peek_bool(rt, loc) ? "true" : "false");
        }
        break;

    case 'c':
        if (type != VAL_CHAR) {
            bif_text_error_wc_mismatch();
        } else {
            str_append(*result, "%c", rt_val_peek_char(rt, loc));
        }
        break;

    case 'd':
        if (type != VAL_INT) {
            bif_text_error_wc_mismatch();
        } else {
            str_append(*result, "%" PRIu64, rt_val_peek_int(rt, loc));
        }
        break;

    case 'f':
        if (type != VAL_REAL) {
            bif_text_error_wc_mismatch();
        } else {
            str_append(*result, "%f", rt_val_peek_real(rt, loc));
        }
        break;

    case 's':
        if (!rt_val_is_string(rt, loc)) {
            bif_text_error_wc_mismatch();
        } else {
            char *str = rt_val_peek_cpd_as_string(rt, loc);
            str_append(*result, "%s", str);
            mem_free(str);
        }
        break;

    default:
        err_push("BIF", "Wildcard '%c' unknown", wc);
    }
}

static void bif_format_impl(
        struct Runtime *rt,
        char *str,
        int argc,
        VAL_LOC_T arg_loc)
{
    int len;
    bool done = false;
    char *copy, *begin, *end;

    int args_left = argc;
    char *result = NULL;

    len = strlen(str);
    copy = mem_malloc(len + 1);
    memcpy(copy, str, len + 1);
    begin = copy;

    while (true) {

        end = bif_text_find_format(begin);
        if (*end == '\0') {
            done = true;
        }

        *end = '\0';
        str_append(result, "%s", begin);

        if (done) {
            break;
        }

        begin = end + 1; /* skip '%' */

        if (args_left == 0) {
            err_push("BIF", "Not enough arguments passed");
            goto end;
        }
        bif_format_try_appending_arg(rt, &result, *begin, arg_loc);
        if (err_state()) {
            err_push("BIF", "Failed parsing DOM list");
            goto end;
        }

        arg_loc = rt_val_next_loc(rt, arg_loc);
        ++begin; /* skip wildcard */

        --args_left;
    }

    if (args_left) {
        err_push("BIF", "%d arguments left after format", args_left);
    } else {
        rt_val_push_string(&rt->stack, result, result + strlen(result));
    }

end:
    mem_free(copy);
    if (result) {
        mem_free(result);
    }
}

static void bif_parse_any_ast(struct Runtime *rt, struct AstNode *ast);

static void bif_parse_any_ast_literal_compound(
        struct Runtime *rt,
        struct AstLiteralCompound *lit_cpd)
{

    VAL_LOC_T size_loc = -1, data_begin, data_size;
    struct AstNode *current = lit_cpd->exprs;
    bool has_first_elem = false;
    VAL_LOC_T first_elem_loc, elem_loc;

    /* Header. */
    switch (lit_cpd->type) {
    case AST_LIT_CPD_ARRAY:
        rt_val_push_array_init(&rt->stack, &size_loc);
        break;

    case AST_LIT_CPD_TUPLE:
        rt_val_push_tuple_init(&rt->stack, &size_loc);
        break;
    }

    /* Data. */
    data_begin = rt->stack.top;
    while (current) {

        elem_loc = rt->stack.top;
        bif_parse_any_ast(rt, current);
        if (err_state()) {
            err_push("BIF", "Failed parsing compound AST node");
            return;
        } else {
            current = current->next;
        }

        if (lit_cpd->type != AST_LIT_CPD_ARRAY) {
            continue;
        }

        /* Homogenity check */
        if (!has_first_elem) {
            has_first_elem = true;
            first_elem_loc = elem_loc;
        } else if (!rt_val_pair_homo(rt, first_elem_loc, elem_loc)) {
            bif_text_error_parse_homo();
            return;
        }
    }

    /* Fix the header with the written size. */
    data_size = rt->stack.top - data_begin;
    rt_val_push_cpd_final(&rt->stack, size_loc, data_size);
}

static void bif_parse_any_ast_literal_atomic(
        struct Runtime *rt,
        struct AstLiteralAtomic *literal_atomic)
{
    char *str;
    switch (literal_atomic->type) {
    case AST_LIT_ATOM_UNIT:
        break;

    case AST_LIT_ATOM_BOOL:
        rt_val_push_bool(&rt->stack, literal_atomic->data.boolean);
        break;

    case AST_LIT_ATOM_STRING:
        str = literal_atomic->data.string;
        rt_val_push_string(&rt->stack, str, str + strlen(str));
        break;

    case AST_LIT_ATOM_CHAR:
        rt_val_push_char(&rt->stack, literal_atomic->data.character);
        break;

    case AST_LIT_ATOM_INT:
        rt_val_push_int(&rt->stack, literal_atomic->data.integer);
        break;

    case AST_LIT_ATOM_REAL:
        rt_val_push_real(&rt->stack, literal_atomic->data.real);
        break;

    case AST_LIT_ATOM_DATATYPE:
        switch (literal_atomic->data.datatype) {
        case AST_LIT_ATOM_DATATYPE_VOID:
            rt_val_push_datatype_atom(&rt->stack, VAL_DATA_VOID);
            break;

        case AST_LIT_ATOM_DATATYPE_UNIT:
            rt_val_push_datatype_atom(&rt->stack, VAL_DATA_UNIT);
            break;

        case AST_LIT_ATOM_DATATYPE_BOOLEAN:
            rt_val_push_datatype_atom(&rt->stack, VAL_DATA_BOOLEAN);
            break;

        case AST_LIT_ATOM_DATATYPE_INTEGER:
            rt_val_push_datatype_atom(&rt->stack, VAL_DATA_INTEGER);
            break;

        case AST_LIT_ATOM_DATATYPE_REAL:
            rt_val_push_datatype_atom(&rt->stack, VAL_DATA_REAL);
            break;

        case AST_LIT_ATOM_DATATYPE_CHARACTER:
            rt_val_push_datatype_atom(&rt->stack, VAL_DATA_CHARACTER);
            break;
        }
        break;
    }
}

static void bif_parse_any_ast(struct Runtime *runtime, struct AstNode *ast)
{
    switch (ast->type) {
    case AST_LITERAL_COMPOUND:
        bif_parse_any_ast_literal_compound(runtime, &ast->data.literal_compound);
        break;

    case AST_LITERAL_ATOMIC:
        bif_parse_any_ast_literal_atomic(runtime, &ast->data.literal_atomic);
        break;

    case AST_SPECIAL:
    case AST_SYMBOL:
    case AST_FUNCTION_CALL:
        bif_text_error_parse();
        break;
    }
}

static void bif_parse_any(struct Runtime *rt, char *string)
{
    struct AstNode *ast;

    ast = parse_source(string, NULL, NULL);
    if (!ast) {
        bif_text_error_parse();
        return;
    }

    if (ast->next != NULL) {
        bif_text_error_parse();
        ast_node_free(ast);
        return;
    }

    bif_parse_any_ast(rt, ast);
    ast_node_free(ast);
}

static void bif_parse_atomic(
        struct Runtime *rt,
        VAL_LOC_T arg_loc,
        char *func,
        enum AstLiteralAtomicType type)
{
    VAL_LOC_T size_loc, data_begin, data_size;
    struct AstNode *ast = NULL;
    char *source, *err;

    /* Assert input. */
    if (!rt_val_is_string(rt, arg_loc)) {
        bif_text_error_arg(1, func, "must be a string");
        return;
    }

    /* Push header. */
    rt_val_push_tuple_init(&rt->stack, &size_loc);
    data_begin = rt->stack.top;

    source = rt_val_peek_cpd_as_string(rt, arg_loc);
    ast = parse_source(source, NULL, NULL);
    mem_free(source);

    /* Error detection. */
    if (!ast) {
        err = "Failed parsing atomic literal";
        rt_val_push_bool(&rt->stack, false);
        rt_val_push_string(&rt->stack, err, err + strlen(err));
        goto end;
    }
    if (ast->next != NULL) {
        err = "Too many nodes.";
        rt_val_push_bool(&rt->stack, false);
        rt_val_push_string(&rt->stack, err, err + strlen(err));
        goto end;
    }
    if (ast->type != AST_LITERAL_ATOMIC || ast->data.literal_atomic.type != type) {
        err = "Incorrect type.";
        rt_val_push_bool(&rt->stack, false);
        rt_val_push_string(&rt->stack, err, err + strlen(err));
        goto end;
    }

    /* Correct case. */
    rt_val_push_bool(&rt->stack, true);
    bif_parse_any_ast_literal_atomic(rt, &ast->data.literal_atomic);

end:
    data_size = rt->stack.top - data_begin;
    rt_val_push_cpd_final(&rt->stack, size_loc, data_size);
    if (ast) {
        ast_node_free(ast);
    }
}

void bif_print(struct Runtime *rt, VAL_LOC_T str_loc)
{
    if (!rt_val_is_string(rt, str_loc)) {
        bif_text_error_arg(1, "print", "must be a string");
    } else {
        char *string = rt_val_peek_cpd_as_string(rt, str_loc);
        printf("%s", string);
        mem_free(string);
        rt_val_push_unit(&rt->stack);
    }
}

void bif_format(struct Runtime *rt, VAL_LOC_T fmt_loc, VAL_LOC_T args_loc)
{
    char *string;
    int argc;

    if (!rt_val_is_string(rt, fmt_loc)) {
        bif_text_error_arg(1, "format", "must be a string");
        return;
    }

    if (rt_val_peek_type(&rt->stack, args_loc) != VAL_TUPLE) {
        bif_text_error_arg(2, "format", "must be a tuple");
        return;
    }

    string = rt_val_peek_cpd_as_string(rt, fmt_loc);
    argc = rt_val_cpd_len(rt, args_loc);

    bif_format_impl(rt, string, argc, rt_val_cpd_first_loc(args_loc));

    mem_free(string);
}

void bif_to_string(struct Runtime *rt, VAL_LOC_T arg_loc)
{
    char *buffer = NULL;
    rt_val_to_string(rt, arg_loc, &buffer);

    if (!buffer) {
        err_push("BIF", "Failed rendering value as string");
    } else {
        rt_val_push_string(&rt->stack, buffer, buffer + strlen(buffer));
        mem_free(buffer);
    }
}

void bif_parse(struct Runtime *rt, VAL_LOC_T arg_loc)
{
    VAL_LOC_T size_loc, data_begin, result_begin, data_size;
    char *string;

    if (!rt_val_is_string(rt, arg_loc)) {
        bif_text_error_arg(1, "parse", "must be a string");
        return;
    }

    rt_val_push_tuple_init(&rt->stack, &size_loc);
    data_begin = rt->stack.top;

    rt_val_push_bool(&rt->stack, true);
    result_begin = rt->stack.top;

    string = rt_val_peek_cpd_as_string(rt, arg_loc);
    bif_parse_any(rt, string);
    mem_free(string);

    if (err_state()) {
        char *message = err_msg();

        /* NOTE: the error here is intentionally sweeped under the carpet. */
        stack_collapse(&rt->stack, result_begin, rt->stack.top);
        rt_val_poke_bool(&rt->stack, data_begin, false);
        rt_val_push_string(&rt->stack, message, message + strlen(message));
        err_reset();

        mem_free(message);
    }

    data_size = rt->stack.top - data_begin;
    rt_val_push_cpd_final(&rt->stack, size_loc, data_size);
}

void bif_parse_bool(struct Runtime *rt, VAL_LOC_T arg_loc)
{
    bif_parse_atomic(rt, arg_loc, "parse-bool", AST_LIT_ATOM_BOOL);
}

void bif_parse_int(struct Runtime *rt, VAL_LOC_T arg_loc)
{
    bif_parse_atomic(rt, arg_loc, "parse-int", AST_LIT_ATOM_INT);
}

void bif_parse_real(struct Runtime *rt, VAL_LOC_T arg_loc)
{
    bif_parse_atomic(rt, arg_loc, "parse-real", AST_LIT_ATOM_REAL);
}

void bif_parse_char(struct Runtime *rt, VAL_LOC_T arg_loc)
{
    bif_parse_atomic(rt, arg_loc, "parse-char", AST_LIT_ATOM_CHAR);
}


