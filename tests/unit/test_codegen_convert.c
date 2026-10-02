#include "gald/codegen.h"
#include "gald/ast.h"
#include "gald/symbol.h"
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <math.h>

static int total = 0, passed = 0;
#define TEST(n) do { printf("  %-50s ", n); total++; } while(0)
#define PASS() do { passed++; printf("PASS\n"); } while(0)
#define FAIL(m) do { printf("FAIL: %s\n", m); return; } while(0)
#define ASSERT(cond, m) do { if (!(cond)) { printf("FAIL: %s\n", m); return; } } while(0)

static symbol_t *make_func(const char *name) {
    symbol_t *s = sym_alloc(SYM_FUNCTION, name);
    s->data.func.return_type = nf_type_from_cst(NULL);
    if (!s->data.func.return_type) {
        s->data.func.return_type = calloc(1, sizeof(gald_type_t));
    }
    s->data.func.return_type->prim = TYPE_VOID;
    return s;
}

// ─── type string tests ─────────────────────────────────────────────────────────────

static void test_type_void(void) {
    TEST("type_str void");
    char *s = ast_type_to_c_str(NULL);
    ASSERT(strcmp(s, "void") == 0, "expected void");
    free(s);
    PASS();
}

static void test_type_int(void) {
    TEST("type_str int");
    ast_type_t *t = ast_type_alloc(); t->prim = TYPE_INT;
    char *s = ast_type_to_c_str(t);
    ASSERT(strcmp(s, "int") == 0, "expected int");
    free(s); ast_type_free(t);
    PASS();
}

static void test_type_pointer(void) {
    TEST("type_str int *");
    ast_type_t *t = ast_type_alloc(); t->prim = TYPE_INT; t->is_pointer = 1;
    char *s = ast_type_to_c_str(t);
    ASSERT(strcmp(s, "int *") == 0, "expected int *");
    free(s); ast_type_free(t);
    PASS();
}

static void test_type_id(void) {
    TEST("type_str id");
    ast_type_t *t = ast_type_alloc(); t->prim = TYPE_ID;
    char *s = ast_type_to_c_str(t);
    ASSERT(strcmp(s, "NFObject *") == 0, "expected NFObject *");
    free(s); ast_type_free(t);
    PASS();
}

static void test_type_named(void) {
    TEST("type_str named");
    ast_type_t *t = ast_type_alloc(); t->prim = TYPE_NAMED; t->name = strdup("NSString");
    char *s = ast_type_to_c_str(t);
    ASSERT(strcmp(s, "NSString") == 0, "expected NSString");
    free(s); ast_type_free(t);
    PASS();
}

static void test_type_bool(void) {
    TEST("type_str _Bool");
    ast_type_t *t = ast_type_alloc(); t->prim = TYPE_BOOL;
    char *s = ast_type_to_c_str(t);
    ASSERT(strcmp(s, "_Bool") == 0, "expected _Bool");
    free(s); ast_type_free(t);
    PASS();
}

static void test_type_sel(void) {
    TEST("type_str SEL");
    ast_type_t *t = ast_type_alloc(); t->prim = TYPE_SEL;
    char *s = ast_type_to_c_str(t);
    ASSERT(strcmp(s, "SEL") == 0, "expected SEL");
    free(s); ast_type_free(t);
    PASS();
}

// ─── cg_call_expr tests ────────────────────────────────────────────────────────

static void test_cg_call_basic(void) {
    TEST("cg_call_expr basic");
    cg_expr_t *c = cg_call_expr("foo", "void", NULL, 0);
    ASSERT(c->kind == CEXPR_CALL, "expected CEXPR_CALL");
    ASSERT(strcmp(c->u.call.name, "foo") == 0, "expected foo");
    ASSERT(c->u.call.arg_count == 0, "expected 0 args");
    cg_expr_free(c);
    PASS();
}

static void test_cg_call_with_args(void) {
    TEST("cg_call_expr with args");
    cg_expr_t *a1 = cg_expr_alloc(CEXPR_INT);
    a1->u.int_val = 1;
    cg_expr_t *args[] = {a1};
    cg_expr_t *c = cg_call_expr("bar", "int", args, 1);
    ASSERT(c->u.call.arg_count == 1, "expected 1 arg");
    ASSERT(c->u.call.args[0]->u.int_val == 1, "expected 1");
    cg_expr_free(c);
    PASS();
}

// ─── ast_to_cg_unit tests ───────────────────────────────────────────────────────

static void test_unit_empty(void) {
    TEST("unit empty");
    ast_unit_t *au = ast_unit_alloc("test.gm");
    symbol_table_t *st = symtab_alloc();
    cg_unit_t *cu = ast_to_cg_unit(au, st);
    ASSERT(cu->decl_count == 0, "expected 0 decls");
    cg_unit_free(cu); symtab_free(st); ast_unit_free(au);
    PASS();
}

static void test_unit_func_no_body(void) {
    TEST("unit func no body");
    ast_unit_t *au = ast_unit_alloc("t.gm");
    ast_decl_t *d = ast_decl_alloc(AST_DECL_FUNCTION, "f");
    d->data.function.func_sym = make_func("f");
    au->decls = calloc(1, sizeof(ast_decl_t *));
    au->decls[0] = d; au->decl_count = 1;
    symbol_table_t *st = symtab_alloc();
    cg_unit_t *cg = ast_to_cg_unit(au, st);
    ASSERT(cg->decl_count == 1, "expected 1 decl");
    ASSERT(cg->decls[0]->kind == CGDECL_FUNCTION, "expected CGDECL_FUNCTION");
    cg_unit_free(cg); symtab_free(st); ast_unit_free(au);
    PASS();
}

static void test_unit_variable(void) {
    TEST("unit variable decl");
    ast_unit_t *au = ast_unit_alloc("t.gm");
    ast_decl_t *d = ast_decl_alloc(AST_DECL_VARIABLE, "x");
    d->data.variable.type = ast_type_alloc();
    d->data.variable.type->prim = TYPE_INT;
    d->data.variable.init = NULL;
    au->decls = calloc(1, sizeof(ast_decl_t *));
    au->decls[0] = d; au->decl_count = 1;
    symbol_table_t *st = symtab_alloc();
    cg_unit_t *cu = ast_to_cg_unit(au, st);
    ASSERT(cu->decl_count == 1, "expected 1 decl");
    ASSERT(cu->decls[0]->kind == CGDECL_VARIABLE, "expected CGDECL_VARIABLE");
    cg_unit_free(cu); symtab_free(st); ast_unit_free(au);
    PASS();
}

int main(void) {
    printf("codegen conversion tests\n");
    printf("---------------------\n");
    test_type_void();
    test_type_int();
    test_type_pointer();
    test_type_id();
    test_type_named();
    test_type_bool();
    test_type_sel();
    test_cg_call_basic();
    test_cg_call_with_args();
    test_unit_empty();
    test_unit_func_no_body();
    test_unit_variable();
    printf("\n%d/%d passed\n", passed, total);
    return passed == total ? 0 : 1;
}