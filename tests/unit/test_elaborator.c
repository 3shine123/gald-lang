#include "gald/elaborator.h"
#include "gald/parser.h"
#include "gald/lexer.h"
#include "gald/cst.h"
#include "gald/symbol.h"
#include "gald/binder.h"
#include "gald/checker.h"
#include <stdio.h>
#include <string.h>

static int total = 0;
static int passed = 0;

#define TEST(name) do { printf("  %-50s ", name); total++; } while(0)
#define PASS() do { passed++; printf("PASS\n"); } while(0)
#define FAIL(msg) do { printf("FAIL: %s\n", msg); return; } while(0)
#define ASSERT(cond, msg) do { if (!(cond)) { printf("FAIL at %d: %s\n", __LINE__, msg); return; } } while(0)

static int run_elaboration(symbol_table_t *st, translation_unit_t *unit) {
    binder_t *b = binder_create(st);
    binder_bind(b, unit);
    binder_destroy(b);

    elaborator_t *e = elaborator_create(st);
    int r = elaborator_run(e, unit);
    ast_unit_t *ast = elaborator_take_ast(e);
    if (ast) ast_unit_free(ast);
    elaborator_destroy(e);
    return r;
}

static symbol_table_t *parse_and_elab(const char *src, translation_unit_t **out_unit) {
    lexer_t lexer;
    lexer_init(&lexer, src, strlen(src), "test.gm");
    parser_t *p = parser_create(&lexer);
    *out_unit = parser_parse_translation_unit(p);
    parser_destroy(p);

    if (!*out_unit) return NULL;

    symbol_table_t *st = symtab_alloc();
    run_elaboration(st, *out_unit);
    return st;
}

static void test_empty(void) {
    TEST("elaborator empty");
    translation_unit_t *unit;
    symbol_table_t *st = parse_and_elab("", &unit);
    ASSERT(st != NULL, "alloc failed");
    symtab_free(st);
    cst_unit_free(unit);
    PASS();
}

static void test_simple_property(void) {
    TEST("@property int x synthesizes _ivar + getter + setter");
    const char *src =
        "@interface Foo\n"
        "  @property int x;\n"
        "@end\n"
        "@implementation Foo\n"
        "@end";

    translation_unit_t *unit;
    symbol_table_t *st = parse_and_elab(src, &unit);
    ASSERT(st != NULL, "st null");

    symbol_t *cls = symtab_find_class(st, "Foo");
    ASSERT(cls != NULL, "Foo class not found");

    // Check ivar_count increased (1 original if any, +1 from property)
    int found_ivar = 0;
    for (int i = 0; i < cls->data.cls.ivar_count; i++) {
        if (strcmp(cls->data.cls.ivars[i]->name, "_x") == 0) {
            found_ivar = 1;
            break;
        }
    }
    ASSERT(found_ivar, "_x ivar not synthesized");

    // Check getter exists
    int found_getter = 0;
    for (int i = 0; i < cls->data.cls.method_count; i++) {
        if (strcmp(cls->data.cls.methods[i]->name, "x") == 0) {
            found_getter = 1;
            ASSERT(cls->data.cls.methods[i]->data.method.return_type != NULL, "getter has no return type");
            break;
        }
    }
    ASSERT(found_getter, "getter 'x' not synthesized");

    // Check setter exists
    int found_setter = 0;
    for (int i = 0; i < cls->data.cls.method_count; i++) {
        if (strcmp(cls->data.cls.methods[i]->name, "setX:") == 0) {
            found_setter = 1;
            ASSERT(cls->data.cls.methods[i]->data.method.params != NULL, "setter has no params");
            break;
        }
    }
    ASSERT(found_setter, "setter 'setX:' not synthesized");

    // Check property is wired to ivar
    ASSERT(cls->data.cls.properties != NULL, "no properties");
    ASSERT(cls->data.cls.properties[0]->data.prop.ivar_sym != NULL, "property not wired to ivar");

    symtab_free(st);
    cst_unit_free(unit);
    PASS();
}

static void test_readonly_property(void) {
    TEST("@property (readonly) int x — no setter");
    const char *src =
        "@interface Foo\n"
        "  @property (readonly) int x;\n"
        "@end\n"
        "@implementation Foo\n"
        "@end";

    translation_unit_t *unit;
    symbol_table_t *st = parse_and_elab(src, &unit);
    ASSERT(st != NULL, "st null");

    symbol_t *cls = symtab_find_class(st, "Foo");
    ASSERT(cls != NULL, "cls null");

    int found_setter = 0;
    for (int i = 0; i < cls->data.cls.method_count; i++) {
        if (strcmp(cls->data.cls.methods[i]->name, "setX:") == 0) {
            found_setter = 1;
            break;
        }
    }
    ASSERT(!found_setter, "readonly property should not have setter");

    symtab_free(st);
    cst_unit_free(unit);
    PASS();
}

static void test_dynamic_property(void) {
    TEST("@dynamic x — no ivar, no getter, no setter");
    const char *src =
        "@interface Foo\n"
        "  @property int x;\n"
        "@end\n"
        "@implementation Foo\n"
        "  @dynamic x;\n"
        "@end";

    translation_unit_t *unit;
    symbol_table_t *st = parse_and_elab(src, &unit);
    ASSERT(st != NULL, "st null");

    symbol_t *cls = symtab_find_class(st, "Foo");
    ASSERT(cls != NULL, "cls null");

    // Check no _x ivar
    int found_ivar = 0;
    for (int i = 0; i < cls->data.cls.ivar_count; i++) {
        if (strcmp(cls->data.cls.ivars[i]->name, "_x") == 0) {
            found_ivar = 1;
            break;
        }
    }
    ASSERT(!found_ivar, "dynamic property should not have _ivar");

    // Check no getter
    int found_getter = 0;
    for (int i = 0; i < cls->data.cls.method_count; i++) {
        if (strcmp(cls->data.cls.methods[i]->name, "x") == 0) {
            found_getter = 1;
            break;
        }
    }
    ASSERT(!found_getter, "dynamic property should not synthesize getter");

    symtab_free(st);
    cst_unit_free(unit);
    PASS();
}

int main(void) {
    printf("elaborator tests\n");
    printf("---------------\n");

    test_empty();
    test_simple_property();
    test_readonly_property();
    test_dynamic_property();

    printf("\n%d/%d passed\n", passed, total);
    return passed == total ? 0 : 1;
}
