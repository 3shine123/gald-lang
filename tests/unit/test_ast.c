#include "nopa/ast.h"
#include <stdio.h>
#include <string.h>
#include <stdlib.h>

static int total = 0, passed = 0;
#define TEST(n) do { printf("  %-50s ", n); total++; } while(0)
#define PASS() do { passed++; printf("PASS\n"); } while(0)
#define FAIL(m) do { printf("FAIL: %s\n", m); return; } while(0)

static void test_alloc_int(void) {
    TEST("ast_expr_alloc int");
    ast_expr_t *e = ast_expr_alloc(AST_EXPR_INT);
    if (!e) { FAIL("alloc failed"); return; }
    e->data.int_val = 42;
    ast_expr_free(e);
    PASS();
}

static void test_alloc_string(void) {
    TEST("ast_expr_alloc string");
    ast_expr_t *e = ast_expr_alloc(AST_EXPR_STRING);
    if (!e) { FAIL("alloc failed"); return; }
    e->data.str_val = strdup("hello");
    ast_expr_free(e);
    PASS();
}

static void test_stmt_compound(void) {
    TEST("ast_stmt_alloc compound");
    ast_stmt_t *s = ast_stmt_alloc(AST_STMT_COMPOUND);
    if (!s) { FAIL("alloc failed"); return; }
    s->data.compound.count = 2;
    s->data.compound.stmts = calloc(2, sizeof(ast_stmt_t *));
    s->data.compound.stmts[0] = ast_stmt_alloc(AST_STMT_BREAK);
    s->data.compound.stmts[1] = ast_stmt_alloc(AST_STMT_RETURN);
    ast_stmt_free(s->data.compound.stmts[0]);
    ast_stmt_free(s->data.compound.stmts[1]);
    free(s->data.compound.stmts);
    ast_stmt_free(s);
    PASS();
}

static void test_unit(void) {
    TEST("ast_unit_alloc + free");
    ast_unit_t *u = ast_unit_alloc("test.np");
    if (!u) { FAIL("alloc failed"); return; }
    ast_unit_free(u);
    PASS();
}

static void test_print(void) {
    TEST("ast_print (no crash)");
    ast_unit_t *u = ast_unit_alloc("test.np");
    ast_decl_t *d = ast_decl_alloc(AST_DECL_FUNCTION, "main");
    u->decls = malloc(sizeof(ast_decl_t *));
    u->decls[0] = d;
    u->decl_count = 1;
    // redirect output to /dev/null
    FILE *old = stdout;
    stdout = fopen("/dev/null", "w");
    ast_print(u);
    fclose(stdout);
    stdout = old;
    ast_unit_free(u);
    PASS();
}

int main(void) {
    printf("ast tests\n");
    printf("---------\n");
    test_alloc_int();
    test_alloc_string();
    test_stmt_compound();
    test_unit();
    test_print();
    printf("\n%d/%d passed\n", passed, total);
    return passed == total ? 0 : 1;
}
