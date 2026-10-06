#include "nopa/parser.h"
#include "nopa/lexer.h"
#include "nopa/cst.h"
#include "nopa/symbol.h"
#include "nopa/binder.h"
#include <stdio.h>
#include <string.h>

static int total = 0;
static int passed = 0;

#define TEST(name) do { printf("  %-50s ", name); total++; } while(0)
#define PASS() do { passed++; printf("PASS\n"); } while(0)
#define FAIL(msg) do { printf("FAIL: %s\n", msg); return; } while(0)
#define ASSERT(cond, msg) do { if (!(cond)) { printf("FAIL at %d: %s\n", __LINE__, msg); return; } } while(0)

static symbol_table_t *parse_and_bind(const char *src) {
    lexer_t lexer;
    lexer_init(&lexer, src, strlen(src), "test.np");
    parser_t *p = parser_create(&lexer);
    translation_unit_t *unit = parser_parse_translation_unit(p);
    parser_destroy(p);
    if (!unit) return NULL;

    symbol_table_t *st = symtab_alloc();
    binder_t *b = binder_create(st);
    binder_bind(b, unit);
    binder_destroy(b);
    cst_unit_free(unit);
    return st;
}

static void test_category_interface(void) {
    TEST("@interface Foo (Cat) appends method");
    const char *src =
        "@interface Foo\n"
        "  - (int)existing;\n"
        "@end\n"
        "@interface Foo (MyCat)\n"
        "  - (void)catMethod;\n"
        "@end\n"
        "@implementation Foo\n"
        "  - (int)existing { return 1; }\n"
        "  - (void)catMethod { }\n"
        "@end";

    symbol_table_t *st = parse_and_bind(src);
    ASSERT(st != NULL, "st null");

    symbol_t *cls = symtab_find_class(st, "Foo");
    ASSERT(cls != NULL, "class not found");

    ASSERT(cls->data.cls.method_count >= 2, "expected at least 2 methods");

    int found_base = 0, found_cat = 0;
    for (int i = 0; i < cls->data.cls.method_count; i++) {
        if (strcmp(cls->data.cls.methods[i]->name, "existing") == 0) found_base = 1;
        if (strcmp(cls->data.cls.methods[i]->name, "catMethod") == 0) found_cat = 1;
    }
    ASSERT(found_base, "existing method not found");
    ASSERT(found_cat, "catMethod not found");

    symtab_free(st);
    PASS();
}

static void test_category_with_implementation(void) {
    TEST("@interface + @implementation category");
    const char *src =
        "@interface Foo\n"
        "  - (int)base;\n"
        "@end\n"
        "@interface Foo (Cat)\n"
        "  - (void)catMethod;\n"
        "@end\n"
        "@implementation Foo (Cat)\n"
        "  - (void)catMethod { }\n"
        "@end\n"
        "@implementation Foo\n"
        "  - (int)base { return 1; }\n"
        "@end";

    symbol_table_t *st = parse_and_bind(src);
    ASSERT(st != NULL, "st null");

    symbol_t *cls = symtab_find_class(st, "Foo");
    ASSERT(cls != NULL, "class not found");

    int found_cat = 0;
    for (int i = 0; i < cls->data.cls.method_count; i++) {
        if (strcmp(cls->data.cls.methods[i]->name, "catMethod") == 0) {
            found_cat = 1;
            ASSERT(cls->data.cls.methods[i]->data.method.has_body == 1,
                   "cat method should have body from @implementation");
            break;
        }
    }
    ASSERT(found_cat, "catMethod not found with body");
    symtab_free(st);
    PASS();
}

static void test_category_conflict(void) {
    TEST("category method conflict is detected");
    const char *src =
        "@interface Foo\n"
        "  - (void)doIt;\n"
        "@end\n"
        "@interface Foo (Cat)\n"
        "  - (void)doIt;\n"
        "@end";

    lexer_t lexer;
    lexer_init(&lexer, src, strlen(src), "test.np");
    parser_t *p = parser_create(&lexer);
    translation_unit_t *unit = parser_parse_translation_unit(p);
    symbol_table_t *st = symtab_alloc();
    binder_t *b = binder_create(st);
    int r = binder_bind(b, unit);
    binder_destroy(b);
    symtab_free(st);
    cst_unit_free(unit);
    parser_destroy(p);
    if (r != -1) { FAIL("expected conflict error"); return; }
    PASS();
}

int main(void) {
    printf("category tests\n");
    printf("--------------\n");

    test_category_interface();
    test_category_with_implementation();
    test_category_conflict();

    printf("\n%d/%d passed\n", passed, total);
    return passed == total ? 0 : 1;
}
