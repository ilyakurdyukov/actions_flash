# -*- tab-width: 8 -*-
	.text
	.set	nomips16
	.set	nomicromips
	.set	noreorder
	.set	nomacro
	.set	noat
	.set	mips32r2

	.p2align 2
	.globl	_start
	.ent	_start
	.type	_start, @function
_start:

_sp = 0xbfc3fe00

.if 0
	lui	$v0, %hi(_start)
	addiu	$v0, %lo(_start)
	sw	$ra, 0x80($v0) # 0xbfc02e45
	sw	$sp, 0x84($v0) # 0xbfc36fe8
.endif
	di
	lui	$sp, %hi(_sp)
	addiu	$sp, %lo(_sp)
	li	$v0, 0x400
	mtc0	$v0, $12 # c0_status
	mtc0	$zero, $12, 1 # c0_intctl
	mtc0	$zero, $12, 2 # c0_srsctl
	mtc0	$zero, $12, 3 # c0_srsmap
	mtc0	$zero, $13 # c0_cause
	lui	$v0, %hi(_start)
	addiu	$v0, %lo(_start)
	mtc0	$v0, $15, 1 # c0_ebase
	# ei
.if 1
	lui	$v0, %hi(entry_main)
	addiu	$v0, %lo(entry_main)
	jr	$v0
.else
	j	entry_main
.endif
	nop

	.globl	int_enable
	.type	int_enable, @function
int_enable:
	ei
	jr	$ra
	nop

int_start_1:
	sw	$at, 0($sp)
	sw	$v0, 4($sp)
	sw	$v1, 8($sp)
	sw	$a0, 0xc($sp)
	sw	$a1, 0x10($sp)
	sw	$a2, 0x14($sp)
	sw	$a3, 0x18($sp)
	sw	$t0, 0x1c($sp)
	sw	$t1, 0x20($sp)
	sw	$t2, 0x24($sp)
	sw	$t3, 0x28($sp)
	sw	$t4, 0x2c($sp)
	sw	$t5, 0x30($sp)
	sw	$t6, 0x34($sp)
	sw	$t7, 0x38($sp)
	# skip s0-s7
	sw	$t8, 0x3c($sp)
	sw	$t9, 0x40($sp)
	# skip k0,k1,gp,fp
	sw	$ra, 0x44($sp)
	jal	int_main
	nop
	lw	$ra, 0x44($sp)
	lw	$t9, 0x40($sp)
	lw	$t8, 0x3c($sp)
	lw	$t7, 0x38($sp)
	lw	$t6, 0x34($sp)
	lw	$t5, 0x30($sp)
	lw	$t4, 0x2c($sp)
	lw	$t3, 0x28($sp)
	lw	$t2, 0x24($sp)
	lw	$t1, 0x20($sp)
	lw	$t0, 0x1c($sp)
	lw	$a3, 0x18($sp)
	lw	$a2, 0x14($sp)
	lw	$a1, 0x10($sp)
	lw	$a0, 0xc($sp)
	lw	$v1, 8($sp)
	lw	$v0, 4($sp)
	lw	$at, 0($sp)
	addiu	$sp, 0x48
	ei
	eret	# no delay slot

	# .p2align 8
	# .fill 128, 1, 0
	.org _start + 0x180, 0

int_start: # must be at c0_ebase + 0x180
	di
	j	int_start_1	# save some space
	addiu	$sp, -0x48

	.end	_start
	.size	_start, .-_start

