.syntax unified
.cpu cortex-m0plus
.thumb
.global _start
.type _start, %function
.thumb_func
_start:
    bl test_main
    movs r7, #1
    svc #0
1:  b 1b
