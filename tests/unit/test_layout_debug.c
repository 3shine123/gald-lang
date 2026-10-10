#include "jeti/layout.h"
#include "jeti/parser.h"
#include "jeti/lexer.h"
#include "jeti/cst.h"
#include "jeti/symbol.h"
#include "jeti/binder.h"
#include <stdio.h>
#include <string.h>

int main(void) {
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
    lexer_init(&lexer, src, strlen(src), "test.jeti");
    parser_t *p = parser_create(&lexer);
    translation_unit_t *unit = parser_parse_translation_unit(p);
    printf("unit->decl_count = %d\n", unit->decl_count);
    for (int i = 0; i < unit->decl_count; i++) {
        printf("  decl[%d]: kind=%d name='%s'\n", i, unit->decls[i]->kind,
               unit->decls[i]->name ? unit->decls[i]->name : "(null)");
    }

    symbol_table_t *st = symtab_alloc();
    binder_t *b = binder_create(st);
    binder_bind(b, unit);
    printf("global symbols: %d\n", st->global->symbol_count);
    for (int i = 0; i < st->global->symbol_count; i++) {
        symbol_t *s = st->global->symbols[i];
        printf("  sym[%d]: kind=%d name='%s'\n", i, s->kind, s->name);
        if (s->kind == SYM_CLASS) {
            printf("    methods: %d\n", s->data.cls.method_count);
            for (int j = 0; j < s->data.cls.method_count; j++) {
                printf("      method[%d]: name='%s' vt_idx=%d\n", j,
                       s->data.cls.methods[j]->name,
                       s->data.cls.methods[j]->data.method.vtable_index);
            }
        }
    }

    layout_ctx_t *ctx = layout_create(st);
    int r = layout_compute(ctx);
    printf("layout = %d\n", r);

    // Check results
    symbol_t *base = symtab_find_class(st, "Base");
    printf("Base methods after layout:\n");
    for (int j = 0; j < base->data.cls.method_count; j++) {
        printf("  method[%d]: name='%s' vt_idx=%d\n", j,
               base->data.cls.methods[j]->name,
               base->data.cls.methods[j]->data.method.vtable_index);
    }

    symbol_t *sub = symtab_find_class(st, "Derived");
    printf("Derived methods after layout:\n");
    for (int j = 0; j < sub->data.cls.method_count; j++) {
        printf("  method[%d]: name='%s' vt_idx=%d\n", j,
               sub->data.cls.methods[j]->name,
               sub->data.cls.methods[j]->data.method.vtable_index);
    }

    layout_destroy(ctx);
    binder_destroy(b);
    symtab_free(st);
    cst_unit_free(unit);
    parser_destroy(p);
    return 0;
}