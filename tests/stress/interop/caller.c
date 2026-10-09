// caller.c — plain C host for three-way interop:
//   C (this file) → Ovel (lib.c, via bridge header lib.h) → asm (asm_lib.s)
//   and Ovel → C (helper.c) / Ovel → asm directly.
// Build: see build.sh. main() must call ovel_metaInit() first.
#include <stdio.h>
#include "lib.h"

// Free functions and class metadata are not bridged (the bridge header
// emits instance-method wrappers only) — declare manually; defined in lib.c.
extern int ovel_add(int a, int b);
extern NPClass OVEL_CLASS_$_Calc;

int main(void) {
    ovel_metaInit();   // required before any class use

    // ── 1. C → Ovel free function (which links ARM64 asm) ──
    printf("[X1] ovel_add(20,22) = %d (expect 42)\n", ovel_add(20, 22));

    // ── 2. C → Ovel object lifecycle ──
    // +alloc is a class method (inherited from NPObject): the bridge header
    // emits instance wrappers only, so dispatch it via the class metadata
    // exactly like generated code does.
    Calc *calc = (Calc *)ovel_Calc_init(ovel_alloc(&OVEL_CLASS_$_Calc));
    if (!calc) { printf("alloc/init failed\n"); return 1; }

    ovel_Calc_add_(calc, 10);
    ovel_Calc_add_(calc, 32);
    printf("[X2] total = %d (expect 42)\n", ovel_Calc_total(calc));

    // ── 3. C → Ovel method → ARM64 asm ──
    printf("[X3] squareOf(9) via asm = %d (expect 81)\n",
           ovel_Calc_squareOf_(calc, 9));

    // ── 4. C → Ovel method → plain C helper ──
    printf("[X4] scale 6 by 7 via C helper = %d (expect 42)\n",
           ovel_Calc_scale_by_(calc, 6, 7));

    // ── 5. C → Ovel method that uses a block internally ──
    int r = ovel_Calc_mix_with_(calc, 5, 8);
    printf("[X5] mix(5,with:8) = %d (expect 41), total now %d (expect 83)\n",
           r, ovel_Calc_total(calc));

    printf("=== interop done ===\n");
    return 0;
}
