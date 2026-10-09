# asm_x64_ext.s — x86_64 (Mach-O/AT&T) external routines for the Rosetta cross-target test.
# Built with `ovicc -arch x86_64` and executed via Rosetta on this arm64 Mac.

    .text
    .balign 16

# int asm_x64_square(int)      — SysV: arg0 in %edi
    .globl _asm_x64_square
_asm_x64_square:
    movl    %edi, %eax
    imull   %edi, %eax
    ret

# int asm_x64_sum3(int, int, int)  — args: %edi, %esi, %edx
    .globl _asm_x64_sum3
_asm_x64_sum3:
    leal    (%rdi, %rsi), %eax
    addl    %edx, %eax
    ret

# unsigned asm_x64_rotl32(unsigned x, int n)  — rotate left
    .globl _asm_x64_rotl32
_asm_x64_rotl32:
    movl    %edi, %eax
    movl    %esi, %ecx
    roll    %cl, %eax
    ret

# unsigned asm_x64_clz32(unsigned) — count leading zero bits (32 for 0)
    .globl _asm_x64_clz32
_asm_x64_clz32:
    movl    %edi, %eax
    testl   %eax, %eax
    jz      .Lclz_zero
    bsrl    %eax, %eax
    movl    $31, %ecx
    subl    %eax, %ecx
    movl    %ecx, %eax
    ret
.Lclz_zero:
    movl    $32, %eax
    ret