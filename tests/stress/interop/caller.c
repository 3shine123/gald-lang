// caller.c — plain C host for three-way interop:
//   C (this file) → Jeti (lib.c, via bridge header lib.h) → asm (asm_lib.s)
//   and Jeti → C (helper.c) / Jeti → asm directly.
// Build: see build.sh. main() must call jeti_metaInit() first.
#include <stdio.h>
#include "lib.h"

// Free functions and class metadata are not bridged (the bridge header
// emits instance-method wrappers only) — declare manually; defined in lib.c.
extern int jeti_add(int a, int b);
extern NPClass JETI_CLASS_$_Calc;

int main(void) {
    jeti_metaInit();   // required before any class use

    // ── 1. C → Jeti free function (which links ARM64 asm) ──
    printf("[X1] jeti_add(20,22) = %d (expect 42)\n", jeti_add(20, 22));

    // ── 2. C → Jeti object lifecycle ──
    // +alloc is a class method (inherited from NPObject): the bridge header
    // emits instance wrappers only, so dispatch it via the class metadata
    // exactly like generated code does.
    Calc *calc = (Calc *)jeti_Calc_init(jeti_alloc(&JETI_CLASS_$_Calc));
    if (!calc) { printf("alloc/init failed\n"); return 1; }

    jeti_Calc_add_(calc, 10);
    jeti_Calc_add_(calc, 32);
    printf("[X2] total = %d (expect 42)\n", jeti_Calc_total(calc));

    // ── 3. C → Jeti method → ARM64 asm ──
    printf("[X3] squareOf(9) via asm = %d (expect 81)\n",
           jeti_Calc_squareOf_(calc, 9));

    // ── 4. C → Jeti method → plain C helper ──
    printf("[X4] scale 6 by 7 via C helper = %d (expect 42)\n",
           jeti_Calc_scale_by_(calc, 6, 7));

    // ── 5. C → Jeti method that uses a block internally ──
    int r = jeti_Calc_mix_with_(calc, 5, 8);
    printf("[X5] mix(5,with:8) = %d (expect 41), total now %d (expect 83)\n",
           r, jeti_Calc_total(calc));

    printf("=== interop done ===\n");
    return 0;
}
