#include "nopa/codegen.h"
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

static int total = 0, passed = 0;
#define TEST(n) do { printf("  %-50s ", n); total++; } while(0)
#define PASS() do { passed++; printf("PASS\n"); } while(0)
#define FAIL(m) do { printf("FAIL: %s\n", m); return; } while(0)
#define ASSERT(cond, m) do { if (!(cond)) { printf("FAIL: %s\n", m); return; } } while(0)

static char *emit_header_to_str(cg_unit_t *unit, const char *guard) {
    char buf[8192];
    FILE *fp = fmemopen(buf, sizeof(buf), "w");
    if (!fp) return NULL;
    cg_emit_header(unit, fp, guard);
    fclose(fp);
    return strdup(buf);
}

static cg_unit_t *make_unit(void) {
    return cg_unit_alloc("test.np");
}

static void test_header_empty(void) {
    TEST("header empty unit");
    cg_unit_t *u = make_unit();
    char *s = emit_header_to_str(u, "MY_GUARD_H");
    ASSERT(s != NULL, "got output");
    ASSERT(strstr(s, "#ifndef MY_GUARD_H") != NULL, "has ifndef");
    ASSERT(strstr(s, "#define MY_GUARD_H") != NULL, "has define");
    ASSERT(strstr(s, "#endif") != NULL, "has endif");
    free(s);
    cg_unit_free(u);
    PASS();
}

static void test_header_func_decl(void) {
    TEST("header function declaration");
    cg_unit_t *u = make_unit();
    cg_decl_t *d = cg_decl_alloc(CGDECL_FUNCTION, "foo");
    d->u.func.return_type = strdup("int");
    d->u.func.param_count = 1;
    d->u.func.params = calloc(2, sizeof(*d->u.func.params));
    d->u.func.params[0].type = strdup("NPObject *");
    d->u.func.params[0].name = strdup("self");
    d->u.func.body = NULL; // declaration only
    cg_unit_add_decl(u, d);

    char *s = emit_header_to_str(u, "TEST_H");
    ASSERT(s != NULL, "got output");
    ASSERT(strstr(s, "int foo(NPObject * self)") != NULL, "has func decl");
    ASSERT(strstr(s, ";") != NULL, "has semicolon");
    ASSERT(strstr(s, "{") == NULL, "no body");
    free(s);
    cg_unit_free(u);
    PASS();
}

static void test_header_struct(void) {
    TEST("header struct forward decl");
    cg_unit_t *u = make_unit();
    cg_decl_t *d = cg_decl_alloc(CGDECL_STRUCT, "MyStruct");
    cg_unit_add_decl(u, d);

    char *s = emit_header_to_str(u, "S_H");
    ASSERT(s != NULL, "got output");
    ASSERT(strstr(s, "struct MyStruct") != NULL, "has struct");
    ASSERT(strstr(s, "{") == NULL, "no body");
    free(s);
    cg_unit_free(u);
    PASS();
}

static void test_header_runtime_include(void) {
    TEST("header includes runtime.h");
    cg_unit_t *u = make_unit();
    cg_decl_t *d = cg_decl_alloc(CGDECL_STRUCT, "Foo");
    cg_unit_add_decl(u, d);

    char *s = emit_header_to_str(u, "X_H");
    ASSERT(s != NULL, "got output");
    ASSERT(strstr(s, "#include <nopa/runtime.h>") != NULL, "has runtime.h");
    free(s);
    cg_unit_free(u);
    PASS();
}

static void test_header_class_meta(void) {
    TEST("header class metadata");
    cg_unit_t *u = make_unit();
    cg_unit_meta_add(u, "Foo", "NPObject", 0, NULL, NULL, 0, NULL, NULL, 0, NULL);

    char *s = emit_header_to_str(u, "META_H");
    ASSERT(s != NULL, "got output");
    ASSERT(strstr(s, "extern NPClass nopa_Foo_class") != NULL, "has class extern");
    ASSERT(strstr(s, "void nopa_meta_init(void)") != NULL, "has init decl");
    free(s);
    cg_unit_free(u);
    PASS();
}

static void test_header_vtable_forward(void) {
    TEST("header vtable forward decl");
    cg_unit_t *u = make_unit();
    cg_unit_meta_add(u, "Bar", NULL, 2, NULL, NULL, 0, NULL, NULL, 0, NULL);
    cg_decl_t *d = cg_decl_alloc(CGDECL_STRUCT, "Bar");
    cg_unit_add_decl(u, d);

    char *s = emit_header_to_str(u, "VT_H");
    ASSERT(s != NULL, "got output");
    ASSERT(strstr(s, "struct nopa_Bar_vtable") != NULL, "has vtable forward");
    free(s);
    cg_unit_free(u);
    PASS();
}

int main(void) {
    printf("codegen header tests\n");
    printf("---------------------\n");
    test_header_empty();
    test_header_func_decl();
    test_header_struct();
    test_header_runtime_include();
    test_header_class_meta();
    test_header_vtable_forward();
    printf("\n%d/%d passed\n", passed, total);
    return passed == total ? 0 : 1;
}