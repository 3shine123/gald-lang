#include "gald/cfg.h"
#include "gald/ast.h"
#include <stdio.h>
#include <stdlib.h>

static int total = 0, passed = 0;
#define TEST(n) do { printf("  %-50s ", n); total++; } while(0)
#define PASS() do { passed++; printf("PASS\n"); } while(0)
#define FAIL(m) do { printf("FAIL: %s\n", m); return; } while(0)

static void test_empty_body(void) {
    TEST("cfg empty body");
    ast_stmt_t *body = ast_stmt_alloc(AST_STMT_COMPOUND);
    cfg_t *cfg = cfg_build(body);
    if (!cfg) { FAIL("cfg is NULL"); return; }
    cfg_free(cfg);
    ast_stmt_free(body);
    PASS();
}

static void test_seq_return(void) {
    TEST("cfg sequence -> return");
    ast_stmt_t *ret = ast_stmt_alloc(AST_STMT_RETURN);
    ast_stmt_t *body = ast_stmt_alloc(AST_STMT_COMPOUND);
    body->data.compound.count = 1;
    body->data.compound.stmts = calloc(1, sizeof(ast_stmt_t *));
    body->data.compound.stmts[0] = ret;

    cfg_t *cfg = cfg_build(body);
    if (!cfg) { FAIL("cfg is NULL"); return; }
    if (!cfg->entry) { FAIL("no entry"); return; }
    if (!cfg->exit) { FAIL("no exit"); return; }
    if (cfg->block_count < 2) { FAIL("too few blocks"); return; }
    cfg_free(cfg);
    ast_stmt_free(body);
    PASS();
}

static void test_if_else(void) {
    TEST("cfg if-else");
    ast_expr_t *cond = ast_expr_alloc(AST_EXPR_BOOL);
    cond->data.bool_val = 1;
    ast_stmt_t *then_s = ast_stmt_alloc(AST_STMT_EXPR);
    then_s->data.expr = ast_expr_alloc(AST_EXPR_INT);
    ast_stmt_t *else_s = ast_stmt_alloc(AST_STMT_EXPR);
    else_s->data.expr = ast_expr_alloc(AST_EXPR_FLOAT);

    ast_stmt_t *if_stmt = ast_stmt_alloc(AST_STMT_IF);
    if_stmt->data.if_.cond = cond;
    if_stmt->data.if_.then = then_s;
    if_stmt->data.if_.else_ = else_s;

    ast_stmt_t *body = ast_stmt_alloc(AST_STMT_COMPOUND);
    body->data.compound.count = 1;
    body->data.compound.stmts = calloc(1, sizeof(ast_stmt_t *));
    body->data.compound.stmts[0] = if_stmt;

    cfg_t *cfg = cfg_build(body);
    if (!cfg) { FAIL("cfg is NULL"); return; }
    if (cfg->block_count < 4) { FAIL("expected >= 4 blocks"); return; }
    cfg_free(cfg);
    ast_stmt_free(body);
    PASS();
}

static void test_while_loop(void) {
    TEST("cfg while loop");
    ast_expr_t *cond = ast_expr_alloc(AST_EXPR_BOOL);
    cond->data.bool_val = 1;
    ast_stmt_t *while_s = ast_stmt_alloc(AST_STMT_WHILE);
    while_s->data.while_.cond = cond;
    while_s->data.while_.body = ast_stmt_alloc(AST_STMT_BREAK);

    ast_stmt_t *body = ast_stmt_alloc(AST_STMT_COMPOUND);
    body->data.compound.count = 1;
    body->data.compound.stmts = calloc(1, sizeof(ast_stmt_t *));
    body->data.compound.stmts[0] = while_s;

    cfg_t *cfg = cfg_build(body);
    if (!cfg) { FAIL("cfg is NULL"); return; }
    if (cfg->block_count < 3) { FAIL("expected >= 3 blocks"); return; }
    cfg_free(cfg);
    ast_stmt_free(body);
    PASS();
}

static void test_cfg_print_no_crash(void) {
    TEST("cfg_print no crash");
    ast_stmt_t *body = ast_stmt_alloc(AST_STMT_COMPOUND);
    cfg_t *cfg = cfg_build(body);
    FILE *old = stdout;
    stdout = fopen("/dev/null", "w");
    cfg_print(cfg);
    fclose(stdout);
    stdout = old;
    cfg_free(cfg);
    ast_stmt_free(body);
    PASS();
}

int main(void) {
    printf("cfg tests\n");
    printf("--------\n");
    test_empty_body();
    test_seq_return();
    test_if_else();
    test_while_loop();
    test_cfg_print_no_crash();
    printf("\n%d/%d passed\n", passed, total);
    return passed == total ? 0 : 1;
}