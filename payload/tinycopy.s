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
	.set	mips16

	lw	$v0, 2f
	lw	$a0, 0($v0) # addr
	lw	$a1, 4($v0) # num
	addiu	$a2, $v0, 8 # buf
	addu	$a1, $a0 # end
	sw	$a2, 0($v0)
1:	lbu	$v1, 0($a0)
	addiu	$a0, 1
	sb	$v1, 0($a2)
	addiu	$a2, 1
	cmp	$a0, $a1
	btnez	1b
	jrc	$ra

	.p2align 2
2:	.word	0x9fc24000

.else
	lui	$v0, 0x9fc2
	ori	$v0, 0x4000
	lw	$a0, 0($v0) # addr
	lw	$a1, 4($v0) # num
	addiu	$a2, $v0, 8 # buf
	addu	$a1, $a0 # end
	sw	$a2, 0($v0)
1:	lbu	$v1, 0($a0)
	addiu	$a0, 1
	sb	$v1, 0($a2)
	bnel	$a0, $a1, 1b
	addiu	$a2, 1
	jr	$ra
	nop
.endif

	.end	_start
	.size	_start, .-_start
