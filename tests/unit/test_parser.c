#include "nopa/parser.h"
#include "nopa/lexer.h"
#include <stdio.h>
#include <string.h>
#include <stdlib.h>

static int total = 0;
static int passed = 0;

#define TEST(name) do { printf("  %-40s ", name); total++; } while(0)
#define PASS() do { passed++; printf("PASS\n"); } while(0)
#define FAIL(msg) do { printf("FAIL: %s\n", msg); return; } while(0)

static translation_unit_t *parse_string(const char *src) {
    lexer_t lexer;
    lexer_init(&lexer, src, strlen(src), "test.np");
    parser_t *p = parser_create(&lexer);
    translation_unit_t *unit = parser_parse_translation_unit(p);
    parser_destroy(p);
    return unit;
}

// ── declaration tests ──────────────────────────────────────────────────────

static void test_empty(void) {
    TEST("empty source");
    translation_unit_t *u = parse_string("");
    if (!u) { FAIL("unit is NULL"); return; }
    if (u->decl_count != 0) { FAIL("expected 0 decls"); return; }
    cst_unit_free(u);
    PASS();
}

static void test_interface_simple(void) {
    TEST("@interface Foo @end");
    translation_unit_t *u = parse_string("@interface Foo @end");
    if (!u) { FAIL("unit is NULL"); return; }
    if (u->decl_count != 1) { FAIL("expected 1 decl"); return; }
    cst_decl_t *d = u->decls[0];
    if (d->kind != CST_DECL_CLASS_INTERFACE) { FAIL("expected class interface"); return; }
    cst_unit_free(u);
    PASS();
}

static void test_interface_superclass(void) {
    TEST("@interface Foo : Bar @end");
    translation_unit_t *u = parse_string("@interface Foo : Bar @end");
    if (!u) { FAIL("unit is NULL"); return; }
    if (u->decl_count != 1) { FAIL("expected 1 decl"); return; }
    cst_decl_t *d = u->decls[0];
    if (d->kind != CST_DECL_CLASS_INTERFACE) { FAIL("expected class interface"); return; }
    if (!d->data.class_.superclass) { FAIL("expected superclass"); return; }
    if (strcmp(d->data.class_.superclass, "Bar") != 0) { FAIL("superclass != Bar"); return; }
    cst_unit_free(u);
    PASS();
}

static void test_interface_protocols(void) {
    TEST("@interface Foo <Proto1, Proto2> @end");
    translation_unit_t *u = parse_string("@interface Foo <Proto1, Proto2> @end");
    if (!u) { FAIL("unit is NULL"); return; }
    if (u->decl_count != 1) { FAIL("expected 1 decl"); return; }
    cst_decl_t *d = u->decls[0];
    if (d->kind != CST_DECL_CLASS_INTERFACE) { FAIL("expected class interface"); return; }
    if (d->data.class_.protocol_count != 2) { FAIL("expected 2 protocols"); return; }
    cst_unit_free(u);
    PASS();
}

static void test_interface_superclass_protocols(void) {
    TEST("@interface Foo : Bar <Proto> @end");
    translation_unit_t *u = parse_string("@interface Foo : Bar <Proto> @end");
    if (!u) { FAIL("unit is NULL"); return; }
    if (u->decl_count != 1) { FAIL("expected 1 decl"); return; }
    cst_decl_t *d = u->decls[0];
    if (d->kind != CST_DECL_CLASS_INTERFACE) { FAIL("expected class interface"); return; }
    if (!d->data.class_.superclass) { FAIL("expected superclass"); return; }
    if (d->data.class_.protocol_count != 1) { FAIL("expected 1 protocol"); return; }
    cst_unit_free(u);
    PASS();
}

static void test_interface_ivars(void) {
    TEST("@interface Foo { int x; id obj; } @end");
    translation_unit_t *u = parse_string("@interface Foo { int x; id obj; } @end");
    if (!u) { FAIL("unit is NULL"); return; }
    if (u->decl_count != 1) { FAIL("expected 1 decl"); return; }
    cst_decl_t *d = u->decls[0];
    if (d->kind != CST_DECL_CLASS_INTERFACE) { FAIL("expected class interface"); return; }
    if (d->data.class_.ivar_count != 2) { FAIL("expected 2 ivars"); return; }
    cst_unit_free(u);
    PASS();
}

