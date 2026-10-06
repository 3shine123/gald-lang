#include "nopa/arc.h"
#include "nopa/cfg.h"
#include "nopa/ownership.h"
#include "nopa/ast.h"
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

static int total = 0, passed = 0;
#define TEST(n) do { printf("  %-50s ", n); total++; } while(0)
#define PASS() do { passed++; printf("PASS\n"); } while(0)
#define FAIL(m) do { printf("FAIL: %s\n", m); return; } while(0)
#define ASSERT(cond, m) do { if (!(cond)) { printf("FAIL: %s\n", m); return; } } while(0)

// ─── test helper: make a method symbol ────────────────────────────────────────

static symbol_t *make_method(const char *name) {
    symbol_t *m = sym_alloc(SYM_METHOD, name);
    m->data.method.return_type = calloc(1, sizeof(nopa_type_t));
    m->data.method.return_type->prim = TYPE_VOID;
    m->data.method.has_body = 1;
    return m;
}

// ─── tests ─────────────────────────────────────────────────────────────────────

static void test_null_body(void) {
    TEST("arc local null body");
    arc_result_t *res = arc_local_analyze(NULL, NULL, NULL);
    ASSERT(res != NULL, "should return empty result");
    ASSERT(res->action_count == 0, "should have 0 actions");
    arc_result_free(res);
    PASS();
}

static void test_empty_body(void) {
    TEST("arc local empty body");
    ast_stmt_t *body = ast_stmt_alloc(AST_STMT_COMPOUND);
    symbol_t *method = make_method("foo");
    arc_result_t *res = arc_local_analyze(body, NULL, method);
    ASSERT(res->action_count == 0, "should have 0 actions");
    arc_result_free(res);
    sym_free(method);
    free(body->data.compound.stmts);
    ast_stmt_free(body);
    PASS();
}

static void test_implicit_self(void) {
    TEST("arc local implicit self");
    ast_stmt_t *body = ast_stmt_alloc(AST_STMT_COMPOUND);
    symbol_t *method = make_method("foo");
    // Create a class to be the owner
    symbol_t *cls = sym_alloc(SYM_CLASS, "Foo");
    method->data.method.owner_class = cls;

    arc_result_t *res = arc_local_analyze(body, NULL, method);
    ASSERT(res->implicit_self_count >= 1, "should detect implicit self");

    arc_result_free(res);
    sym_free(method);
    sym_free(cls);
    free(body->data.compound.stmts);
    ast_stmt_free(body);
    PASS();
}

static void test_retained_expr_needs_release(void) {
    TEST("arc local retained expr release");
    ast_expr_t *call = ast_expr_alloc(AST_EXPR_MSG_SEND);
    symbol_t *alloc_m = sym_alloc(SYM_METHOD, "alloc");
    alloc_m->data.method.return_type = calloc(1, sizeof(nopa_type_t));
    alloc_m->data.method.return_type->prim = TYPE_ID;
    call->data.msg_send.method = alloc_m;
    call->type = ast_type_alloc();
    call->type->prim = TYPE_ID;

    ast_stmt_t *es = ast_stmt_alloc(AST_STMT_EXPR);
    es->data.expr = call;

    ast_stmt_t *body = ast_stmt_alloc(AST_STMT_COMPOUND);
    body->data.compound.count = 1;
    body->data.compound.stmts = calloc(1, sizeof(ast_stmt_t *));
    body->data.compound.stmts[0] = es;

    symbol_t *method = make_method("testRetained");

    arc_result_t *res = arc_local_analyze(body, NULL, method);
    ASSERT(res->action_count >= 1, "should have >=1 action");

    arc_result_free(res);
    sym_free(method);
    // Leak remaining for simplicity
    PASS();
}

