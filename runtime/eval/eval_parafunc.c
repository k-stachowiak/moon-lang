/* Copyright (C) 2015 Krzysztof Stachowiak */

#include <stdbool.h>
#include <inttypes.h>

#include "error.h"
#include "eval.h"
#include "eval_detail.h"
#include "rt_val.h"

static void para_error_ref_of_nonref(void)
{
	err_push("EVAL", "Parafunc _ref_ used with a non-symbol argument");
}

static void para_error_peek_nonref(char *func)
{
	err_push("EVAL", "Parafunc _%s_ used with a non-reference argument", func);
}

static void para_error_not_implemented_yet(char *func)
{
	err_push("EVAL", "Parafunc _%s_ not implemented yet", func);
}

static void para_error_invalid_argc(char *func, int count)
{
	err_push("EVAL",
        "Incorrect arguments count passed to _%s_: %d",
        func, count);
}

static void para_error_arg_not_bool(char *func, int index)
{
	err_push("EVAL",
        "argument %d of _%s_ is not of boolean type",
        index, func);
}

static void para_error_case(char *unmet)
{
	err_push("EVAL", "Case %s", unmet);
}

static void eval_parafunc_if(
        struct Runtime *rt,
        struct SymMap *sym_map,
        struct AstNode *args)
{
    VAL_LOC_T test_loc, temp_begin, temp_end;
    VAL_BOOL_T test_val;
    int argc = ast_list_len(args);

    if (argc != 3) {
        para_error_invalid_argc("if", argc);
        return;
    }

    temp_begin = rt->stack.top;
    test_loc = eval_impl(args, rt, sym_map);
    temp_end = rt->stack.top;

    if (err_state()) {
		err_push_src("EVAL", args->loc, "Failed evaluating if test");
        return;
    }

    if (rt_val_peek_type(&rt->stack, test_loc) != VAL_BOOL) {
        para_error_arg_not_bool("if", 1);
        stack_collapse(&rt->stack, temp_begin, temp_end);
        return;
    }

    test_val = rt_val_peek_bool(rt, test_loc);
    stack_collapse(&rt->stack, temp_begin, temp_end);

    if (test_val) {
        eval_impl(args->next, rt, sym_map);
    } else {
        eval_impl(args->next->next, rt, sym_map);
    }
}

static bool eval_parafunc_switch_case(
        VAL_LOC_T switch_loc,
        struct Runtime *rt,
        struct SymMap *sym_map,
        struct AstNode *case_node)
{
    VAL_LOC_T temp_begin, temp_end;
    VAL_LOC_T case_loc, case_key, case_value;
    int case_len;
    bool result = false;

    temp_begin = rt->stack.top;
    case_loc = eval_impl(case_node, rt, sym_map);
    temp_end = rt->stack.top;

    if (err_state()) {
		err_push_src("EVAL", case_node->loc, "Failed evaluating case expression");
        goto end;
    }

    if (rt_val_peek_type(&rt->stack, case_loc) != VAL_TUPLE) {
        para_error_case("must be tuple");
        goto end;
    }

    case_len = rt_val_cpd_len(rt, case_loc);

    if (case_len != 2) {
        para_error_case("must be 2 element tuple");
        goto end;
    }

    case_key = rt_val_cpd_first_loc(case_loc);
    case_value = rt_val_next_loc(rt, case_key);

    if (rt_val_eq_bin(rt, switch_loc, case_key)) {
        rt_val_push_copy(&rt->stack, case_value);
        result = true;
    }

end:
    stack_collapse(&rt->stack, temp_begin, temp_end);
    return result;
}

static void eval_parafunc_switch(
        struct Runtime *rt,
        struct SymMap *sym_map,
        struct AstNode *args)
{
    VAL_LOC_T switch_loc, temp_begin, temp_end;
    int argc = ast_list_len(args);

    if (argc < 2) {
        para_error_invalid_argc("switch", argc);
        return;
    }

