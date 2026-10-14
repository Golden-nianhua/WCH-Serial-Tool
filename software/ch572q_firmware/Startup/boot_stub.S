    .section .boot,"ax",@progbits
    .global _boot_start
    .align 1

_boot_start:
    li t0, 0x20002ff0
    lw t1, 0(t0)
    li t2, 0x424c4532
    bne t1, t2, 1f
    lw t1, 12(t0)
    li t2, 0xbdb3bacd
    bne t1, t2, 1f
    sw zero, 0(t0)
    sw zero, 12(t0)
    li t0, 0x00012000
    jr t0
1:
    j _start
