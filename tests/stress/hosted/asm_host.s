.text

// int asm_square(int v)
.globl _asm_square
_asm_square:
    mul     w0, w0, w0
    ret

// int asm_add3(int a, int b, int c)
.globl _asm_add3
_asm_add3:
    add     w0, w0, w1
    add     w0, w0, w2
    ret

// unsigned asm_rotr32(unsigned v, unsigned r)
.globl _asm_rotr32
_asm_rotr32:
    ror     w0, w0, w2
    ret
