#include "gald/codegen.h"
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

static int total = 0, passed = 0;
#define TEST(n) do { printf("  %-50s ", n); total++; } while(0)
#define PASS() do { passed++; printf("PASS\n"); } while(0)
#define FAIL(m) do { printf("FAIL: %s\n", m); return; } while(0)
#define ASSERT(cond, m) do { if (!(cond)) { printf("FAIL: %s\n", m); return; } } while(0)

static char *emit_to_str(cg_unit_t *unit) {
    char buf[65536];
    FILE *fp = fmemopen(buf, sizeof(buf), "w");
    if (!fp) return NULL;
    cg_emit(unit, fp);
    fclose(fp);
    return strdup(buf);
}

static cg_unit_t *make_unit(void) {
    return cg_unit_alloc("test.gm");
}

static void test_emit_empty(void) {
    TEST("emit empty unit");
    cg_unit_t *u = make_unit();
    char *s = emit_to_str(u);
    ASSERT(s != NULL, "got output");
    ASSERT(strstr(s, "// Generated") == NULL, "no header in cg_emit");
    free(s);
    cg_unit_free(u);
    PASS();
}

static void test_emit_int(void) {
    TEST("emit int expr");
    cg_unit_t *u = make_unit();
    cg_decl_t *d = cg_decl_alloc(CGDECL_FUNCTION, "foo");
    d->u.func.return_type = strdup("int");
    d->u.func.param_count = 0;
    d->u.func.body = cg_stmt_alloc(CGSTMT_COMPOUND);

    cg_stmt_t *ret = cg_stmt_alloc(CGSTMT_RETURN);
    ret->u.return_.value = cg_expr_alloc(CEXPR_INT);
    ret->u.return_.value->u.int_val = 42;
    cg_compound_add(d->u.func.body, ret);

    cg_unit_add_decl(u, d);
    char *s = emit_to_str(u);
    ASSERT(s != NULL, "got output");
    ASSERT(strstr(s, "return 42;") != NULL, "has return 42");
    free(s);
    cg_unit_free(u);
    PASS();
}

static void test_emit_if(void) {
    TEST("emit if-else");
    cg_unit_t *u = make_unit();
    cg_decl_t *d = cg_decl_alloc(CGDECL_FUNCTION, "test");
    d->u.func.return_type = strdup("void");
    d->u.func.body = cg_stmt_alloc(CGSTMT_COMPOUND);

    cg_stmt_t *if_stmt = cg_stmt_alloc(CGSTMT_IF);
    if_stmt->u.if_.cond = cg_expr_alloc(CEXPR_INT);
    if_stmt->u.if_.cond->u.int_val = 1;
    if_stmt->u.if_.then = cg_stmt_alloc(CGSTMT_RETURN);
    if_stmt->u.if_.then->u.return_.value = NULL;
    if_stmt->u.if_.else_ = cg_stmt_alloc(CGSTMT_COMPOUND);
    cg_compound_add(d->u.func.body, if_stmt);

    cg_unit_add_decl(u, d);
    char *s = emit_to_str(u);
    ASSERT(s != NULL, "got output");
    ASSERT(strstr(s, "if") != NULL, "has if");
    ASSERT(strstr(s, "else") != NULL, "has else");
    free(s);
    cg_unit_free(u);
    PASS();
}

static void test_emit_while(void) {
    TEST("emit while loop");
    cg_unit_t *u = make_unit();
    cg_decl_t *d = cg_decl_alloc(CGDECL_FUNCTION, "loop");
    d->u.func.return_type = strdup("void");
    d->u.func.body = cg_stmt_alloc(CGSTMT_COMPOUND);

    cg_stmt_t *wh = cg_stmt_alloc(CGSTMT_WHILE);
    wh->u.while_.cond = cg_expr_alloc(CEXPR_INT);
    wh->u.while_.cond->u.int_val = 1;
    wh->u.while_.body = cg_stmt_alloc(CGSTMT_BREAK);
    cg_compound_add(d->u.func.body, wh);

    cg_unit_add_decl(u, d);
    char *s = emit_to_str(u);
    ASSERT(s != NULL, "got output");
    ASSERT(strstr(s, "while (1)") != NULL, "has while(1)");
    ASSERT(strstr(s, "break;") != NULL, "has break");
    free(s);
    cg_unit_free(u);
    PASS();
}

