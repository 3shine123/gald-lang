#include "ovel/codegen.h"
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

static int total = 0, passed = 0;
#define TEST(n) do { printf("  %-50s ", n); total++; } while(0)
#define PASS() do { passed++; printf("PASS\n"); } while(0)
#define FAIL(m) do { printf("FAIL: %s\n", m); return; } while(0)
#define ASSERT(cond, m) do { if (!(cond)) { printf("FAIL: %s\n", m); return; } } while(0)

static void test_expr_int(void) {
    TEST("cg_expr int");
    cg_expr_t *e = cg_expr_alloc(CEXPR_INT);
    e->u.int_val = 42;
    cg_expr_free(e);
    PASS();
}

static void test_expr_call(void) {
    TEST("cg_expr call");
    cg_expr_t *e = cg_expr_alloc(CEXPR_CALL);
    e->u.call.name = strdup("ovel_release");
    e->u.call.args = calloc(2, sizeof(cg_expr_t *));
    e->u.call.args[0] = cg_expr_alloc(CEXPR_IDENT);
    e->u.call.args[0]->u.id = strdup("obj");
    e->u.call.args[1] = NULL;
    e->u.call.arg_count = 1;
    cg_expr_free(e->u.call.args[0]);
    free(e->u.call.args);
    cg_expr_free(e);
    PASS();
}

static void test_stmt_compound(void) {
    TEST("cg_stmt compound");
    cg_stmt_t *s = cg_stmt_alloc(CGSTMT_COMPOUND);
    s->u.compound.count = 2;
    s->u.compound.cap = 2;
    s->u.compound.stmts = calloc(2, sizeof(cg_stmt_t *));
    s->u.compound.stmts[0] = cg_stmt_alloc(CGSTMT_EMPTY);
    s->u.compound.stmts[1] = cg_stmt_alloc(CGSTMT_RETURN);
    cg_stmt_free(s);
    PASS();
}

static void test_decl_function(void) {
    TEST("cg_decl function");
    cg_decl_t *d = cg_decl_alloc(CGDECL_FUNCTION, "foo");
    d->u.func.return_type = "int";
    d->u.func.param_count = 1;
    d->u.func.params = calloc(1, sizeof(*d->u.func.params));
    d->u.func.params[0].type = "id";
    d->u.func.params[0].name = "obj";
    d->u.func.body = cg_stmt_alloc(CGSTMT_EMPTY);
    cg_decl_free(d);
    PASS();
}

static void test_unit(void) {
    TEST("cg_unit alloc/free");
    cg_unit_t *u = cg_unit_alloc("out.c");
    u->decl_count = 1;
    u->decls[0] = cg_decl_alloc(CGDECL_FUNCTION, "main");
    u->decls[0]->u.func.return_type = "int";
    u->decls[0]->u.func.body = cg_stmt_alloc(CGSTMT_RETURN);
    cg_unit_free(u);
    PASS();
}

static void test_print_no_crash(void) {
    TEST("cg_print no crash");
    cg_unit_t *u = cg_unit_alloc("test.c");
    cg_decl_t *d = cg_decl_alloc(CGDECL_FUNCTION, "f");
    d->u.func.return_type = "void";
    u->decls[0] = d;
    u->decl_count = 1;
    FILE *old = stdout;
    stdout = fopen("/dev/null", "w");
    cg_print(u);
    fclose(stdout);
    stdout = old;
    cg_unit_free(u);
    PASS();
}

static void test_free_null(void) {
    TEST("cg_free null");
    cg_expr_free(NULL);
    cg_stmt_free(NULL);
    cg_decl_free(NULL);
    cg_unit_free(NULL);
    PASS();
}

int main(void) {
    printf("codegen tests\n");
    printf("-------------\n");
    test_expr_int();
    test_expr_call();
    test_stmt_compound();
    test_decl_function();
    test_unit();
    test_print_no_crash();
    test_free_null();
    printf("\n%d/%d passed\n", passed, total);
    return passed == total ? 0 : 1;
}