    temp_begin = rt->stack.top;
    switch_loc = eval_impl(args, rt, sym_map);
    temp_end = rt->stack.top;

    if (err_state()) {
		err_push_src("EVAL", args->loc, "Failed evaluating switch expression");
        goto end;
    }

    args = args->next;

    while (args) {
        if (eval_parafunc_switch_case(switch_loc, rt, sym_map, args)) {
            goto end;
        } else {
            if (err_state()) {
				err_push("EVAL", "Failed evaluating switch case");
                goto end;
            }
            args = args->next;
        }
    }

	err_push("EVAL", "Unmached case in a swithc");

end:
    stack_collapse(&rt->stack, temp_begin, temp_end);
}

static void eval_parafunc_logic(
        struct Runtime *rt,
        struct SymMap *sym_map,
        struct AstNode *args,
        bool breaking_value,
        char *func)
{
    int i = 0;
    VAL_LOC_T temp_begin = rt->stack.top;
    VAL_BOOL_T result = !breaking_value;

    if (!args) {
        para_error_invalid_argc(func, 0);
        return;
    }

    while (args) {

        VAL_LOC_T loc = eval_impl(args, rt, sym_map);
        if (err_state()) {
			err_push_src("EVAL", args->loc, "Failed evaluating logic parafunc element");
            return;
        }

        if (rt_val_peek_type(&rt->stack, loc) != VAL_BOOL) {
            para_error_arg_not_bool(func, i);
            return;
        }

        if (rt_val_peek_bool(rt, loc) == breaking_value) {
            result = breaking_value;
            break;
        }

        args = args->next;
        ++i;
    }

    stack_collapse(&rt->stack, temp_begin, rt->stack.top);
    rt_val_push_bool(&rt->stack, result);
}

static void eval_parafunc_ref(
        struct Runtime *rt,
        struct SymMap *sym_map,
        struct AstNode *args)
{
    int len = ast_list_len(args);
    char *symbol;
    struct SymMapKvp *kvp;

    if (len != 1) {
        para_error_invalid_argc("ref", len);
        return;
    }

    if (args->type != AST_REFERENCE) {
        para_error_ref_of_nonref();
        return;
    }
    symbol = args->data.reference.symbol;
    LOG_DEBUG("(ref %s)", symbol);
    kvp = sym_map_find(sym_map, symbol);

    if (!kvp) {
        eval_error_not_found(symbol);
        return;
    }

    LOG_DEBUG("Storing reference to %" PRIu64 " at %" PRIu64, kvp->stack_loc, rt->stack.top);
    rt_val_push_ref(&rt->stack, kvp->stack_loc);
}

static void eval_parafunc_peek(
        struct Runtime *rt,
        struct SymMap *sym_map,
        struct AstNode *args)
{
    int len;
    VAL_LOC_T ref_loc, target_loc;
    VAL_LOC_T temp_begin, temp_end;
    enum ValueType ref_type;

    len = ast_list_len(args);
    if (len != 1) {
        para_error_invalid_argc("peek", len);
        return;
    }

    temp_begin = rt->stack.top;
    ref_loc = eval_impl(args, rt, sym_map);
    temp_end = rt->stack.top;
    if (err_state()) {
		err_push_src("EVAL", args->loc, "Failed evaluating _peek_ reference argument");
        return;
    }

    ref_type = rt_val_peek_type(&rt->stack, ref_loc);
    if (ref_type != VAL_REF) {
        para_error_peek_nonref("peek");
        return;
    }

    target_loc = rt_val_peek_ref(rt, ref_loc);

    LOG_DEBUG("Peeking refered value from %" PRIu64 " towards %" PRIu64, ref_loc, target_loc);
    rt_val_push_copy(&rt->stack, target_loc);

    stack_collapse(&rt->stack, temp_begin, temp_end);
}

static void eval_parafunc_poke(
        struct Runtime *rt,
        struct SymMap *sym_map,
        struct AstNode *args)
{
    int len;
    VAL_LOC_T ref_loc, source_loc, target_loc;
    VAL_LOC_T temp_begin, temp_end;
    enum ValueType ref_type;

