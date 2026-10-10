#include "jeti/parser.h"
#include "jeti/lexer.h"
#include "jeti/cst.h"
#include <stdio.h>
#include <string.h>

static int total = 0;
static int passed = 0;

#define TEST(name) do { printf("  %-40s ", name); total++; } while(0)
#define PASS() do { passed++; printf("PASS\n"); } while(0)
#define FAIL(msg) do { printf("FAIL: %s\n", msg); return; } while(0)

static void do_test(const char *name, const char *src, int expected_decls) {
    TEST(name);
    lexer_t lexer;
    lexer_init(&lexer, src, strlen(src), "test.jeti");
    parser_t *p = parser_create(&lexer);
    translation_unit_t *unit = parser_parse_translation_unit(p);

    if (!unit) { FAIL("parse returned NULL"); parser_destroy(p); return; }
    if (parser_has_error(p) && unit->decl_count == 0) { FAIL("parse error"); cst_unit_free(unit); parser_destroy(p); return; }

    printf("\n");
    cst_print(unit);

    if (unit->decl_count < expected_decls) { FAIL("fewer decls than expected"); cst_unit_free(unit); parser_destroy(p); return; }
    cst_unit_free(unit);
    parser_destroy(p);
    PASS();
}

int main(void) {
    printf("cst_print tests\n");
    printf("-----------\n");

    do_test("@interface empty",
        "@interface Foo @end", 1);

    do_test("@interface with ivar",
        "@interface Foo { int x; } @end", 1);

    do_test("@interface with property",
        "@interface Foo @property int age; @end", 1);

    do_test("@interface with method",
        "@interface Foo - (void)bar; @end", 1);

    do_test("@implementation empty",
        "@implementation Foo @end", 1);

    do_test("@implementation with method",
        "@implementation Foo - (void)bar { return 42; } @end", 1);

    do_test("@class forward",
        "@class Foo, Bar;", 1);

    do_test("multiple toplevel decls",
        "@interface Foo @end @interface Bar @end @interface Baz @end", 3);

    printf("\n%d/%d passed\n", passed, total);
    return passed == total ? 0 : 1;
}