#include "jeti/binder.h"
#include "jeti/parser.h"
#include "jeti/lexer.h"
#include "jeti/cst.h"
#include <stdio.h>
#include <string.h>

int main(void) {
    const char *src = "@interface Foo\n- (int)bar;\n@end\n\n@implementation Foo\n- (int)bar {\n    return 42;\n}\n@end";
    lexer_t lexer;
    lexer_init(&lexer, src, strlen(src), "test.jeti");
    parser_t *p = parser_create(&lexer);
    translation_unit_t *u = parser_parse_translation_unit(p);
    if (!u) {
        printf("Parse failed: %s\n", parser_last_error(p));
        parser_destroy(p);
        return 1;
    }
    printf("Parse OK, has %d decls\n", u->decl_count);
    for (int i = 0; i < u->decl_count; i++) {
        printf("  decl %d: kind=%d name=%s\n", i, u->decls[i]->kind, u->decls[i]->name ? u->decls[i]->name : "?");
        if (u->decls[i]->kind == CST_DECL_CLASS_INTERFACE || u->decls[i]->kind == CST_DECL_CLASS_IMPLEMENTATION) {
            printf("    method_count=%d\n", u->decls[i]->data.class_.method_count);
        }
    }

    symbol_table_t *st = symtab_alloc();
    binder_t *b = binder_create(st);
    int r = binder_bind(b, u);
    printf("Bind result: %d, has_error=%d, errmsg='%s'\n", r, binder_has_error(b), binder_last_error(b));

    parser_destroy(p);
    binder_destroy(b);
    symtab_free(st);
    cst_unit_free(u);
    return r ? 0 : 1;
}