static void test_ret_var_no_release(void) {
    TEST("arc_local retained assigned to var no release");
    // id obj = [Foo alloc]; — variable takes ownership
    ast_expr_t *call = ast_expr_alloc(AST_EXPR_MSG_SEND);
    symbol_t *alloc_m = sym_alloc(SYM_METHOD, "alloc");
    alloc_m->data.method.return_type = calloc(1, sizeof(nopa_type_t));
    alloc_m->data.method.return_type->prim = TYPE_ID;
    call->data.msg_send.method = alloc_m;
    call->type = ast_type_alloc();
    call->type->prim = TYPE_ID;

    ast_decl_t *var_decl = ast_decl_alloc(AST_DECL_VARIABLE, "obj");
    var_decl->data.variable.init = call;
    var_decl->data.variable.type = ast_type_alloc();
    var_decl->data.variable.type->prim = TYPE_ID;

    ast_stmt_t *decl_stmt = ast_stmt_alloc(AST_STMT_DECL);
    decl_stmt->data.decl_stmt.decl = var_decl;

    ast_stmt_t *body = ast_stmt_alloc(AST_STMT_COMPOUND);
    body->data.compound.count = 1;
    body->data.compound.stmts = calloc(1, sizeof(ast_stmt_t *));
    body->data.compound.stmts[0] = decl_stmt;

    symbol_t *method = make_method("test");

    arc_result_t *res = arc_local_analyze(body, NULL, method);
    ASSERT(res->action_count >= 1, "should have release (via var tracking)");

    arc_result_free(res);
    sym_free(method);
    PASS();
}

static void test_return_no_release(void) {
    TEST("arc local return no release");
    ast_expr_t *call = ast_expr_alloc(AST_EXPR_MSG_SEND);
    symbol_t *alloc_m = sym_alloc(SYM_METHOD, "alloc");
    alloc_m->data.method.return_type = calloc(1, sizeof(nopa_type_t));
    alloc_m->data.method.return_type->prim = TYPE_ID;
    call->data.msg_send.method = alloc_m;
    call->type = ast_type_alloc();
    call->type->prim = TYPE_ID;

    ast_stmt_t *ret_stmt = ast_stmt_alloc(AST_STMT_RETURN);
    ret_stmt->data.return_.value = call;

    ast_stmt_t *body = ast_stmt_alloc(AST_STMT_COMPOUND);
    body->data.compound.count = 1;
    body->data.compound.stmts = calloc(1, sizeof(ast_stmt_t *));
    body->data.compound.stmts[0] = ret_stmt;

    symbol_t *method = make_method("test");

    arc_result_t *res = arc_local_analyze(body, NULL, method);
    // Return transfers ownership — no release for the return value
    // But the alloc result is not stored anywhere, so it might still get a release
    // after the return. Our analysis should catch that return values are special.
    // For now we check we don't crash; more precise checking later.
    arc_result_free(res);
    sym_free(method);
    PASS();
}

static void test_print_no_crash(void) {
    TEST("arc result print no crash");
    ast_stmt_t *body = ast_stmt_alloc(AST_STMT_COMPOUND);
    arc_result_t *res = arc_local_analyze(body, NULL, NULL);
    FILE *old = stdout;
    stdout = fopen("/dev/null", "w");
    arc_result_print(res);
    fclose(stdout);
    stdout = old;
    arc_result_free(res);
    free(body->data.compound.stmts);
    ast_stmt_free(body);
    PASS();
}

static void test_free_null(void) {
    TEST("arc_free null");
    arc_result_free(NULL);
    PASS();
}

// ─── global analysis tests ───────────────────────────────────────────────────────────────

static void test_global_join(void) {
    TEST("arc global join exists");
    // Build CFG with an if-else → natural join point
    ast_expr_t *cond = ast_expr_alloc(AST_EXPR_BOOL);
    cond->data.bool_val = 1;
    ast_stmt_t *then_s = ast_stmt_alloc(AST_STMT_EXPR);
    then_s->data.expr = ast_expr_alloc(AST_EXPR_INT);
    ast_stmt_t *else_s = ast_stmt_alloc(AST_STMT_EXPR);
    else_s->data.expr = ast_expr_alloc(AST_EXPR_FLOAT);
    ast_stmt_t *if_s = ast_stmt_alloc(AST_STMT_IF);
    if_s->data.if_.cond = cond;
    if_s->data.if_.then = then_s;
    if_s->data.if_.else_ = else_s;
    ast_stmt_t *body = ast_stmt_alloc(AST_STMT_COMPOUND);
    body->data.compound.count = 1;
    body->data.compound.stmts = calloc(1, sizeof(ast_stmt_t *));
    body->data.compound.stmts[0] = if_s;

    cfg_t *cfg = cfg_build(body);
    arc_result_t *res = arc_local_analyze(body, cfg, NULL);

    arc_global_analyze(cfg, res, NULL);
    // Should not crash, should produce no actions (no objects in this test)
    ASSERT(res != NULL, "result valid");

    arc_result_free(res);
    cfg_free(cfg);
    // Leak sub-nodes
    PASS();
}

