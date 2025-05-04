# -*- tab-width: 8 -*-
	.text
	.set	nomips16
	.set	nomicromips
	.set	noreorder
	.set	nomacro
	.set	mips32r2

	.p2align 2
	.globl	_start
	.ent	_start
	.type	_start, @function
_start:
.if 1
	lui	$v0, %hi(entry_main)
	addiu	$v0, %lo(entry_main)
	jr	$v0
.else
	j	entry_main
.endif
	nop

	.end	_start
	.size	_start, .-_start
