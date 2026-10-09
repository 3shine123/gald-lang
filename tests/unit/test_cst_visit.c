// test_cst_visit — CST visitor + validation tests

#include "ovel/lexer.h"
#include "ovel/parser.h"
#include "ovel/cst.h"
#include "ovel/cst_visit.h"
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

static int total = 0, passed = 0;

#define TEST(name) do { \
    printf("  %-55s ", name); \
    fflush(stdout); \
    total++; \
} while (0)

#define PASS() do { passed++; printf("PASS\n"); } while (0)
#define FAIL(msg) do { printf("FAIL: %s\n", msg); return; } while (0)

// ─── helper: parse source ──────────────────────────────────────────────────

static translation_unit_t *parse(const char *src) {
    lexer_t lexer;
    lexer_init(&lexer, src, strlen(src), "<test>");
    parser_t *p = parser_create(&lexer);
    if (!p) return NULL;
    translation_unit_t *u = parser_parse_translation_unit(p);
    if (parser_has_error(p)) {
        fprintf(stderr, "parse error: %s\n", parser_last_error(p));
        cst_unit_free(u);
        u = NULL;
    }
    parser_destroy(p);
    return u;
}

// ─── counter visitor ───────────────────────────────────────────────────────

typedef struct {
    int decls, stmts, exprs, types, params;
} counter_t;

static int cnt_enter_decl(cst_visitor_t *v, cst_decl_t *d) {
    (void)d; ((counter_t *)v->context)->decls++; return 1;
}
static int cnt_enter_stmt(cst_visitor_t *v, cst_stmt_t *s) {
    (void)s; ((counter_t *)v->context)->stmts++; return 1;
}
static int cnt_enter_expr(cst_visitor_t *v, cst_expr_t *e) {
    (void)e; ((counter_t *)v->context)->exprs++; return 1;
}
static void cnt_visit_type(cst_visitor_t *v, cst_type_t *t) {
    (void)t; ((counter_t *)v->context)->types++;
}
static void cnt_visit_param(cst_visitor_t *v, cst_param_t *p) {
    (void)p; ((counter_t *)v->context)->params++;
}

static void test_interface_class(void) {
    TEST("visit @interface class");
    const char *src = "@interface Foo : Bar { int x; }"
        "@property (readonly) int age;"
        "- (void)method:(int)arg;"
        "@end";
    translation_unit_t *u = parse(src);
    if (!u) { FAIL("parse failed"); }

    counter_t c = {0,0,0,0,0};
    cst_visitor_t vis;
    cst_visitor_init_default(&vis);
    vis.context = &c;
    vis.enter_decl = cnt_enter_decl;
    vis.enter_stmt = cnt_enter_stmt;
    vis.enter_expr = cnt_enter_expr;
    vis.visit_type = cnt_visit_type;
    vis.visit_param = cnt_visit_param;

    cst_visit_unit(&vis, u);

    if (c.decls < 1) FAIL("expected at least 1 decl (interface itself)");
    if (c.types < 3) FAIL("expected at least 3 types (ivar, property, method return)");
    if (c.params < 1) FAIL("expected at least 1 param (method:)");

    cst_unit_free(u);
    PASS();
}

static void test_implementation_methods(void) {
    TEST("visit @implementation with body");
    const char *src = "@implementation Foo\n"
        "- (int)bar { return 42 + 1; }\n"
        "+ (id)alloc { return 0; }\n"
        "@end";
    translation_unit_t *u = parse(src);
    if (!u) { FAIL("parse failed"); }

    counter_t c = {0,0,0,0,0};
    cst_visitor_t vis;
    cst_visitor_init_default(&vis);
    vis.context = &c;
    vis.enter_decl = cnt_enter_decl;
    vis.enter_stmt = cnt_enter_stmt;
    vis.enter_expr = cnt_enter_expr;
    vis.visit_type = cnt_visit_type;

    cst_visit_unit(&vis, u);

    if (c.decls < 3) FAIL("expected >= 3 decls (impl + 2 methods)");
    if (c.stmts < 2) FAIL("expected >= 2 stmts (return stmts)");
    if (c.exprs < 2) FAIL("expected >= 2 exprs (42, 1)");

    cst_unit_free(u);
    PASS();
}