static void test_interface_method_decl(void) {
    TEST("@interface Foo - (void)method; +(int)classMethod; @end");
    translation_unit_t *u = parse_string("@interface Foo - (void)method; +(int)classMethod; @end");
    if (!u) { FAIL("unit is NULL"); return; }
    if (u->decl_count != 1) { FAIL("expected 1 decl"); return; }
    cst_decl_t *d = u->decls[0];
    if (d->kind != CST_DECL_CLASS_INTERFACE) { FAIL("expected class interface"); return; }
    if (d->data.class_.method_count != 2) { FAIL("expected 2 methods"); return; }
    cst_decl_t *m0 = d->data.class_.methods[0];
    if (m0->kind != CST_DECL_METHOD) { FAIL("expected method decl"); return; }
    if (m0->data.method.is_class_method) { FAIL("expected instance method"); return; }
    cst_decl_t *m1 = d->data.class_.methods[1];
    if (m1->kind != CST_DECL_METHOD) { FAIL("expected method decl"); return; }
    if (!m1->data.method.is_class_method) { FAIL("expected class method"); return; }
    cst_unit_free(u);
    PASS();
}

static void test_interface_method_with_args(void) {
    TEST("@interface Foo - (void)setName:(NSString *)name age:(int)age; @end");
    translation_unit_t *u = parse_string("@interface Foo - (void)setName:(NSString *)name age:(int)age; @end");
    if (!u) { FAIL("unit is NULL"); return; }
    if (u->decl_count != 1) { FAIL("expected 1 decl"); return; }
    cst_decl_t *d = u->decls[0];
    if (d->kind != CST_DECL_CLASS_INTERFACE) { FAIL("expected class interface"); return; }
    if (d->data.class_.method_count != 1) { FAIL("expected 1 method"); return; }
    cst_decl_t *m = d->data.class_.methods[0];
    if (m->kind != CST_DECL_METHOD) { FAIL("expected method decl"); return; }
    cst_unit_free(u);
    PASS();
}

static void test_interface_property(void) {
    TEST("@interface Foo @property int age; @end");
    translation_unit_t *u = parse_string("@interface Foo @property int age; @end");
    if (!u) { FAIL("unit is NULL"); return; }
    if (u->decl_count != 1) { FAIL("expected 1 decl"); return; }
    cst_decl_t *d = u->decls[0];
    if (d->kind != CST_DECL_CLASS_INTERFACE) { FAIL("expected class interface"); return; }
    if (d->data.class_.property_count != 1) { FAIL("expected 1 property"); return; }
    cst_unit_free(u);
    PASS();
}

static void test_interface_property_attributes(void) {
    TEST("@interface Foo @property(readonly, weak) id delegate; @end");
    translation_unit_t *u = parse_string("@interface Foo @property(readonly, weak) id delegate; @end");
    if (!u) { FAIL("unit is NULL"); return; }
    if (u->decl_count != 1) { FAIL("expected 1 decl"); return; }
    cst_decl_t *d = u->decls[0];
    if (d->kind != CST_DECL_CLASS_INTERFACE) { FAIL("expected class interface"); return; }
    if (d->data.class_.property_count != 1) { FAIL("expected 1 property"); return; }
    cst_decl_t *p = d->data.class_.properties[0];
    if (!p->data.property.is_readonly) { FAIL("expected readonly"); return; }
    if (!p->data.property.is_weak) { FAIL("expected weak"); return; }
    cst_unit_free(u);
    PASS();
}

static void test_implementation_simple(void) {
    TEST("@implementation Foo @end");
    translation_unit_t *u = parse_string("@implementation Foo @end");
    if (!u) { FAIL("unit is NULL"); return; }
    if (u->decl_count != 1) { FAIL("expected 1 decl"); return; }
    if (u->decls[0]->kind != CST_DECL_CLASS_IMPLEMENTATION) { FAIL("expected class impl"); return; }
    cst_unit_free(u);
    PASS();
}

