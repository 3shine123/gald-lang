    .text
    .globl _fs_asm_square
_fs_asm_square:
    mul x0, x0, x0
    ret

    .globl _fs_asm_add3
_fs_asm_add3:
    add x0, x0, x1
    add x0, x0, x2
    ret