    len = ast_list_len(args);
    if (len != 2) {
        para_error_invalid_argc("poke", len);
        return;
    }

    temp_begin = rt->stack.top;
    ref_loc = eval_impl(args, rt, sym_map);
    if (err_state()) {
		err_push_src("EVAL", args->loc, "Failed evaluating _poke_ reference argument");
        return;
    }

    ref_type = rt_val_peek_type(&rt->stack, ref_loc);
    if (ref_type != VAL_REF) {
        para_error_peek_nonref("poke");
        return;
    }

    target_loc = rt_val_peek_ref(rt, ref_loc);
    LOG_DEBUG("(poke %" PRIu64 ")", target_loc);

    source_loc = eval_impl(args->next, rt, sym_map);
    temp_end = rt->stack.top;
    if (err_state()) {
		err_push_src("EVAL", args->loc, "Failed evaluating _poke_ source argument");
        return;
    }

    LOG_DEBUG("Poking refered value at %" PRIu64 " with value from %" PRIu64, target_loc, source_loc);
    rt_val_poke_copy(&rt->stack, target_loc, source_loc);

    stack_collapse(&rt->stack, temp_begin, temp_end);
}

static void eval_parafunc_succ(
        struct Runtime *rt,
        struct SymMap *sym_map,
        struct AstNode *args)
{
    int len;
    VAL_LOC_T ref_loc, target_loc;
    VAL_LOC_T temp_begin, temp_end;
    enum ValueType ref_type;

    len = ast_list_len(args);
    if (len != 1) {
        para_error_invalid_argc("succ", len);
        return;
    }

    temp_begin = rt->stack.top;
    ref_loc = eval_impl(args, rt, sym_map);
    temp_end = rt->stack.top;
    if (err_state()) {
		err_push_src("EVAL", args->loc, "Failed evaluating _succ_ reference argument");
        return;
    }

    ref_type = rt_val_peek_type(&rt->stack, ref_loc);
    if (ref_type != VAL_REF) {
        para_error_peek_nonref("succ");
        return;
    }

    target_loc = rt_val_peek_ref(rt, ref_loc);
    target_loc = rt_val_next_loc(rt, target_loc);
    rt_val_poke_ref(&rt->stack, ref_loc, target_loc);

    stack_collapse(&rt->stack, temp_begin, temp_end);
}

static void eval_parafunc_pred(
        struct Runtime *rt,
        struct SymMap *sym_map,
        struct AstNode *args)
{
    para_error_not_implemented_yet("pred");
}

void eval_parafunc(
        struct AstNode *node,
        struct Runtime *rt,
        struct SymMap *sym_map)
{
    enum AstParafuncType type = node->data.parafunc.type;
    struct AstNode *args = node->data.parafunc.args;

    switch (type) {
    case AST_PARAFUNC_IF:
        eval_parafunc_if(rt, sym_map, args);
        break;

    case AST_PARAFUNC_SWITCH:
        eval_parafunc_switch(rt, sym_map, args);
        break;

    case AST_PARAFUNC_AND:
        eval_parafunc_logic(rt, sym_map, args, false, "and");
        break;

    case AST_PARAFUNC_OR:
        eval_parafunc_logic(rt, sym_map, args, true, "or");
        break;

    case AST_PARAFUNC_REF:
        eval_parafunc_ref(rt, sym_map, args);
		break;

    case AST_PARAFUNC_PEEK:
        eval_parafunc_peek(rt, sym_map, args);
		break;

    case AST_PARAFUNC_POKE:
        eval_parafunc_poke(rt, sym_map, args);
		break;

    case AST_PARAFUNC_SUCC:
        eval_parafunc_succ(rt, sym_map, args);
		break;

    case AST_PARAFUNC_PRED:
        eval_parafunc_pred(rt, sym_map, args);
        break;
    }
}