static void test_function_decl(void) {
    TEST("visit C function");
    const char *src = "int add(int a, int b) { return a + b; }";
    translation_unit_t *u = parse(src);
    if (!u) { FAIL("parse failed"); }

    counter_t c = {0,0,0,0,0};
    cst_visitor_t vis;
    cst_visitor_init_default(&vis);
    vis.context = &c;
    vis.enter_decl = cnt_enter_decl;
    vis.enter_stmt = cnt_enter_stmt;
    vis.enter_expr = cnt_enter_expr;
    vis.visit_type = cnt_visit_type;
    vis.visit_param = cnt_visit_param;

    cst_visit_unit(&vis, u);

    if (c.decls < 1) FAIL("expected 1 function decl");
    if (c.params < 2) FAIL("expected 2 params");
    if (c.types < 3) FAIL("expected >= 3 types (return + param types)");
    if (c.exprs < 3) FAIL("expected >= 3 exprs (a, b, a+b)");
    if (c.stmts < 1) FAIL("expected 1 return stmt");

    cst_unit_free(u);
    PASS();
}

static void test_typedef_struct_enum(void) {
    TEST("visit typedef/struct/enum");
    const char *src =
        "typedef int MyInt;\n"
        "struct Point { int x; int y; };\n"
        "enum Color { RED, GREEN, BLUE };\n";
    translation_unit_t *u = parse(src);
    if (!u) { FAIL("parse failed"); }

    counter_t c = {0,0,0,0,0};
    cst_visitor_t vis;
    cst_visitor_init_default(&vis);
    vis.context = &c;
    vis.enter_decl = cnt_enter_decl;
    vis.visit_type = cnt_visit_type;

    cst_visit_unit(&vis, u);

    if (c.decls < 3) FAIL("expected 3 top-level decls");
    if (c.types < 3) FAIL("expected >= 3 types (typedef int + struct int x + struct int y)");

    cst_unit_free(u);
    PASS();
}

// ─── validation tests ──────────────────────────────────────────────────────

static void test_validate_ok(void) {
    TEST("validate valid tree");
    const char *src = "@interface Foo @end";
    translation_unit_t *u = parse(src);
    if (!u) { FAIL("parse failed"); }
    int ok = cst_validate(u, NULL, 0);
    cst_unit_free(u);
    if (!ok) FAIL("expected valid=true");
    PASS();
}

static void test_validate_function_ok(void) {
    TEST("validate function decl");
    const char *src = "int foo(void) { return 0; }";
    translation_unit_t *u = parse(src);
    if (!u) { FAIL("parse failed"); }
    int ok = cst_validate(u, NULL, 0);
    cst_unit_free(u);
    if (!ok) FAIL("expected valid=true");
    PASS();
}

static void test_validate_struct(void) {
    TEST("validate struct/enum/typedef");
    const char *src = "struct S { int x; }; enum E { A, B }; typedef int T;";
    translation_unit_t *u = parse(src);
    if (!u) { FAIL("parse failed"); }
    int ok = cst_validate(u, NULL, 0);
    cst_unit_free(u);
    if (!ok) FAIL("expected valid=true");
    PASS();
}

// ─── main ──────────────────────────────────────────────────────────────────

int main(void) {
    printf("CST visitor + validation tests:\n");

    test_interface_class();
    test_implementation_methods();
    test_function_decl();
    test_typedef_struct_enum();
    test_validate_ok();
    test_validate_function_ok();
    test_validate_struct();

    printf("\n%d/%d passed\n", passed, total);
    return passed == total ? 0 : 1;
}