static void test_implementation_method_body(void) {
    TEST("@implementation Foo - (void)bar { ; } @end");
    translation_unit_t *u = parse_string("@implementation Foo - (void)bar { ; } @end");
    if (!u) { FAIL("unit is NULL"); return; }
    if (u->decl_count != 1) { FAIL("expected 1 decl"); return; }
    cst_decl_t *d = u->decls[0];
    if (d->kind != CST_DECL_CLASS_IMPLEMENTATION) { FAIL("expected class impl"); return; }
    if (d->data.class_.method_count != 1) { FAIL("expected 1 method"); return; }
    cst_decl_t *m = d->data.class_.methods[0];
    if (m->kind != CST_DECL_METHOD) { FAIL("expected method decl"); return; }
    if (!m->data.method.body) { FAIL("expected method body"); return; }
    cst_unit_free(u);
    PASS();
}

static void test_implementation_method_body_return(void) {
    TEST("@implementation Foo - (int)bar { return 42; } @end");
    translation_unit_t *u = parse_string("@implementation Foo - (int)bar { return 42; } @end");
    if (!u) { FAIL("unit is NULL"); return; }
    if (u->decl_count != 1) { FAIL("expected 1 decl"); return; }
    cst_decl_t *d = u->decls[0];
    if (d->kind != CST_DECL_CLASS_IMPLEMENTATION) { FAIL("expected class impl"); return; }
    if (d->data.class_.method_count != 1) { FAIL("expected 1 method"); return; }
    cst_decl_t *m = d->data.class_.methods[0];
    if (m->kind != CST_DECL_METHOD) { FAIL("expected method decl"); return; }
    if (!m->data.method.body) { FAIL("expected method body"); return; }
    cst_unit_free(u);
    PASS();
}

static void test_forward_class(void) {
    TEST("@class Foo, Bar;");
    translation_unit_t *u = parse_string("@class Foo, Bar;");
    if (!u) { FAIL("unit is NULL"); return; }
    if (u->decl_count != 1) { FAIL("expected 1 decl"); return; }
    if (u->decls[0]->kind != CST_DECL_FORWARD_CLASS) { FAIL("expected forward class"); return; }
    cst_unit_free(u);
    PASS();
}

static void test_protocol_decl(void) {
    TEST("@protocol MyProto - (void)requiredMethod; @end");
    translation_unit_t *u = parse_string("@protocol MyProto - (void)requiredMethod; @end");
    if (!u) { FAIL("unit is NULL"); return; }
    if (u->decl_count != 1) { FAIL("expected 1 decl"); return; }
    cst_decl_t *d = u->decls[0];
    if (d->kind != CST_DECL_PROTOCOL) { FAIL("expected protocol"); return; }
    if (d->data.protocol.method_count != 1) { FAIL("expected 1 method"); return; }
    cst_unit_free(u);
    PASS();
}

static void test_protocol_with_parent(void) {
    TEST("@protocol MyProto <BaseProto> - (void)method; @end");
    translation_unit_t *u = parse_string("@protocol MyProto <BaseProto> - (void)method; @end");
    if (!u) { FAIL("unit is NULL"); return; }
    if (u->decl_count != 1) { FAIL("expected 1 decl"); return; }
    cst_decl_t *d = u->decls[0];
    if (d->kind != CST_DECL_PROTOCOL) { FAIL("expected protocol"); return; }
    if (d->data.protocol.protocol_count != 1) { FAIL("expected 1 parent protocol"); return; }
    if (d->data.protocol.method_count != 1) { FAIL("expected 1 method"); return; }
    cst_unit_free(u);
    PASS();
}

// ── statement tests (inside method bodies) ─────────────────────────────────

static void test_stmt_if(void) {
    TEST("if/else inside method");
    translation_unit_t *u = parse_string(
        "@implementation Foo - (void)foo { if (x) { return 1; } else { return 2; } } @end");
    if (!u) { FAIL("unit is NULL"); return; }
    if (u->decl_count != 1) { FAIL("expected 1 decl"); return; }
    cst_decl_t *d = u->decls[0];
    if (d->kind != CST_DECL_CLASS_IMPLEMENTATION) { FAIL("expected class impl"); return; }
    if (d->data.class_.method_count != 1) { FAIL("expected 1 method"); return; }
    cst_unit_free(u);
    PASS();
}

static void test_stmt_while(void) {
    TEST("while inside method");
    translation_unit_t *u = parse_string(
        "@implementation Foo - (void)foo { while (i < 10) { i++; } } @end");
    if (!u) { FAIL("unit is NULL"); return; }
    if (u->decl_count != 1) { FAIL("expected 1 decl"); return; }
    cst_decl_t *d = u->decls[0];
    if (d->data.class_.method_count < 1) { FAIL("expected method"); return; }
    cst_unit_free(u);
    PASS();
}

