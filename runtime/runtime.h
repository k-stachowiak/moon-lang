/* Copyright (C) 2014,2015 Krzysztof Stachowiak */

#ifndef RUNTIME_H
#define RUNTIME_H

#include <stdbool.h>

#include "dbg.h"
#include "ast.h"
#include "stack.h"
#include "symmap.h"

typedef void (*ClifIntraHandler)(char*, VAL_LOC_T*, int);

struct Runtime {
    struct Stack *stack;
    struct SymMap global_sym_map;
    struct AstNode *node_store;

    bool debug;
    struct Debugger debugger;

    VAL_LOC_T saved_loc;
    struct AstNode *saved_store;
};

struct Runtime *rt_make(void);
void rt_reset(struct Runtime *rt);
void rt_free(struct Runtime *rt);

void rt_save(struct Runtime *rt);
void rt_restore(struct Runtime *rt);

void rt_register_clif_handler(
		struct Runtime *rt,
		char *symbol,
		int arity,
		ClifIntraHandler handler);

bool rt_consume_one(
        struct Runtime *rt,
        struct AstNode *ast,
        VAL_LOC_T *loc,
        struct AstNode **next);

bool rt_consume_list(
        struct Runtime *rt,
        struct AstNode *ast,
        VAL_LOC_T *last_loc);

void rt_for_each_stack_val(struct Runtime *rt, void(*f)(void*, VAL_LOC_T));
void rt_for_each_sym(struct Runtime *rt, void(*f)(void*, char*, VAL_LOC_T));

#endif