static void test_emit_binary(void) {
    TEST("emit binary expr");
    cg_unit_t *u = make_unit();
    cg_decl_t *d = cg_decl_alloc(CGDECL_FUNCTION, "add");
    d->u.func.return_type = strdup("int");
    d->u.func.body = cg_stmt_alloc(CGSTMT_COMPOUND);

    cg_expr_t *bin = cg_expr_alloc(CEXPR_BINARY);
    bin->u.binary.left = cg_expr_alloc(CEXPR_INT);
    bin->u.binary.left->u.int_val = 1;
    bin->u.binary.right = cg_expr_alloc(CEXPR_INT);
    bin->u.binary.right->u.int_val = 2;

    cg_stmt_t *ret = cg_stmt_alloc(CGSTMT_RETURN);
    ret->u.return_.value = bin;
    cg_compound_add(d->u.func.body, ret);
    cg_unit_add_decl(u, d);

    char *s = emit_to_str(u);
    ASSERT(s != NULL, "got output");
    ASSERT(strstr(s, "return 1 op 2") != NULL, "has binary");
    free(s);
    cg_unit_free(u);
    PASS();
}

static void test_emit_call(void) {
    TEST("emit function call");
    cg_unit_t *u = make_unit();

    cg_expr_t *a1 = cg_expr_alloc(CEXPR_INT);
    a1->u.int_val = 7;
    cg_expr_t *args[] = {a1};
    cg_expr_t *call = cg_call_expr("gald_retain", "void", args, 1);

    cg_stmt_t *es = cg_stmt_alloc(CGSTMT_EXPR);
    es->u.expr = call;

    cg_decl_t *d = cg_decl_alloc(CGDECL_FUNCTION, "demo");
    d->u.func.return_type = strdup("void");
    d->u.func.body = cg_stmt_alloc(CGSTMT_COMPOUND);
    cg_compound_add(d->u.func.body, es);
    cg_unit_add_decl(u, d);

    char *s = emit_to_str(u);
    ASSERT(s != NULL, "got output");
    ASSERT(strstr(s, "gald_retain") != NULL, "has gald_retain");
    ASSERT(strstr(s, "7") != NULL, "has 7");
    free(s);
    cg_unit_free(u);
    PASS();
}

static void test_emit_variable(void) {
    TEST("emit variable decl");
    cg_unit_t *u = make_unit();
    cg_decl_t *d = cg_decl_alloc(CGDECL_VARIABLE, "x");
    d->u.var.type = strdup("int");
    d->u.var.init = cg_expr_alloc(CEXPR_INT);
    d->u.var.init->u.int_val = 10;
    d->u.var.is_static = 1;
    cg_unit_add_decl(u, d);

    char *s = emit_to_str(u);
    ASSERT(s != NULL, "got output");
    ASSERT(strstr(s, "static int x = 10") != NULL, "has static int x = 10");
    free(s);
    cg_unit_free(u);
    PASS();
}

static void test_emit_goto_label(void) {
    TEST("emit goto and label");
    cg_unit_t *u = make_unit();
    cg_decl_t *d = cg_decl_alloc(CGDECL_FUNCTION, "f");
    d->u.func.return_type = strdup("void");
    d->u.func.body = cg_stmt_alloc(CGSTMT_COMPOUND);

    cg_stmt_t *label = cg_stmt_alloc(CGSTMT_LABEL);
    label->u.label = strdup("retry");
    cg_compound_add(d->u.func.body, label);

    cg_stmt_t *gto = cg_stmt_alloc(CGSTMT_GOTO);
    gto->u.label = strdup("retry");
    cg_compound_add(d->u.func.body, gto);

    cg_unit_add_decl(u, d);
    char *s = emit_to_str(u);
    ASSERT(s != NULL, "got output");
    ASSERT(strstr(s, "retry:") != NULL, "has label");
    ASSERT(strstr(s, "goto retry;") != NULL, "has goto");
    free(s);
    cg_unit_free(u);
    PASS();
}

static void test_emit_member(void) {
    TEST("emit member access");
    cg_unit_t *u = make_unit();
    cg_expr_t *self = cg_expr_alloc(CEXPR_IDENT);
    self->u.id = strdup("self");
    cg_expr_t *mem = cg_expr_alloc(CEXPR_MEMBER);
    mem->u.member.obj = self;
    mem->u.member.field = strdup("name");

    cg_stmt_t *es = cg_stmt_alloc(CGSTMT_EXPR);
    es->u.expr = mem;

    cg_decl_t *d = cg_decl_alloc(CGDECL_FUNCTION, "g");
    d->u.func.return_type = strdup("void");
    d->u.func.body = cg_stmt_alloc(CGSTMT_COMPOUND);
    cg_compound_add(d->u.func.body, es);
    cg_unit_add_decl(u, d);

    char *s = emit_to_str(u);
    ASSERT(s != NULL, "got output");
    ASSERT(strstr(s, "self.name") != NULL, "has self.name");
    free(s);
    cg_unit_free(u);
    PASS();
}