static void test_stmt_do_while(void) {
    TEST("do-while inside method");
    translation_unit_t *u = parse_string(
        "@implementation Foo - (void)foo { do { i++; } while (i < 10); } @end");
    if (!u) { FAIL("unit is NULL"); return; }
    if (u->decl_count != 1) { FAIL("expected 1 decl"); return; }
    cst_unit_free(u);
    PASS();
}

static void test_stmt_for(void) {
    TEST("for loop inside method");
    translation_unit_t *u = parse_string(
        "@implementation Foo - (void)foo { for (i = 0; i < 10; i++) { ; } } @end");
    if (!u) { FAIL("unit is NULL"); return; }
    if (u->decl_count != 1) { FAIL("expected 1 decl"); return; }
    cst_unit_free(u);
    PASS();
}

static void test_stmt_break_continue(void) {
    TEST("break/continue inside method");
    translation_unit_t *u = parse_string(
        "@implementation Foo - (void)foo { while (1) { break; continue; } } @end");
    if (!u) { FAIL("unit is NULL"); return; }
    if (u->decl_count != 1) { FAIL("expected 1 decl"); return; }
    cst_unit_free(u);
    PASS();
}

static void test_stmt_switch(void) {
    TEST("switch inside method");
    translation_unit_t *u = parse_string(
        "@implementation Foo - (void)foo { switch (x) { break; } } @end");
    if (!u) { FAIL("unit is NULL"); return; }
    if (u->decl_count != 1) { FAIL("expected 1 decl"); return; }
    cst_unit_free(u);
    PASS();
}

static void test_stmt_return_expr(void) {
    TEST("return expression inside method");
    translation_unit_t *u = parse_string(
        "@implementation Foo - (int)foo { return x + 1; } @end");
    if (!u) { FAIL("unit is NULL"); return; }
    if (u->decl_count != 1) { FAIL("expected 1 decl"); return; }
    cst_unit_free(u);
    PASS();
}

// ── expression tests (inside method bodies) ────────────────────────────────

static void test_expr_message_send_no_args(void) {
    TEST("message send [self doSomething]");
    translation_unit_t *u = parse_string(
        "@implementation Foo - (void)foo { [self doSomething]; } @end");
    if (!u) { FAIL("unit is NULL"); return; }
    if (u->decl_count != 1) { FAIL("expected 1 decl"); return; }
    cst_unit_free(u);
    PASS();
}

static void test_expr_message_send_args(void) {
    TEST("message send [obj setValue:42]");
    translation_unit_t *u = parse_string(
        "@implementation Foo - (void)foo { [obj setValue:42]; } @end");
    if (!u) { FAIL("unit is NULL"); return; }
    if (u->decl_count != 1) { FAIL("expected 1 decl"); return; }
    cst_unit_free(u);
    PASS();
}

static void test_expr_message_send_multi_args(void) {
    TEST("message send [obj setX:1 y:2]");
    translation_unit_t *u = parse_string(
        "@implementation Foo - (void)foo { [obj setX:1 y:2]; } @end");
    if (!u) { FAIL("unit is NULL"); return; }
    if (u->decl_count != 1) { FAIL("expected 1 decl"); return; }
    cst_unit_free(u);
    PASS();
}

static void test_expr_arithmetic(void) {
    TEST("arithmetic 1 + 2 * 3");
    translation_unit_t *u = parse_string(
        "@implementation Foo - (void)foo { 1 + 2 * 3; } @end");
    if (!u) { FAIL("unit is NULL"); return; }
    if (u->decl_count != 1) { FAIL("expected 1 decl"); return; }
    cst_unit_free(u);
    PASS();
}

static void test_expr_arithmetic_parens(void) {
    TEST("arithmetic (a + b) * c");
    translation_unit_t *u = parse_string(
        "@implementation Foo - (void)foo { (a + b) * c; } @end");
    if (!u) { FAIL("unit is NULL"); return; }
    if (u->decl_count != 1) { FAIL("expected 1 decl"); return; }
    cst_unit_free(u);
    PASS();
}

