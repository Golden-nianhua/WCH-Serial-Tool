    .section .boot,"ax",@progbits
    .global _boot_start
    .align 1

_boot_start:
    j _start
