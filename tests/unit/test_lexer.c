#include "nopa/lexer.h"
#include "nopa/token.h"
#include <stdio.h>
#include <string.h>
#include <stdlib.h>

static int total = 0;
static int passed = 0;

#define TEST(name) do { \
    printf("  %-40s ", name); \
    total++; \
} while(0)

#define PASS() do { passed++; printf("PASS\n"); } while(0)
#define FAIL(msg) do { printf("FAIL: %s\n", msg); } while(0)

static void test_identifiers(void) {
    TEST("identifiers");
    const char *src = "foo bar _private";
    lexer_t l;
    lexer_init(&l, src, strlen(src), "test.np");

    token_t t = lexer_next(&l);
    if (t.kind != TOKEN_IDENTIFIER || t.length != 3 || strncmp(t.start, "foo", 3) != 0) {
        FAIL("expected 'foo'");
        return;
    }
    t = lexer_next(&l);
    if (t.kind != TOKEN_IDENTIFIER || t.length != 3 || strncmp(t.start, "bar", 3) != 0) {
        FAIL("expected 'bar'");
        return;
    }
    t = lexer_next(&l);
    if (t.kind != TOKEN_IDENTIFIER || t.length != 8 || strncmp(t.start, "_private", 8) != 0) {
        FAIL("expected '_private'");
        return;
    }
    PASS();
}

static void test_keywords(void) {
    TEST("keywords @interface @end");

    const char *src = "@interface @end self";
    lexer_t l;
    lexer_init(&l, src, strlen(src), "test.np");

    token_t t = lexer_next(&l);
    if (t.kind != TOKEN_KEYWORD || t.keyword != KW_AT_INTERFACE) { FAIL("expected @interface"); return; }
    t = lexer_next(&l);
    if (t.kind != TOKEN_KEYWORD || t.keyword != KW_AT_END) { FAIL("expected @end"); return; }
    t = lexer_next(&l);
    if (t.kind != TOKEN_KEYWORD || t.keyword != KW_SELF) { FAIL("expected self"); return; }
    PASS();
}

static void test_integers(void) {
    TEST("integers");
    const char *src = "42 0xFF 123u";
    lexer_t l;
    lexer_init(&l, src, strlen(src), "test.np");

    token_t t = lexer_next(&l);
    if (t.kind != TOKEN_INTEGER || t.length != 2) { FAIL("expected 42"); return; }
    t = lexer_next(&l);
    if (t.kind != TOKEN_INTEGER || t.length != 4) { FAIL("expected 0xFF"); return; }
    t = lexer_next(&l);
    if (t.kind != TOKEN_INTEGER || t.length != 4) { FAIL("expected 123u"); return; }
    PASS();
}

static void test_floats(void) {
    TEST("floats");
    const char *src = "3.14 1e10 .5";
    lexer_t l;
    lexer_init(&l, src, strlen(src), "test.np");

    token_t t = lexer_next(&l);
    if (t.kind != TOKEN_FLOAT) { FAIL("expected float 3.14"); return; }
    t = lexer_next(&l);
    if (t.kind != TOKEN_FLOAT) { FAIL("expected float 1e10"); return; }
    t = lexer_next(&l);
    if (t.kind != TOKEN_FLOAT) { FAIL("expected float .5"); return; }
    PASS();
}

static void test_strings(void) {
    TEST("string literals");
    const char *src = "\"hello\" @\"world\"";
    lexer_t l;
    lexer_init(&l, src, strlen(src), "test.np");

    token_t t = lexer_next(&l);
    if (t.kind != TOKEN_STRING || t.length != 5) { FAIL("expected \"hello\""); return; }
    t = lexer_next(&l);
    if (t.kind != TOKEN_STRING || t.length != 5) { FAIL("expected @\"world\""); return; }
    PASS();
}

static void test_operators(void) {
    TEST("operators");
    const char *src = "++ == -> != ...";
    lexer_t l;
    lexer_init(&l, src, strlen(src), "test.np");

    token_t t = lexer_next(&l);
    if (t.kind != TOKEN_INCR) { FAIL("expected ++"); return; }
    t = lexer_next(&l);
    if (t.kind != TOKEN_EQ) { FAIL("expected =="); return; }
    t = lexer_next(&l);
    if (t.kind != TOKEN_ARROW) { FAIL("expected ->"); return; }
    t = lexer_next(&l);
    if (t.kind != TOKEN_NEQ) { FAIL("expected !="); return; }
    t = lexer_next(&l);
    if (t.kind != TOKEN_ELLIPSIS) { FAIL("expected ..."); return; }
    PASS();
}

static void test_comments(void) {
    TEST("comments skipped");
    const char *src = "/* block */ foo\nbar // line comment";
    lexer_t l;
    lexer_init(&l, src, strlen(src), "test.np");
    token_t t = lexer_next(&l);
    if (t.kind != TOKEN_IDENTIFIER || strncmp(t.start, "foo", 3) != 0) { FAIL("expected 'foo' after block comment"); return; }
    t = lexer_next(&l);
    if (t.kind != TOKEN_IDENTIFIER || strncmp(t.start, "bar", 3) != 0) { FAIL("expected 'bar' after newline+line comment"); return; }
    PASS();
}

static void test_eof(void) {
    TEST("EOF");
    lexer_t l;
    lexer_init(&l, "", 0, "test.np");
    token_t t = lexer_next(&l);
    if (t.kind != TOKEN_EOF) { FAIL("expected EOF"); return; }
    PASS();
}

int main(void) {
    printf("lexer tests\n");
    printf("-----------\n");

    test_identifiers();
    test_keywords();
    test_integers();
    test_floats();
    test_strings();
    test_operators();
    test_comments();
    test_eof();

    printf("\n%d/%d passed\n", passed, total);
    return passed == total ? 0 : 1;
}