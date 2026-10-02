// helpers.c — host-side runtime stubs for the golden test.
// Runtime globals (gald___gald_root_class, exception state, memcpy) and
// the allocator+lifecycle (gald_alloc/init/release) come from
// runtime_freestanding.c; this file only provides console output + factories.
#include <stdio.h>
#include <stdint.h>
#include <stddef.h>
#include <gald/runtime.h>

// ── Console output (the transpiled .gm calls these via extern) ──

void kputs(const char *s) { fputs(s, stdout); }
void kputdec(int v)        { fprintf(stdout, "%d", v); }
void kputhex(unsigned v)   { fprintf(stdout, "%x", v); }

// ── Instance factories (Gald calls these via extern) ──

extern NFClass GALD_CLASS_$_BareMetal__Calculator;
extern NFClass GALD_CLASS_$_BareMetal__NFIoError;

struct BareMetal__Calculator {
    struct NFClass *isa;
    uint32_t retain_count;
    int total;
};

struct BareMetal__NFIoError {
    struct NFClass *isa;
    uint32_t retain_count;
    int code;
};

static struct BareMetal__Calculator g_calc;
static struct BareMetal__NFIoError g_err;

struct BareMetal__Calculator *create_calculator(void) {
    g_calc.isa = &GALD_CLASS_$_BareMetal__Calculator;
    g_calc.retain_count = 1;
    g_calc.total = 0;
    return &g_calc;
}

struct BareMetal__NFIoError *create_error(int code) {
    g_err.isa = &GALD_CLASS_$_BareMetal__NFIoError;
    g_err.retain_count = 1;
    g_err.code = code;
    return &g_err;
}