static void test_expr_comparison(void) {
    TEST("comparison x == y");
    translation_unit_t *u = parse_string(
        "@implementation Foo - (void)foo { x == y; } @end");
    if (!u) { FAIL("unit is NULL"); return; }
    if (u->decl_count != 1) { FAIL("expected 1 decl"); return; }
    cst_unit_free(u);
    PASS();
}

static void test_expr_comparison_chain(void) {
    TEST("comparison x != y && x < y");
    translation_unit_t *u = parse_string(
        "@implementation Foo - (void)foo { x != y && x < y; } @end");
    if (!u) { FAIL("unit is NULL"); return; }
    if (u->decl_count != 1) { FAIL("expected 1 decl"); return; }
    cst_unit_free(u);
    PASS();
}

static void test_expr_assignment(void) {
    TEST("assignment x = 42");
    translation_unit_t *u = parse_string(
        "@implementation Foo - (void)foo { x = 42; } @end");
    if (!u) { FAIL("unit is NULL"); return; }
    if (u->decl_count != 1) { FAIL("expected 1 decl"); return; }
    cst_unit_free(u);
    PASS();
}

static void test_expr_compound_assign(void) {
    TEST("compound assignment x += 1");
    translation_unit_t *u = parse_string(
        "@implementation Foo - (void)foo { x += 1; } @end");
    if (!u) { FAIL("unit is NULL"); return; }
    if (u->decl_count != 1) { FAIL("expected 1 decl"); return; }
    cst_unit_free(u);
    PASS();
}

static void test_expr_unary_minus(void) {
    TEST("unary -x");
    translation_unit_t *u = parse_string(
        "@implementation Foo - (void)foo { -x; } @end");
    if (!u) { FAIL("unit is NULL"); return; }
    if (u->decl_count != 1) { FAIL("expected 1 decl"); return; }
    cst_unit_free(u);
    PASS();
}

static void test_expr_unary_not(void) {
    TEST("unary !flag");
    translation_unit_t *u = parse_string(
        "@implementation Foo - (void)foo { !flag; } @end");
    if (!u) { FAIL("unit is NULL"); return; }
    if (u->decl_count != 1) { FAIL("expected 1 decl"); return; }
    cst_unit_free(u);
    PASS();
}

static void test_expr_unary_deref(void) {
    TEST("unary *ptr");
    translation_unit_t *u = parse_string(
        "@implementation Foo - (void)foo { *ptr; } @end");
    if (!u) { FAIL("unit is NULL"); return; }
    if (u->decl_count != 1) { FAIL("expected 1 decl"); return; }
    cst_unit_free(u);
    PASS();
}

static void test_expr_unary_addr(void) {
    TEST("unary &addr");
    translation_unit_t *u = parse_string(
        "@implementation Foo - (void)foo { &addr; } @end");
    if (!u) { FAIL("unit is NULL"); return; }
    if (u->decl_count != 1) { FAIL("expected 1 decl"); return; }
    cst_unit_free(u);
    PASS();
}

static void test_expr_ternary(void) {
    TEST("ternary x ? y : z");
    translation_unit_t *u = parse_string(
        "@implementation Foo - (void)foo { x ? y : z; } @end");
    if (!u) { FAIL("unit is NULL"); return; }
    if (u->decl_count != 1) { FAIL("expected 1 decl"); return; }
    cst_unit_free(u);
    PASS();
}

static void test_expr_sizeof_type(void) {
    TEST("sizeof(int)");
    translation_unit_t *u = parse_string(
        "@implementation Foo - (void)foo { sizeof(int); } @end");
    if (!u) { FAIL("unit is NULL"); return; }
    if (u->decl_count != 1) { FAIL("expected 1 decl"); return; }
    cst_unit_free(u);
    PASS();
}

static void test_expr_sizeof_expr(void) {
    TEST("sizeof x");
    translation_unit_t *u = parse_string(
        "@implementation Foo - (void)foo { sizeof x; } @end");
    if (!u) { FAIL("unit is NULL"); return; }
    if (u->decl_count != 1) { FAIL("expected 1 decl"); return; }
    cst_unit_free(u);
    PASS();
}

static void test_expr_selector(void) {
    TEST("@selector(foo)");
    translation_unit_t *u = parse_string(
        "@implementation Foo - (void)foo { @selector(foo); } @end");
    if (!u) { FAIL("unit is NULL"); return; }
    if (u->decl_count != 1) { FAIL("expected 1 decl"); return; }
    cst_unit_free(u);
    PASS();
}

