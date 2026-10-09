#include "ovic/ownership.h"
#include "ovic/symbol.h"
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

static int total = 0, passed = 0;
#define TEST(n) do { printf("  %-50s ", n); total++; } while(0)
#define PASS() do { passed++; printf("PASS\n"); } while(0)
#define FAIL(m) do { printf("FAIL: %s\n", m); return; } while(0)
#define ASSERT(cond, m) do { if (!(cond)) { printf("FAIL: %s\n", m); return; } } while(0)

static symbol_t *make_method(const char *name) {
    symbol_t *m = sym_alloc(SYM_METHOD, name);
    m->data.method.return_type = calloc(1, sizeof(ovic_type_t));
    m->data.method.return_type->prim = TYPE_ID;
    return m;
}

static void test_null_method(void) {
    TEST("ownership NULL method");
    ASSERT(ownership_for_method(NULL) == OWN_RETAINED, "should be retained");
    PASS();
}

static void test_alloc_method(void) {
    TEST("ownership alloc method");
    symbol_t *m = make_method("alloc");
    ASSERT(ownership_for_method(m) == OWN_RETAINED, "alloc -> retained");
    sym_free(m);
    PASS();
}

static void test_new_method(void) {
    TEST("ownership new method");
    symbol_t *m = make_method("newWithInt:");
    ASSERT(ownership_for_method(m) == OWN_RETAINED, "new -> retained");
    sym_free(m);
    PASS();
}

static void test_init_method(void) {
    TEST("ownership init method");
    symbol_t *m = make_method("initWithInt:");
    ASSERT(ownership_for_method(m) == OWN_UNRETAINED, "init -> unretained");
    sym_free(m);
    PASS();
}

static void test_copy_method(void) {
    TEST("ownership copy method");
    symbol_t *m = make_method("copyWithZone:");
    ASSERT(ownership_for_method(m) == OWN_RETAINED, "copy -> retained");
    sym_free(m);
    PASS();
}

static void test_mutablecopy_method(void) {
    TEST("ownership mutableCopy method");
    symbol_t *m = make_method("mutableCopyWithZone:");
    ASSERT(ownership_for_method(m) == OWN_RETAINED, "mutableCopy -> retained");
    sym_free(m);
    PASS();
}

static void test_regular_method(void) {
    TEST("ownership regular method");
    symbol_t *m = make_method("doSomething");
    ASSERT(ownership_for_method(m) == OWN_RETAINED, "default -> retained");
    sym_free(m);
    PASS();
}

// ─── expression tests ────────────────────────────────────────────────────────────

static void test_expr_int(void) {
    TEST("ownership int literal");
    ast_expr_t *e = ast_expr_alloc(AST_EXPR_INT);
    ASSERT(ownership_for_expr(e) == OWN_UNRETAINED, "int -> unretained");
    ast_expr_free(e);
    PASS();
}

static void test_expr_string(void) {
    TEST("ownership string literal");
    ast_expr_t *e = ast_expr_alloc(AST_EXPR_STRING);
    ASSERT(ownership_for_expr(e) == OWN_UNRETAINED, "string -> unretained");
    ast_expr_free(e);
    PASS();
}

static void test_expr_nil(void) {
    TEST("ownership nil");
    ast_expr_t *e = ast_expr_alloc(AST_EXPR_NIL);
    ASSERT(ownership_for_expr(e) == OWN_UNRETAINED, "nil -> unretained");
    ast_expr_free(e);
    PASS();
}

static void test_expr_self(void) {
    TEST("ownership self");
    ast_expr_t *e = ast_expr_alloc(AST_EXPR_SELF);
    ASSERT(ownership_for_expr(e) == OWN_UNRETAINED, "self -> unretained");
    ast_expr_free(e);
    PASS();
}

static void test_expr_msg_send(void) {
    TEST("ownership msg send [obj alloc]");
    ast_expr_t *e = ast_expr_alloc(AST_EXPR_MSG_SEND);
    symbol_t *m = make_method("alloc");
    e->data.msg_send.method = m;
    ASSERT(ownership_for_expr(e) == OWN_RETAINED, "alloc msg -> retained");
    ast_expr_free(e);
    sym_free(m);
    PASS();
}

static void test_expr_msg_send_init(void) {
    TEST("ownership msg send init");
    ast_expr_t *e = ast_expr_alloc(AST_EXPR_MSG_SEND);
    symbol_t *m = make_method("init");
    e->data.msg_send.method = m;
    ownership_t o = ownership_for_expr(e);
    if (o != OWN_UNRETAINED) { FAIL("expected unretained, got retained"); return; }
    ast_expr_free(e);
    sym_free(m);
    PASS();
}

static void test_expr_block(void) {
    TEST("ownership block literal");
    ast_expr_t *e = ast_expr_alloc(AST_EXPR_BLOCK_LIT);
    ASSERT(ownership_for_expr(e) == OWN_RETAINED, "block lit -> retained");
    ast_expr_free(e);
    PASS();
}

static void test_expr_array_lit(void) {
    TEST("ownership array literal @[]");
    ast_expr_t *e = ast_expr_alloc(AST_EXPR_ARRAY_LIT);
    ASSERT(ownership_for_expr(e) == OWN_RETAINED, "@[] -> retained");
    ast_expr_free(e);
    PASS();
}

// ─── stringify tests ─────────────────────────────────────────────────────────────

static void test_ownership_name(void) {
    TEST("ownership_name strings");
    ASSERT(strcmp(ownership_name(OWN_RETAINED), "retained") == 0, "retained string");
    ASSERT(strcmp(ownership_name(OWN_UNRETAINED), "unretained") == 0, "unretained string");
    ASSERT(strcmp(ownership_name(OWN_AUTORELEASED), "autoreleased") == 0, "autoreleased string");
    ASSERT(strcmp(ownership_name(OWN_UNKNOWN), "unknown") == 0, "unknown string");
    PASS();
}

int main(void) {
    printf("ownership tests\n");
    printf("--------------\n");
    test_null_method();
    test_alloc_method();
    test_new_method();
    test_init_method();
    test_copy_method();
    test_mutablecopy_method();
    test_regular_method();
    test_expr_int();
    test_expr_string();
    test_expr_nil();
    test_expr_self();
    test_expr_msg_send();
    test_expr_msg_send_init();
    test_expr_block();
    test_expr_array_lit();
    test_ownership_name();
    printf("\n%d/%d passed\n", passed, total);
    return passed == total ? 0 : 1;
}