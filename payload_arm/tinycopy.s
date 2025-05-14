@ -*- tab-width: 8 -*-
	.code 16
	.arch armv7-m
	.syntax unified
	.section .text._start, "ax", %progbits
	.p2align 2
	.global _start
	.type _start, %function
_start:
	ldr	r0, 2f
	movs	r3, r0
	ldmia	r0!, {r1,r2}
	str	r0, [r3]
1:	ldrb	r3, [r1], #1
	subs	r2, #1
	strb	r3, [r0], #1
	bne	1b
	ldr	r0, 2f
	bx	lr

	.p2align 2
2:	.long	0x124000


