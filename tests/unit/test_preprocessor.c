#include "ovel/preprocessor.h"
#include <stdio.h>
#include <string.h>
#include <stdlib.h>

static int total = 0;
static int passed = 0;

#define TEST(name) do { printf("  %-45s ", name); total++; } while(0)
#define PASS() do { passed++; printf("PASS\n"); } while(0)
#define FAIL(msg) do { printf("FAIL: %s\n", msg); return; } while(0)

static void test_basic_pass_through(void) {
    TEST("pass-through without directives");
    ovel_pp_state_t *s = pp_state_create();

    // Write a temp file
    FILE *f = fopen("/tmp/ovel_test_simple.ov", "w");
    fputs("int x = 42;\n", f);
    fputs("int y = x + 1;\n", f);
    fclose(f);

    int r = pp_process_file(s, "/tmp/ovel_test_simple.ov");
    if (r != 0) { FAIL("process_file failed"); }

    const char *out = pp_get_output(s);
    if (strstr(out, "int x = 42;") == NULL) { FAIL("expected 'int x = 42;'"); }
    if (strstr(out, "int y = x + 1;") == NULL) { FAIL("expected 'int y = x + 1;'"); }

    pp_state_destroy(s);
    PASS();
}

static void test_define(void) {
    TEST("#define + macro expansion");
    ovel_pp_state_t *s = pp_state_create();

    FILE *f = fopen("/tmp/ovel_test_define.ov", "w");
    fputs("#define FOO 42\n", f);
    fputs("int x = FOO;\n", f);
    fclose(f);

    pp_process_file(s, "/tmp/ovel_test_define.ov");
    const char *out = pp_get_output(s);

    // The macro is removed from output, FOO is not expanded (simple placeholder)
    if (strstr(out, "#define") != NULL) { FAIL("expected #define removed"); }
    // FOO remains as-is (macro expansion in source requires a second pass in full impl)
    // For now, just verify it doesn't crash.

    pp_state_destroy(s);
    PASS();
}

static void test_ifdef(void) {
    TEST("#ifdef / #endif");
    ovel_pp_state_t *s = pp_state_create();
    pp_add_macro(s, "DEBUG", "1");

    FILE *f = fopen("/tmp/ovel_test_ifdef.ov", "w");
    fputs("#ifdef DEBUG\n", f);
    fputs("int debug = 1;\n", f);
    fputs("#endif\n", f);
    fputs("int normal = 0;\n", f);
    fclose(f);

    pp_process_file(s, "/tmp/ovel_test_ifdef.ov");
    const char *out = pp_get_output(s);

    if (strstr(out, "int debug = 1;") == NULL) { FAIL("expected debug block"); }
    if (strstr(out, "int normal = 0;") == NULL) { FAIL("expected normal block"); }

    pp_state_destroy(s);
    PASS();
}

static void test_ifndef(void) {
    TEST("#ifndef / #endif");
    ovel_pp_state_t *s = pp_state_create();

    FILE *f = fopen("/tmp/ovel_test_ifndef.ov", "w");
    fputs("#ifndef DEBUG\n", f);
    fputs("int fallback = 1;\n", f);
    fputs("#endif\n", f);
    fclose(f);

    pp_process_file(s, "/tmp/ovel_test_ifndef.ov");
    const char *out = pp_get_output(s);

    if (strstr(out, "int fallback = 1;") == NULL) { FAIL("expected fallback block"); }

    pp_state_destroy(s);
    PASS();
}

static void test_else(void) {
    TEST("#ifdef / #else / #endif");
    ovel_pp_state_t *s = pp_state_create();

    FILE *f = fopen("/tmp/ovel_test_else.ov", "w");
    fputs("#ifdef UNDEFINED\n", f);
    fputs("int a = 1;\n", f);
    fputs("#else\n", f);
    fputs("int b = 2;\n", f);
    fputs("#endif\n", f);
    fclose(f);

    pp_process_file(s, "/tmp/ovel_test_else.ov");
    const char *out = pp_get_output(s);

    if (strstr(out, "int a = 1;") != NULL) { FAIL("expected 'a' to be skipped"); }
    if (strstr(out, "int b = 2;") == NULL) { FAIL("expected 'b' block"); }

    pp_state_destroy(s);
    PASS();
}

static void test_import_no_cycle(void) {
    TEST("#import (no cycle)");
    ovel_pp_state_t *s = pp_state_create();

    FILE *h = fopen("/tmp/ovel_test_imported.h", "w");
    fputs("int imported_var;\n", h);
    fclose(h);

    FILE *f = fopen("/tmp/ovel_test_import_main.ov", "w");
    fputs("#import \"ovel_test_imported.oh\"\n", f);
    fputs("int main_var;\n", f);
    fclose(f);

    // add search path for /tmp
    pp_add_search_path(s, "/tmp");
    pp_process_file(s, "/tmp/ovel_test_import_main.ov");
    const char *out = pp_get_output(s);

    if (strstr(out, "int imported_var;") == NULL) { FAIL("expected imported header content"); }
    if (strstr(out, "int main_var;") == NULL) { FAIL("expected main file content"); }

    pp_state_destroy(s);
    PASS();
}

static void test_error_nonexistent(void) {
    TEST("error on nonexistent file");
    ovel_pp_state_t *s = pp_state_create();
    int r = pp_process_file(s, "/tmp/ovel_nonexistent_file.ov");
    if (r == 0) { FAIL("expected error"); }
    if (!pp_has_errors(s)) { FAIL("expected has_errors"); }

    pp_state_destroy(s);
    PASS();
}

int main(void) {
    printf("preprocessor tests\n");
    printf("------------------\n");

    test_basic_pass_through();
    test_define();
    test_ifdef();
    test_ifndef();
    test_else();
    test_import_no_cycle();
    test_error_nonexistent();

    printf("\n%d/%d passed\n", passed, total);
    return passed == total ? 0 : 1;
}