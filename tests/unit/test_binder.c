#include "nopa/binder.h"
#include "nopa/parser.h"
#include "nopa/lexer.h"
#include "nopa/cst.h"
#include "nopa/symbol.h"
#include <stdio.h>
#include <string.h>

static int total = 0;
static int passed = 0;

#define TEST(name) do { printf("  %-55s ", name); total++; } while(0)
#define PASS() do { passed++; printf("PASS\n"); } while(0)
#define FAIL(msg) do { printf("FAIL: %s\n", msg); return; } while(0)

static int do_bind(const char *src) {
    lexer_t lexer;
    lexer_init(&lexer, src, strlen(src), "test.np");
    parser_t *p = parser_create(&lexer);
    translation_unit_t *unit = parser_parse_translation_unit(p);
    if (!unit) { parser_destroy(p); return -1; }

    symbol_table_t *st = symtab_alloc();
    binder_t *b = binder_create(st);
    int r = binder_bind(b, unit);

    parser_destroy(p);
    binder_destroy(b);
    symtab_free(st);
    cst_unit_free(unit);
    return r;
}

static int do_bind_get_symtab(const char *src, symbol_table_t **out_st) {
    lexer_t lexer;
    lexer_init(&lexer, src, strlen(src), "test.np");
    parser_t *p = parser_create(&lexer);
    translation_unit_t *unit = parser_parse_translation_unit(p);
    if (!unit) { parser_destroy(p); return -1; }

    symbol_table_t *st = symtab_alloc();
    binder_t *b = binder_create(st);
    int r = binder_bind(b, unit);

    parser_destroy(p);
    binder_destroy(b);
    cst_unit_free(unit);

    if (r == 0) {
        *out_st = st; // caller must free
    } else {
        symtab_free(st);
        *out_st = NULL;
    }
    return r;
}

// ─── existing tests ───────────────────────────────────────────────────────

static void test_empty(void) {
    TEST("bind empty");
    int r = do_bind("");
    if (r != 0) { FAIL("expected success"); return; }
    PASS();
}

static void test_interface_simple(void) {
    TEST("bind @interface Foo @end");
    int r = do_bind("@interface Foo @end");
    if (r != 0) { FAIL("expected success"); return; }
    PASS();
}

static void test_implementation_simple(void) {
    TEST("bind @implementation Foo @end (no @interface)");
    int r = do_bind("@implementation Foo @end");
    if (r != -1) { FAIL("expected error (no @interface)"); return; }
    PASS();
}

static void test_interface_with_ivar(void) {
    TEST("bind @interface Foo { int x; } @end");
    int r = do_bind("@interface Foo { int x; } @end");
    if (r != 0) { FAIL("expected success"); return; }
    PASS();
}

static void test_interface_with_method(void) {
    TEST("bind @interface with method");
    int r = do_bind("@interface Foo - (void)bar; @end @implementation Foo - (void)bar { return 42; } @end");
    if (r != 0) { FAIL("expected success"); return; }
    PASS();
}

static void test_protocol(void) {
    TEST("bind @protocol");
    int r = do_bind("@protocol MyProto - (void)doIt; @end");
    if (r != 0) { FAIL("expected success"); return; }
    PASS();
}

// ─── new: @selector ───────────────────────────────────────────────────────

static void test_selector_expr(void) {
    TEST("bind @selector expression");
    // Use @selector inside a method body
    int r = do_bind("@interface Foo - (void)bar; @end "
                    "@implementation Foo - (void)bar { @selector(foo); } @end");
    if (r != 0) { FAIL("expected success"); return; }
    PASS();
}

static void test_selector_registers_sym(void) {
    TEST("@selector registers selector symbol");
    symbol_table_t *st = NULL;
    int r = do_bind_get_symtab("@interface Foo - (void)bar; @end "
                               "@implementation Foo - (void)bar { @selector(foo); } @end", &st);
    if (r != 0) { FAIL("bind failed"); return; }
    symbol_t *sel = symtab_find_selector(st, "foo");
    if (!sel) { FAIL("selector not found in table"); symtab_free(st); return; }
    if (sel->kind != SYM_SELECTOR) { FAIL("wrong kind"); symtab_free(st); return; }
    symtab_free(st);
    PASS();
}

// ─── new: typedef ─────────────────────────────────────────────────────────

static void test_typedef_simple(void) {
    TEST("bind typedef int MyInt");
    int r = do_bind("typedef int MyInt;");
    if (r != 0) { FAIL("expected success"); return; }
    PASS();
}

static void test_typedef_resolution(void) {
    TEST("typedef used in variable decl");
    int r = do_bind("typedef int MyInt; MyInt x;");
    if (r != 0) { FAIL("expected success"); return; }
    PASS();
}

static void test_typedef_unknown_type_error(void) {
    TEST("undefined type in var decl -> warning (not error)");
    int r = do_bind("UndefinedType x;");
    if (r != 0) { FAIL("expected no error (unknown types are lenient now)"); return; }
    PASS();
}

// ─── new: struct / union / enum ───────────────────────────────────────────

static void test_struct_decl(void) {
    TEST("bind struct Point { int x; int y; };");
    int r = do_bind("struct Point { int x; int y; };");
    if (r != 0) { FAIL("expected success"); return; }
    PASS();
}

static void test_union_decl(void) {
    TEST("bind union Data { int i; float f; };");
    int r = do_bind("union Data { int i; float f; };");
    if (r != 0) { FAIL("expected success"); return; }
    PASS();
}

static void test_enum_decl(void) {
    TEST("bind enum Color { RED, GREEN, BLUE };");
    int r = do_bind("enum Color { RED, GREEN, BLUE };");
    if (r != 0) { FAIL("expected success"); return; }
    PASS();
}

static void test_enum_member_as_var(void) {
    TEST("enum member visible as constant");
    symbol_table_t *st = NULL;
    int r = do_bind_get_symtab("enum Color { RED, GREEN, BLUE };", &st);
    if (r != 0) { FAIL("bind failed"); return; }
    symbol_t *red = symtab_lookup(st, "RED");
    if (!red) { FAIL("RED not found"); symtab_free(st); return; }
    if (red->kind != SYM_VARIABLE) { FAIL("expected SYM_VARIABLE"); symtab_free(st); return; }
    symtab_free(st);
    PASS();
}

// ─── new: conflict detection ──────────────────────────────────────────────

static void test_conflicting_typedef_twice(void) {
    TEST("duplicate typedef ok (redeclaration)");
    int r = do_bind("typedef int A; typedef int A;");
    if (r != 0) { FAIL("expected success"); return; }
    PASS();
}

int main(void) {
    printf("binder tests\n");
    printf("-----------\n");

    test_empty();
    test_interface_simple();
    test_implementation_simple();
    test_interface_with_ivar();
    test_interface_with_method();
    test_protocol();

    test_selector_expr();
    test_selector_registers_sym();
    test_typedef_simple();
    test_typedef_resolution();
    test_typedef_unknown_type_error();
    test_struct_decl();
    test_union_decl();
    test_enum_decl();
    test_enum_member_as_var();
    test_conflicting_typedef_twice();

    printf("\n%d/%d passed\n", passed, total);
    return passed == total ? 0 : 1;
}
