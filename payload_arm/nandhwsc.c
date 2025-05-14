#include <stdint.h>
#include <stddef.h>

#define MEM4(addr) *(volatile uint32_t*)(addr)

#define NAND_BASE 0xc0150000
#define NAND_REG(x) MEM4(NAND_BASE + x)

static int check_vendor(int id) {
	static const uint8_t allowed[] = {
		0x01, // Spansion
		0x07, // Renesas
		0x20, // ST Micro
		0x2c, // Micron
		0x45, // Sandisk
		0x89, // Intel
		0x92, // EON
		0x98, // Toshiba
		0x9b, // ATO Solution
		0xad, // Hynix
		0xc1, // ???
		0xc2, // Macronix
		0xc8, // GigaDevice
		0xec, // Samsung
		0xef, // Winbond
	};
	unsigned i;
	for (i = 0; i < sizeof(allowed); i++)
		if (id == allowed[i]) return 1;
	return 0;
}

static void nand_power(int a0, int a1, int a2) {
	a0 = a0 << 12 | 1;
	a1 = a1 << 12 | 1;
	a2 = a2 << 12 | 1;
	MEM4(0xc01c0004) = a1;
	MEM4(0xc01c0008) = a1;
	MEM4(0xc01c000c) = a1;
	MEM4(0xc01c0010) = a1;
	MEM4(0xc01c0014) = a1;
	MEM4(0xc01c0018) = a1;
	MEM4(0xc01c001c) = a1;
	MEM4(0xc01c0020) = a1;
	MEM4(0xc01c0024) = a2;
	MEM4(0xc01c0028) = a2;
	MEM4(0xc01c002c) = a0;
	MEM4(0xc01c0030) = a0;
	MEM4(0xc01c0034) = a2;
	MEM4(0xc01c0044) = a2 | 0x100;
	MEM4(0xc01c0048) = a0;
}

static int wait_bits(uint32_t a0, uint32_t a1, uint32_t a2) {
	unsigned i;
	for (i = 0; i < 0x80000; i++)
		if ((MEM4(a0) & a1) == a2) return 1;
	return 0;
}

static void nand_wait(void) {
	while ((int32_t)NAND_REG(4) < 0);
}

static void nand_restore_ctl(uint32_t a0) {
	NAND_REG(0) = a0 | 1;
}

static uint32_t nand_select(uint32_t i) {
	uint32_t old = NAND_REG(0);
	NAND_REG(0) &= ~0x78;
	NAND_REG(0) |= 8 << i | 1;
	return old;
}

static void nand_reset(void) {
	uint32_t old = nand_select(0);
	NAND_REG(0x20) = 0xff; // Reset
	NAND_REG(0x24) = 0x62;
	NAND_REG(0x38) |= 1;
	nand_wait();
	nand_restore_ctl(old);
}

static int nand_read_id(uint32_t cs, uint32_t *buf) {
	uint32_t t0, t1, t2, t3; int i;
	if (cs) return 0;
	t0 = NAND_REG(8); // config
	t1 = NAND_REG(0x38); // fsm_start
	t2 = NAND_REG(0xc); // bc
	t3 = nand_select(cs);

	NAND_REG(0xc) = 8; // mbytecnt
	NAND_REG(0x18) = 0; // rowaddr0
	NAND_REG(0) &= ~0x100; // bsel = 0
	NAND_REG(8) &= ~0x70; // config_rowadd = 0
	NAND_REG(0x20) = 0x90; // cmd_fsm0 = Read ID
	NAND_REG(0x24) = 0x69; // cmd_fsm_ctl0: dataats0, srow0, scmd0, fsmenc0
	NAND_REG(0x38) &= ~0xff;
	NAND_REG(0x38) |= 1; // fsm_start = 1

	nand_wait();
	while (!(NAND_REG(4) & 0x10)); // status_rdrq
	for (i = 0; i < 2; i++)
		buf[i] = NAND_REG(0x10); // data

	nand_restore_ctl(t3);
	NAND_REG(8) = t0;
	NAND_REG(0x38) = t1;
	NAND_REG(0xc) = t2;
	return 1;
}

static void nand_init(void) {
	MEM4(0xc0001008) |= 3;
	MEM4(0xc0001010) = 0x10;
	// nand reset
	MEM4(0xc0000000) &= ~2;
	MEM4(0xc0000000) |= 2;
	// dma reset
	MEM4(0xc0000000) &= ~1;
	MEM4(0xc0000000) |= 1;

	nand_power(3, 2, 1);

	NAND_REG(8) &= ~0xf0;
	NAND_REG(0x38) &= ~0x1c;
	NAND_REG(0x38) |= 0xc;
	NAND_REG(0) |= 0x100;
	NAND_REG(0) &= ~0x79;
	NAND_REG(0) |= 1;

	wait_bits(NAND_BASE + 4, 1u << 31, 0);
	nand_reset();
}

void* entry_main(void) {
	static const uint8_t tab[][3] = {
		{ 0, 0, 0 },
		{ 1, 1, 1 },
		{ 1, 2, 1 },
		{ 3, 2, 1 },
		{ 4, 3, 2 },
		{ 4, 4, 3 },
	};
	unsigned i, n = 0x9c;
	uint8_t *d = (void*)0x124008;
	uint32_t *p;

	for (i = 0; i < n; i += 4) *(uint32_t*)(d + i) = 0;
	d[0] = 'H';
	d[1] = 'W';
	*(uint32_t*)(d + 0x18) = 0x125400;

	nand_init();
	for (i = 0; i < 6; i++) {
		nand_power(tab[i][0], tab[i][1], tab[i][2]);
		nand_read_id(0, (uint32_t*)(d + 0x10));
		if (check_vendor(d[0x10]) == 1) break;
	}
	p = (uint32_t*)(d - 8);
	p[0] = (uint32_t)d; p[1] = n;
	return p;
}