static void test_expr_protocol(void) {
    TEST("@protocol(Proto)");
    translation_unit_t *u = parse_string(
        "@implementation Foo - (void)foo { @protocol(Proto); } @end");
    if (!u) { FAIL("unit is NULL"); return; }
    if (u->decl_count != 1) { FAIL("expected 1 decl"); return; }
    cst_unit_free(u);
    PASS();
}

static void test_expr_encode(void) {
    TEST("@encode(int)");
    translation_unit_t *u = parse_string(
        "@implementation Foo - (void)foo { @encode(int); } @end");
    if (!u) { FAIL("unit is NULL"); return; }
    if (u->decl_count != 1) { FAIL("expected 1 decl"); return; }
    cst_unit_free(u);
    PASS();
}

static void test_expr_block_simple(void) {
    TEST("block ^{ return 1; }");
    translation_unit_t *u = parse_string(
        "@implementation Foo - (void)foo { ^{ return 1; }; } @end");
    if (!u) { FAIL("unit is NULL"); return; }
    if (u->decl_count != 1) { FAIL("expected 1 decl"); return; }
    cst_unit_free(u);
    PASS();
}

static void test_expr_block_params(void) {
    TEST("block ^(int x) { return x + 1; }");
    translation_unit_t *u = parse_string(
        "@implementation Foo - (void)foo { ^(int x) { return x + 1; }; } @end");
    if (!u) { FAIL("unit is NULL"); return; }
    if (u->decl_count != 1) { FAIL("expected 1 decl"); return; }
    cst_unit_free(u);
    PASS();
}

static void test_expr_dot_access(void) {
    TEST("dot access obj.property");
    translation_unit_t *u = parse_string(
        "@implementation Foo - (void)foo { obj.property; } @end");
    if (!u) { FAIL("unit is NULL"); return; }
    if (u->decl_count != 1) { FAIL("expected 1 decl"); return; }
    cst_unit_free(u);
    PASS();
}

static void test_expr_subscript(void) {
    TEST("subscript array[i]");
    translation_unit_t *u = parse_string(
        "@implementation Foo - (void)foo { array[i]; } @end");
    if (!u) { FAIL("unit is NULL"); return; }
    if (u->decl_count != 1) { FAIL("expected 1 decl"); return; }
    cst_unit_free(u);
    PASS();
}

static void test_expr_self_super(void) {
    TEST("self and super");
    translation_unit_t *u = parse_string(
        "@implementation Foo - (void)foo { self; super; } @end");
    if (!u) { FAIL("unit is NULL"); return; }
    if (u->decl_count != 1) { FAIL("expected 1 decl"); return; }
    cst_unit_free(u);
    PASS();
}

static void test_expr_nil_null(void) {
    TEST("nil and null");
    translation_unit_t *u = parse_string(
        "@implementation Foo - (void)foo { nil; null; } @end");
    if (!u) { FAIL("unit is NULL"); return; }
    if (u->decl_count != 1) { FAIL("expected 1 decl"); return; }
    cst_unit_free(u);
    PASS();
}

static void test_expr_bool_literals(void) {
    TEST("BOOL literals YES NO");
    translation_unit_t *u = parse_string(
        "@implementation Foo - (void)foo { YES; NO; } @end");
    if (!u) { FAIL("unit is NULL"); return; }
    if (u->decl_count != 1) { FAIL("expected 1 decl"); return; }
    cst_unit_free(u);
    PASS();
}

static void test_expr_string_int_float(void) {
    TEST("string, int, float literals");
    translation_unit_t *u = parse_string(
        "@implementation Foo - (void)foo { \"hello\"; 42; 3.14; } @end");
    if (!u) { FAIL("unit is NULL"); return; }
    if (u->decl_count != 1) { FAIL("expected 1 decl"); return; }
    cst_unit_free(u);
    PASS();
}

// ── @try/@catch/@finally statement ──────────────────────────────────────────

static void test_stmt_try_catch_finally(void) {
    TEST("@try/@catch/@finally inside method");
    translation_unit_t *u = parse_string(
        "@implementation Foo - (void)foo { @try { ; } @catch(id e) { ; } @finally { ; } } @end");
    if (!u) { FAIL("unit is NULL"); return; }
    if (u->decl_count != 1) { FAIL("expected 1 decl"); return; }
    cst_unit_free(u);
    PASS();
}