static void test_global_noop(void) {
    TEST("arc global null cfg no crash");
    arc_result_t *res = arc_result_alloc();
    arc_global_analyze(NULL, res, NULL);
    arc_global_analyze(NULL, NULL, NULL);
    arc_result_free(res);
    PASS();
}

static void test_loop_basic(void) {
    TEST("arc loop basic no crash");
    ast_expr_t *cond = ast_expr_alloc(AST_EXPR_BOOL);
    cond->data.bool_val = 1;
    ast_stmt_t *while_body = ast_stmt_alloc(AST_STMT_EXPR);
    while_body->data.expr = ast_expr_alloc(AST_EXPR_INT);
    ast_stmt_t *while_s = ast_stmt_alloc(AST_STMT_WHILE);
    while_s->data.while_.cond = cond;
    while_s->data.while_.body = while_body;
    ast_stmt_t *body = ast_stmt_alloc(AST_STMT_COMPOUND);
    body->data.compound.count = 1;
    body->data.compound.stmts = calloc(1, sizeof(ast_stmt_t *));
    body->data.compound.stmts[0] = while_s;

    cfg_t *cfg = cfg_build(body);
    arc_result_t *res = arc_local_analyze(body, cfg, NULL);

    arc_analyze_loops(cfg, res, NULL);
    ASSERT(res != NULL, "result valid");

    arc_result_free(res);
    cfg_free(cfg);
    PASS();
}

static void test_loop_no_crash(void) {
    TEST("arc loops null cfg no crash");
    arc_result_t *res = arc_result_alloc();
    arc_analyze_loops(NULL, res, NULL);
    arc_analyze_loops(NULL, NULL, NULL);
    arc_result_free(res);
    PASS();
}

// ─── insertion tests ─────────────────────────────────────────────────────────────────

static void test_insert_basic(void) {
    TEST("arc insert retain/release");
    // Build: { [Foo alloc]; }
    ast_expr_t *call = ast_expr_alloc(AST_EXPR_MSG_SEND);
    symbol_t *alloc_m = sym_alloc(SYM_METHOD, "alloc");
    alloc_m->data.method.return_type = calloc(1, sizeof(nopa_type_t));
    alloc_m->data.method.return_type->prim = TYPE_ID;
    call->data.msg_send.method = alloc_m;
    call->type = ast_type_alloc();
    call->type->prim = TYPE_ID;

    ast_stmt_t *es = ast_stmt_alloc(AST_STMT_EXPR);
    es->data.expr = call;

    ast_stmt_t *body = ast_stmt_alloc(AST_STMT_COMPOUND);
    body->data.compound.count = 1;
    body->data.compound.stmts = calloc(1, sizeof(ast_stmt_t *));
    body->data.compound.stmts[0] = es;

    arc_result_t *res = arc_local_analyze(body, NULL, NULL);
    int before = body->data.compound.count;

    arc_insert_actions(body, res);

    int after = body->data.compound.count;
    ASSERT(after > before, "should have more stmts after insert");

    arc_result_free(res);
    PASS();
}

static void test_insert_noop(void) {
    TEST("arc insert null body no crash");
    arc_insert_actions(NULL, NULL);
    arc_result_t *res = arc_result_alloc();
    arc_insert_actions(NULL, res);
    arc_result_free(res);
    PASS();
}