static void test_emit_switch(void) {
    TEST("emit switch-case");
    cg_unit_t *u = make_unit();
    cg_decl_t *d = cg_decl_alloc(CGDECL_FUNCTION, "f");
    d->u.func.return_type = strdup("void");
    d->u.func.body = cg_stmt_alloc(CGSTMT_COMPOUND);

    cg_stmt_t *sw = cg_stmt_alloc(CGSTMT_SWITCH);
    sw->u.switch_.expr = cg_expr_alloc(CEXPR_IDENT);
    sw->u.switch_.expr->u.id = strdup("x");
    sw->u.switch_.body = cg_stmt_alloc(CGSTMT_COMPOUND);

    cg_stmt_t *cs = cg_stmt_alloc(CGSTMT_CASE);
    cs->u.case_.value = cg_expr_alloc(CEXPR_INT);
    cs->u.case_.value->u.int_val = 1;
    cs->u.case_.body = cg_stmt_alloc(CGSTMT_BREAK);
    cg_compound_add(sw->u.switch_.body, cs);

    cg_compound_add(d->u.func.body, sw);
    cg_unit_add_decl(u, d);

    char *s = emit_to_str(u);
    ASSERT(s != NULL, "got output");
    ASSERT(strstr(s, "switch") != NULL, "has switch");
    ASSERT(strstr(s, "case 1") != NULL, "has case 1");
    free(s);
    cg_unit_free(u);
    PASS();
}

static void test_emit_for_in(void) {
    TEST("emit for-in");
    cg_unit_t *u = make_unit();
    cg_decl_t *d = cg_decl_alloc(CGDECL_FUNCTION, "f");
    d->u.func.return_type = strdup("void");
    d->u.func.body = cg_stmt_alloc(CGSTMT_COMPOUND);

    cg_expr_t *col = cg_expr_alloc(CEXPR_IDENT);
    col->u.id = strdup("arr");

    cg_stmt_t *forin = cg_stmt_alloc(CGSTMT_FOR_IN);
    forin->u.for_in.var_name = strdup("x");
    forin->u.for_in.collection = col;
    forin->u.for_in.body = cg_stmt_alloc(CGSTMT_BREAK);
    cg_compound_add(d->u.func.body, forin);

    cg_unit_add_decl(u, d);
    char *s = emit_to_str(u);
    ASSERT(s != NULL, "got output");
    ASSERT(strstr(s, "for-in: x in collection") != NULL, "has for-in comment");
    ASSERT(strstr(s, "__col = arr") != NULL, "has col assign");
    ASSERT(strstr(s, "__count") != NULL, "has count");
    ASSERT(strstr(s, "objectAtIndex") != NULL, "has objectAtIndex");
    ASSERT(strstr(s, "x = [__col") != NULL, "has x assignment");
    free(s);
    cg_unit_free(u);
    PASS();
}

static void test_emit_protocol_meta(void) {
    TEST("emit protocol metadata");
    cg_unit_t *u = make_unit();

    // Add protocol P with required methods
    char *req_names[] = { strdup("doIt"), strdup("getValue") };
    cg_protocol_meta_t *pm = cg_unit_protocol_add(u, "P", 2, req_names, 0, NULL, 0, NULL);

    // Add class conforming to P
    char *mnames[] = { strdup("doIt"), strdup("getValue") };
    int vindices[] = { 0, 1 };
    cg_protocol_meta_t *protocols[] = { pm };
    cg_unit_meta_add(u, "Foo", "NFObject", 2, mnames, vindices, 0, NULL, NULL, 1, protocols);

    char *s = emit_to_str(u);
    ASSERT(s != NULL, "got output");
    ASSERT(strstr(s, "gald_protocol_P") != NULL, "has protocol var");
    ASSERT(strstr(s, ".required_methods") != NULL, "has required_methods");
    ASSERT(strstr(s, "doIt") != NULL, "has doIt");
    ASSERT(strstr(s, "getValue") != NULL, "has getValue");
    ASSERT(strstr(s, ".required_count = 2") != NULL, "has required_count");
    ASSERT(strstr(s, ".protocols = (NFProtocol *[])") != NULL, "has class protocols");
    ASSERT(strstr(s, "&gald_protocol_P") != NULL, "links to protocol");
    free(s);
    cg_unit_free(u);
    PASS();
}

int main(void) {
    printf("codegen emit tests\n");
    printf("---------------------\n");
    test_emit_empty();
    test_emit_int();
    test_emit_if();
    test_emit_while();
    test_emit_binary();
    test_emit_call();
    test_emit_variable();
    test_emit_goto_label();
    test_emit_member();
    test_emit_switch();
    test_emit_for_in();
    test_emit_protocol_meta();
    printf("\n%d/%d passed\n", passed, total);
    return passed == total ? 0 : 1;
}