static void test_stmt_synchronized(void) {
    TEST("@synchronized inside method");
    translation_unit_t *u = parse_string(
        "@implementation Foo - (void)foo { @synchronized (lock) { ; } } @end");
    if (!u) { FAIL("unit is NULL"); return; }
    if (u->decl_count != 1) { FAIL("expected 1 decl"); return; }
    cst_unit_free(u);
    PASS();
}

static void test_stmt_throw(void) {
    TEST("@throw inside method");
    translation_unit_t *u = parse_string(
        "@implementation Foo - (void)foo { @throw exc; } @end");
    if (!u) { FAIL("unit is NULL"); return; }
    if (u->decl_count != 1) { FAIL("expected 1 decl"); return; }
    cst_unit_free(u);
    PASS();
}

// ── combined / realistic ───────────────────────────────────────────────────

static void test_realistic_class(void) {
    TEST("realistic Nopa class with multiple methods");
    const char *src =
        "@interface MyClass : NSObject <MyProtocol> {\n"
        "    int count;\n"
        "    id delegate;\n"
        "}\n"
        "@property (readonly) int count;\n"
        "@property (weak) id delegate;\n"
        "- (instancetype)init;\n"
        "- (void)increment;\n"
        "+ (id)sharedInstance;\n"
        "@end\n"
        "@implementation MyClass\n"
        "- (instancetype)init {\n"
        "    self;\n"
        "    return self;\n"
        "}\n"
        "- (void)increment {\n"
        "    count++;\n"
        "}\n"
        "+ (id)sharedInstance {\n"
        "    return shared;\n"
        "}\n"
        "@end";
    translation_unit_t *u = parse_string(src);
    if (!u) { FAIL("unit is NULL"); return; }
    if (u->decl_count != 2) { FAIL("expected 2 decls"); return; }
    cst_decl_t *i = u->decls[0];
    if (i->kind != CST_DECL_CLASS_INTERFACE) { FAIL("expected class interface"); return; }
    if (!i->data.class_.superclass) { FAIL("expected superclass"); return; }
    if (i->data.class_.protocol_count != 1) { FAIL("expected 1 protocol"); return; }
    if (i->data.class_.ivar_count != 2) { FAIL("expected 2 ivars"); return; }
    if (i->data.class_.property_count != 2) { FAIL("expected 2 properties"); return; }
    if (i->data.class_.method_count != 3) { FAIL("expected 3 method decls"); return; }
    cst_decl_t *impl = u->decls[1];
    if (impl->kind != CST_DECL_CLASS_IMPLEMENTATION) { FAIL("expected class impl"); return; }
    if (impl->data.class_.method_count != 3) { FAIL("expected 3 method impls"); return; }
    for (int j = 0; j < impl->data.class_.method_count; j++) {
        if (!impl->data.class_.methods[j]->data.method.body) {
            FAIL("expected method body in impl"); return;
        }
    }
    cst_unit_free(u);
    PASS();
}

static void test_many_decls(void) {
    TEST("multiple top-level declarations");
    const char *src =
        "@interface Foo @end\n"
        "@implementation Foo @end\n"
        "@class Bar;\n"
        "@protocol P - (void)m; @end";
    translation_unit_t *u = parse_string(src);
    if (!u) { FAIL("unit is NULL"); return; }
    if (u->decl_count != 4) { FAIL("expected 4 decls"); return; }
    if (u->decls[0]->kind != CST_DECL_CLASS_INTERFACE) { FAIL("decl 0: expected interface"); return; }
    if (u->decls[1]->kind != CST_DECL_CLASS_IMPLEMENTATION) { FAIL("decl 1: expected implementation"); return; }
    if (u->decls[2]->kind != CST_DECL_FORWARD_CLASS) { FAIL("decl 2: expected forward class"); return; }
    if (u->decls[3]->kind != CST_DECL_PROTOCOL) { FAIL("decl 3: expected protocol"); return; }
    cst_unit_free(u);
    PASS();
}

static void test_nested_compound(void) {
    TEST("nested compound blocks");
    translation_unit_t *u = parse_string(
        "@implementation Foo - (void)foo { { { ; } } } @end");
    if (!u) { FAIL("unit is NULL"); return; }
    if (u->decl_count != 1) { FAIL("expected 1 decl"); return; }
    cst_unit_free(u);
    PASS();
}