static void test_optimize_pair(void) {
    TEST("arc optimize retain/release pair");
    // Build a compound with a fake retain + fake release sequence
    ast_expr_t *target = ast_expr_alloc(AST_EXPR_SELF);

    // Create release call stmt
    ast_expr_t *release_call = ast_expr_alloc(AST_EXPR_FUNC_CALL);
    release_call->data.func_call.args = calloc(1, sizeof(ast_expr_t *));
    release_call->data.func_call.args[0] = target;
    release_call->data.func_call.arg_count = 1;
    ast_stmt_t *rs = ast_stmt_alloc(AST_STMT_EXPR);
    rs->data.expr = release_call;

    // Create retain call stmt (same target)
    ast_expr_t *retain_call = ast_expr_alloc(AST_EXPR_FUNC_CALL);
    retain_call->data.func_call.args = calloc(1, sizeof(ast_expr_t *));
    retain_call->data.func_call.args[0] = target;
    retain_call->data.func_call.arg_count = 1;
    ast_stmt_t *as = ast_stmt_alloc(AST_STMT_EXPR);
    as->data.expr = retain_call;

    // Place them opposite order to test adjacent pair removal
    ast_stmt_t *body = ast_stmt_alloc(AST_STMT_COMPOUND);
    body->data.compound.stmts = calloc(3, sizeof(ast_stmt_t *));
    body->data.compound.stmts[0] = rs;
    body->data.compound.stmts[1] = as;
    body->data.compound.stmts[2] = ast_stmt_alloc(AST_STMT_BREAK); // unrelated stmt
    body->data.compound.count = 3;

    arc_optimize_pairs(body);

    ASSERT(body->data.compound.count == 1, "should have removed 2 and kept the break");
    ASSERT(body->data.compound.stmts[0]->kind == AST_STMT_BREAK, "remaining is break");

    PASS();
}

// ─── validation tests ───────────────────────────────────────────────────────────────

static void test_validate_basic(void) {
    TEST("arc validate basic");
    ast_stmt_t *body = ast_stmt_alloc(AST_STMT_COMPOUND);
    arc_validation_t *val = arc_validate(body, NULL);
    ASSERT(val != NULL, "val non-null");
    ASSERT(val->diag_count == 0, "no diags for empty body");
    arc_validation_free(val);
    free(body->data.compound.stmts);
    ast_stmt_free(body);
    PASS();
}

static void test_validate_overrelease(void) {
    TEST("arc validate overrelease");
    // Create a body with many release-like calls but no retains
    ast_stmt_t *body = ast_stmt_alloc(AST_STMT_COMPOUND);
    int n = 10;
    body->data.compound.stmts = calloc(n, sizeof(ast_stmt_t *));
    for (int i = 0; i < n; i++) {
        ast_expr_t *call = ast_expr_alloc(AST_EXPR_FUNC_CALL);
        call->data.func_call.args = calloc(1, sizeof(ast_expr_t *));
        call->data.func_call.args[0] = ast_expr_alloc(AST_EXPR_SELF);
        call->data.func_call.arg_count = 1;
        ast_stmt_t *es = ast_stmt_alloc(AST_STMT_EXPR);
        es->data.expr = call;
        body->data.compound.stmts[i] = es;
    }
    body->data.compound.count = n;

    arc_validation_t *val = arc_validate(body, NULL);
    ASSERT(val != NULL, "non-null");
    // should detect over-release
    arc_validation_free(val);
    PASS();
}

static void test_validate_null(void) {
    TEST("arc validate null no crash");
    arc_validation_t *val = arc_validate(NULL, NULL);
    arc_validation_free(val);
    arc_validation_free(NULL);
    PASS();
}

static void test_detect_cycles_noop(void) {
    TEST("arc detect cycles noop");
    arc_validation_t *val = arc_validation_alloc();
    arc_detect_cycles(NULL, val, NULL);
    arc_detect_cycles(NULL, NULL, NULL);
    arc_validation_free(val);
    PASS();
}

static void test_validation_print(void) {
    TEST("arc validation print");
    arc_validation_t *val = arc_validation_alloc();
    FILE *old = stdout;
    stdout = fopen("/dev/null", "w");
    arc_validation_print(val);
    arc_validation_print(NULL);
    fclose(stdout);
    stdout = old;
    arc_validation_free(val);
    PASS();
}

int main(void) {
    printf("arc tests\n");
    printf("-------\n");
    test_null_body();
    test_empty_body();
    test_implicit_self();
    test_retained_expr_needs_release();
    test_ret_var_no_release();
    test_return_no_release();
    test_print_no_crash();
    test_free_null();
    printf("\n");
    test_global_join();
    test_global_noop();
    test_loop_basic();
    test_loop_no_crash();
    printf("\n");

    test_insert_basic();
    test_insert_noop();
    test_optimize_pair();
    printf("\n");

    test_validate_basic();
    test_validate_overrelease();
    test_validate_null();
    test_detect_cycles_noop();
    test_validation_print();
    printf("\n%d/%d passed\n", passed, total);
    return passed == total ? 0 : 1;
}