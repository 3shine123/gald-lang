#include "ovel/layout.h"
#include "ovel/parser.h"
#include "ovel/lexer.h"
#include "ovel/cst.h"
#include "ovel/symbol.h"
#include "ovel/binder.h"
#include <stdio.h>
#include <string.h>

static int total = 0;
static int passed = 0;

#define TEST(name) do { printf("  %-50s ", name); total++; } while(0)
#define PASS() do { passed++; printf("PASS\n"); } while(0)
#define FAIL(msg) do { printf("FAIL: %s\n", msg); return; } while(0)
#define ASSERT(cond, msg) do { if (!(cond)) { printf("FAIL at %d: %s\n", __LINE__, msg); return; } } while(0)

static int do_layout(symbol_table_t *st, translation_unit_t *unit) {
    binder_t *b = binder_create(st);
    binder_bind(b, unit);
    binder_destroy(b);

    layout_ctx_t *ctx = layout_create(st);
    int r = layout_compute(ctx);
    layout_destroy(ctx);
    return r;
}

static void test_empty(void) {
    TEST("layout empty");
    lexer_t lexer;
    lexer_init(&lexer, "", 0, "test.ov");
    parser_t *p = parser_create(&lexer);
    translation_unit_t *unit = parser_parse_translation_unit(p);
    symbol_table_t *st = symtab_alloc();
    int r = do_layout(st, unit);
    parser_destroy(p);
    symtab_free(st);
    cst_unit_free(unit);
    if (r != 0) { FAIL("expected success"); return; }
    PASS();
}

static void test_interface_no_ivars(void) {
    TEST("layout @interface Foo @end");
    lexer_t lexer;
    const char *src = "@interface Foo @end";
    lexer_init(&lexer, src, strlen(src), "test.ov");
    parser_t *p = parser_create(&lexer);
    translation_unit_t *unit = parser_parse_translation_unit(p);
    symbol_table_t *st = symtab_alloc();
    int r = do_layout(st, unit);
    parser_destroy(p);
    symtab_free(st);
    cst_unit_free(unit);
    if (r != 0) { FAIL("expected success"); return; }
    PASS();
}

static void test_vtable_indices(void) {
    TEST("layout vtable indices");
    const char *src =
        "@interface Base\n"
        "  - (int)methodA;\n"
        "  - (void)methodB;\n"
        "@end\n"
        "@interface Derived : Base\n"
        "  - (int)methodA;\n"
        "  - (void)methodC;\n"
        "@end\n"
        "@implementation Base\n"
        "- (int)methodA { return 1; }\n"
        "- (void)methodB { }\n"
        "@end\n"
        "@implementation Derived\n"
        "- (int)methodA { return 2; }\n"
        "- (void)methodC { }\n"
        "@end";

    lexer_t lexer;
    lexer_init(&lexer, src, strlen(src), "test.ov");
    parser_t *p = parser_create(&lexer);
    translation_unit_t *unit = parser_parse_translation_unit(p);
    ASSERT(unit != NULL, "parse failed");

    symbol_table_t *st = symtab_alloc();
    binder_t *b = binder_create(st);
    binder_bind(b, unit);
    binder_destroy(b);

    // Check Base methods got vtable_indices
    symbol_t *base = symtab_find_class(st, "Base");
    ASSERT(base != NULL, "Base not found");
    ASSERT(base->data.cls.method_count > 0, "Base has no methods");

    // methodA (index 0), methodB (index 1)
    for (int i = 0; i < base->data.cls.method_count; i++) {
        base->data.cls.methods[i]->data.method.vtable_index = -1;
    }

    symbol_t *sub = symtab_find_class(st, "Derived");
    ASSERT(sub != NULL, "Derived not found");

    // Check Derived overrides
    for (int i = 0; i < sub->data.cls.method_count; i++) {
        sub->data.cls.methods[i]->data.method.vtable_index = -1;
    }

    layout_ctx_t *ctx = layout_create(st);
    int r = layout_compute(ctx);
    ASSERT(r == 0, "layout failed");

    // Re-fetch after layout
    base = symtab_find_class(st, "Base");
    sub = symtab_find_class(st, "Derived");

    int base_a_idx = -1, base_b_idx = -1;
    int sub_a_idx = -1, sub_c_idx = -1;

    for (int i = 0; i < base->data.cls.method_count; i++) {
        if (strcmp(base->data.cls.methods[i]->name, "methodA") == 0)
            base_a_idx = base->data.cls.methods[i]->data.method.vtable_index;
        if (strcmp(base->data.cls.methods[i]->name, "methodB") == 0)
            base_b_idx = base->data.cls.methods[i]->data.method.vtable_index;
    }
    for (int i = 0; i < sub->data.cls.method_count; i++) {
        if (strcmp(sub->data.cls.methods[i]->name, "methodA") == 0)
            sub_a_idx = sub->data.cls.methods[i]->data.method.vtable_index;
        if (strcmp(sub->data.cls.methods[i]->name, "methodC") == 0)
            sub_c_idx = sub->data.cls.methods[i]->data.method.vtable_index;
    }

    ASSERT(base_a_idx >= 0, "Base methodA no index");
    ASSERT(base_b_idx >= 0, "Base methodB no index");
    ASSERT(base_a_idx != base_b_idx, "Base methods same index");
    ASSERT(sub_a_idx >= 0, "Sub methodA no index");
    ASSERT(sub_c_idx >= 0, "Sub methodC no index");
    ASSERT(sub_a_idx == base_a_idx, "Sub methodA != Base methodA (override)");
    ASSERT(sub_c_idx != sub_a_idx, "Sub methodC same as methodA");

    layout_destroy(ctx);
    parser_destroy(p);
    symtab_free(st);
    cst_unit_free(unit);
    PASS();
}

int main(void) {
    printf("layout tests\n");
    printf("-----------\n");

    test_empty();
    test_interface_no_ivars();
    test_vtable_indices();

    printf("\n%d/%d passed\n", passed, total);
    return passed == total ? 0 : 1;
}