#include "nepa/symbol.h"
#include <stdio.h>
#include <string.h>

static int total = 0;
static int passed = 0;

#define TEST(name) do { printf("  %-45s ", name); total++; } while(0)
#define PASS() do { passed++; printf("PASS\n"); } while(0)
#define FAIL(msg) do { printf("FAIL: %s\n", msg); return; } while(0)

static void test_sym_alloc(void) {
    TEST("sym alloc/free");
    symbol_t *s = sym_alloc(SYM_CLASS, "NPObject");
    if (!s) { FAIL("sym_alloc returned NULL"); return; }
    if (s->kind != SYM_CLASS) { FAIL("wrong kind"); sym_free(s); return; }
    if (strcmp(s->name, "NPObject") != 0) { FAIL("wrong name"); sym_free(s); return; }
    sym_free(s);
    PASS();
}

static void test_scope_add_lookup(void) {
    TEST("scope add + lookup");
    scope_t *s = scope_alloc(NULL);
    symbol_t *foo = sym_alloc(SYM_VARIABLE, "foo");
    symbol_t *bar = sym_alloc(SYM_VARIABLE, "bar");

    if (scope_add(s, foo) != 0) { FAIL("add foo failed"); scope_free(s); return; }
    if (scope_add(s, bar) != 0) { FAIL("add bar failed"); scope_free(s); return; }

    if (scope_lookup(s, "foo") != foo) { FAIL("lookup foo failed"); scope_free(s); return; }
    if (scope_lookup(s, "bar") != bar) { FAIL("lookup bar failed"); scope_free(s); return; }
    if (scope_lookup(s, "nonexistent") != NULL) { FAIL("lookup should be NULL"); scope_free(s); return; }

    scope_free(s);
    PASS();
}

static void test_scope_nested(void) {
    TEST("nested scope resolution");
    scope_t *global = scope_alloc(NULL);
    symbol_t *x_global = sym_alloc(SYM_VARIABLE, "x");
    scope_add(global, x_global);

    scope_t *inner = scope_alloc(global);
    symbol_t *y_local = sym_alloc(SYM_VARIABLE, "y");
    scope_add(inner, y_local);

    if (scope_lookup(inner, "x") != x_global) { FAIL("inner can't see global x"); scope_free(inner); scope_free(global); return; }
    if (scope_lookup_local(inner, "x") != NULL) { FAIL("x should not be local to inner"); scope_free(inner); scope_free(global); return; }
    if (scope_lookup(global, "y") != NULL) { FAIL("global should not see inner y"); scope_free(inner); scope_free(global); return; }

    // shadowing
    symbol_t *x_shadow = sym_alloc(SYM_VARIABLE, "x");
    if (scope_add(inner, x_shadow) != 0) { FAIL("shadow x should succeed"); scope_free(inner); scope_free(global); return; }
    if (scope_lookup(inner, "x") != x_shadow) { FAIL("inner should see shadowed x"); scope_free(inner); scope_free(global); return; }

    scope_free(inner);
    scope_free(global);
    PASS();
}

static void test_scope_duplicate(void) {
    TEST("scope reject duplicate");
    scope_t *s = scope_alloc(NULL);
    symbol_t *a = sym_alloc(SYM_VARIABLE, "dup");
    symbol_t *b = sym_alloc(SYM_VARIABLE, "dup");

    if (scope_add(s, a) != 0) { FAIL("first add failed"); scope_free(s); return; }
    if (scope_add(s, b) != -1) { FAIL("second add should fail"); scope_free(s); return; }

    sym_free(b); // b was not stored
    scope_free(s);
    PASS();
}

static void test_symtab_class_lookup(void) {
    TEST("symtab find class/protocol");
    symbol_table_t *st = symtab_alloc();

    symbol_t *cls = sym_alloc(SYM_CLASS, "NPObject");
    symbol_t *proto = sym_alloc(SYM_PROTOCOL, "NSCoding");

    symtab_declare(st, cls);
    symtab_declare(st, proto);

    if (symtab_find_class(st, "NPObject") != cls) { FAIL("find NPObject failed"); symtab_free(st); return; }
    if (symtab_find_class(st, "Nonexistent") != NULL) { FAIL("should be NULL"); symtab_free(st); return; }
    if (symtab_find_protocol(st, "NSCoding") != proto) { FAIL("find NSCoding failed"); symtab_free(st); return; }

    symtab_free(st);
    PASS();
}

static void test_selector_register(void) {
    TEST("register + find selector");
    symbol_table_t *st = symtab_alloc();

    symbol_t *s1 = symtab_register_selector(st, "setName:");
    if (!s1) { FAIL("register setName: failed"); symtab_free(st); return; }
    if (s1->kind != SYM_SELECTOR) { FAIL("wrong kind"); symtab_free(st); return; }
    if (strcmp(s1->name, "setName:") != 0) { FAIL("wrong name"); symtab_free(st); return; }

    symbol_t *s2 = symtab_register_selector(st, "setName:");
    if (s2 != s1) { FAIL("re-register should return same"); symtab_free(st); return; }

    symbol_t *found = symtab_find_selector(st, "setName:");
    if (found != s1) { FAIL("find failed"); symtab_free(st); return; }

    if (symtab_find_selector(st, "nonexistent") != NULL) { FAIL("find nonexistent should be NULL"); symtab_free(st); return; }

    symtab_free(st);
    PASS();
}

static void test_selector_multiple(void) {
    TEST("multiple distinct selectors");
    symbol_table_t *st = symtab_alloc();

    symbol_t *init = symtab_register_selector(st, "init");
    symbol_t *alloc = symtab_register_selector(st, "alloc");
    symbol_t *foo = symtab_register_selector(st, "foo:bar:baz:");

    if (!init || !alloc || !foo) { FAIL("register failed"); symtab_free(st); return; }

    if (symtab_find_selector(st, "init") != init) { FAIL("init"); symtab_free(st); return; }
    if (symtab_find_selector(st, "alloc") != alloc) { FAIL("alloc"); symtab_free(st); return; }
    if (symtab_find_selector(st, "foo:bar:baz:") != foo) { FAIL("foo:bar:baz:"); symtab_free(st); return; }

    // Ensure they are distinct
    if (init == alloc) { FAIL("selectors should be distinct"); symtab_free(st); return; }

    symtab_free(st);
    PASS();
}

int main(void) {
    printf("symbol table tests\n");
    printf("-----------------\n");

    test_sym_alloc();
    test_scope_add_lookup();
    test_scope_nested();
    test_scope_duplicate();
    test_symtab_class_lookup();
    test_selector_register();
    test_selector_multiple();

    printf("\n%d/%d passed\n", passed, total);
    return passed == total ? 0 : 1;
}