// ── main ───────────────────────────────────────────────────────────────────

static void test_type_protocol_qualifier(void) {
    TEST("id<Protocol> type (method param)");
    translation_unit_t *u = parse_string(
        "@protocol P - (void)m; @end\n"
        "@interface Foo\n"
        "  - (void)bar:(id<P>)arg;\n"
        "@end");
    if (!u) { FAIL("parse failed"); return; }
    cst_decl_t *d = u->decls[1];
    if (d->kind != CST_DECL_CLASS_INTERFACE) { FAIL("expected class interface"); return; }
    cst_decl_t *m = d->data.class_.methods[0];
    cst_param_t *p = m->data.method.params;
    if (!p) { FAIL("expected param"); return; }
    cst_type_t *pt = p->type;
    if (pt->prim != TYPE_ID) { FAIL("expected id type"); return; }
    if (pt->protocol_count != 1) { FAIL("expected 1 protocol qualifier"); return; }
    if (strcmp(pt->protocols[0], "P") != 0) { FAIL("expected protocol P"); return; }
    cst_unit_free(u);
    PASS();
}

static void test_type_protocol_qualifier_return(void) {
    TEST("id<P,Q> return type");
    translation_unit_t *u = parse_string(
        "@protocol P - (void)m; @end\n"
        "@protocol Q - (void)n; @end\n"
        "@interface Foo\n"
        "  - (id<P,Q>)getObject;\n"
        "@end");
    if (!u) { FAIL("parse failed"); return; }
    cst_decl_t *d = u->decls[2];
    if (d->kind != CST_DECL_CLASS_INTERFACE) { FAIL("expected class interface"); return; }
    cst_decl_t *m = d->data.class_.methods[0];
    cst_type_t *rt = m->data.method.return_type;
    if (rt->prim != TYPE_ID) { FAIL("expected id type"); return; }
    if (rt->protocol_count != 2) { FAIL("expected 2 protocol qualifiers"); return; }
    if (strcmp(rt->protocols[0], "P") != 0) { FAIL("expected protocol P"); return; }
    if (strcmp(rt->protocols[1], "Q") != 0) { FAIL("expected protocol Q"); return; }
    cst_unit_free(u);
    PASS();
}

int main(void) {
    printf("parser tests\n");
    printf("-----------\n");

    // declarations
    test_empty();
    test_interface_simple();
    test_interface_superclass();
    test_interface_protocols();
    test_interface_superclass_protocols();
    test_interface_ivars();
    test_interface_method_decl();
    test_interface_method_with_args();
    test_interface_property();
    test_interface_property_attributes();
    test_implementation_simple();
    test_implementation_method_body();
    test_implementation_method_body_return();
    test_forward_class();
    test_protocol_decl();
    test_protocol_with_parent();

    // statements
    test_stmt_if();
    test_stmt_while();
    test_stmt_do_while();
    test_stmt_for();
    test_stmt_break_continue();
    test_stmt_switch();
    test_stmt_return_expr();
    test_stmt_try_catch_finally();
    test_stmt_synchronized();
    test_stmt_throw();

    // expressions
    test_expr_message_send_no_args();
    test_expr_message_send_args();
    test_expr_message_send_multi_args();
    test_expr_arithmetic();
    test_expr_arithmetic_parens();
    test_expr_comparison();
    test_expr_comparison_chain();
    test_expr_assignment();
    test_expr_compound_assign();
    test_expr_unary_minus();
    test_expr_unary_not();
    test_expr_unary_deref();
    test_expr_unary_addr();
    test_expr_ternary();
    test_expr_sizeof_type();
    test_expr_sizeof_expr();
    test_expr_selector();
    test_expr_protocol();
    test_expr_encode();
    test_expr_block_simple();
    test_expr_block_params();
    test_expr_dot_access();
    test_expr_subscript();
    test_expr_self_super();
    test_expr_nil_null();
    test_expr_bool_literals();
    test_expr_string_int_float();

    // types
    test_type_protocol_qualifier();
    test_type_protocol_qualifier_return();

    // combined
    test_realistic_class();
    test_many_decls();
    test_nested_compound();

    printf("\n%d/%d passed\n", passed, total);
    return passed == total ? 0 : 1;
}
