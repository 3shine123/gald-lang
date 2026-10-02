#include "gald/checker.h"
#include "gald/binder.h"
#include "gald/parser.h"
#include "gald/lexer.h"
#include "gald/cst.h"
#include <stdio.h>
#include <string.h>

static int total = 0;
static int passed = 0;

#define TEST(name) do { printf("  %-50s ", name); total++; } while(0)
#define PASS() do { passed++; printf("PASS\n"); } while(0)
#define FAIL(msg) do { printf("FAIL: %s\n", msg); return; } while(0)

static int do_check(const char *src) {
    lexer_t lexer;
    lexer_init(&lexer, src, strlen(src), "test.gm");
    parser_t *p = parser_create(&lexer);
    translation_unit_t *unit = parser_parse_translation_unit(p);
    if (!unit) { parser_destroy(p); return -1; }

    symbol_table_t *st = symtab_alloc();
    binder_t *b = binder_create(st);
    binder_bind(b, unit);

    checker_t *c = checker_create(st);
    int r = checker_check(c, unit);

    binder_destroy(b);
    checker_destroy(c);
    symtab_free(st);
    cst_unit_free(unit);
    parser_destroy(p);
    return r;
}

static void test_empty(void) {
    TEST("check empty");
    int r = do_check("");
    if (r != 0) { FAIL("expected success"); return; }
    PASS();
}

static void test_literal_int(void) {
    TEST("check literal int");
    int r = do_check("int main(void) { 42; }");
    if (r != 0) { FAIL("expected success"); return; }
    PASS();
}

static void test_literal_float(void) {
    TEST("check literal float");
    int r = do_check("int main(void) { 3.14; }");
    if (r != 0) { FAIL("expected success"); return; }
    PASS();
}

static void test_literal_string(void) {
    TEST("check literal string");
    int r = do_check("int main(void) { \"hello\"; }");
    if (r != 0) { FAIL("expected success"); return; }
    PASS();
}

static void test_if_stmt(void) {
    TEST("check if statement");
    int r = do_check("int main(void) { if (1) { 42; } else { 0; } }");
    if (r != 0) { FAIL("expected success"); return; }
    PASS();
}

static void test_while_loop(void) {
    TEST("check while loop");
    int r = do_check("int main(void) { while (1) { break; } }");
    if (r != 0) { FAIL("expected success"); return; }
    PASS();
}

static void test_return_stmt(void) {
    TEST("check return statement");
    int r = do_check("int foo(void) { return 42; }");
    if (r != 0) { FAIL("expected success"); return; }
    PASS();
}

static void test_return_void(void) {
    TEST("check void return");
    int r = do_check("void foo(void) { return; }");
    if (r != 0) { FAIL("expected success"); return; }
    PASS();
}

static void test_empty_interface(void) {
    TEST("check @interface Foo @end");
    int r = do_check("@interface Foo @end");
    if (r != 0) { FAIL("expected success"); return; }
    PASS();
}

static void test_interface_with_method(void) {
    TEST("check @interface with method and body");
    int r = do_check("@interface Foo - (int)bar; @end @implementation Foo - (int)bar { return 42; } @end");
    if (r != 0) { FAIL("expected success"); return; }
    PASS();
}

static void test_simple_binary(void) {
    TEST("check int + int");
    int r = do_check("int main(void) { 1 + 2; }");
    if (r != 0) { FAIL("expected success"); return; }
    PASS();
}

static void test_assign(void) {
    TEST("check assignment");
    int r = do_check("int main(void) { int x; x = 42; }");
    if (r != 0) { FAIL("expected success"); return; }
    PASS();
}

static void test_protocol_conformance_pass(void) {
    TEST("protocol conformance — passes");
    const char *src =
        "@protocol MyProto\n"
        "  - (void)doIt;\n"
        "  - (int)getValue;\n"
        "@end\n"
        "@interface Foo <MyProto>\n"
        "  - (void)doIt;\n"
        "  - (int)getValue;\n"
        "@end\n"
        "@implementation Foo\n"
        "  - (void)doIt { }\n"
        "  - (int)getValue { return 42; }\n"
        "@end";
    int r = do_check(src);
    if (r != 0) { FAIL("expected success"); return; }
    PASS();
}

static void test_protocol_conformance_fail(void) {
    TEST("protocol conformance — fails missing method");
    const char *src =
        "@protocol MyProto\n"
        "  - (void)doIt;\n"
        "  - (int)getValue;\n"
        "@end\n"
        "@interface Foo <MyProto>\n"
        "  - (void)doIt;\n"
        "@end\n"
        "@implementation Foo\n"
        "  - (void)doIt { }\n"
        "@end";
    int r = do_check(src);
    if (r != -1) { FAIL("expected error"); return; }
    PASS();
}

static void test_protocol_inheritance_pass(void) {
    TEST("protocol inheritance — passes");
    const char *src =
        "@protocol Base\n"
        "  - (void)baseMethod;\n"
        "@end\n"
        "@protocol Derived <Base>\n"
        "  - (void)derivedMethod;\n"
        "@end\n"
        "@interface Foo <Derived>\n"
        "  - (void)baseMethod;\n"
        "  - (void)derivedMethod;\n"
        "@end\n"
        "@implementation Foo\n"
        "  - (void)baseMethod { }\n"
        "  - (void)derivedMethod { }\n"
        "@end";
    int r = do_check(src);
    if (r != 0) { FAIL("expected success"); return; }
    PASS();
}

static void test_id_protocol_receiver(void) {
    TEST("id<Protocol> message dispatch passes");
    const char *src =
        "@protocol P\n"
        "  - (int)getValue;\n"
        "@end\n"
        "@interface Foo\n"
        "  - (void)test:(id<P>)obj;\n"
        "@end\n"
        "@implementation Foo\n"
        "  - (void)test:(id<P>)obj {\n"
        "    int x = [obj getValue];\n"
        "  }\n"
        "@end";
    int r = do_check(src);
    if (r != 0) { FAIL("expected success"); return; }
    PASS();
}

int main(void) {
    printf("checker tests\n");
    printf("-----------\n");

    test_empty();
    test_literal_int();
    test_literal_float();
    test_literal_string();
    test_if_stmt();
    test_while_loop();
    test_return_stmt();
    test_return_void();
    test_empty_interface();
    test_interface_with_method();
    test_simple_binary();
    test_assign();
    test_protocol_conformance_pass();
    test_protocol_conformance_fail();
    test_protocol_inheritance_pass();
    test_id_protocol_receiver();

    printf("\n%d/%d passed\n", passed, total);
    return passed == total ? 0 : 1;
}