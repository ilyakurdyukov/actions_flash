#include <stdint.h>
#include <stddef.h>

#define MEM4(addr) *(volatile uint32_t*)(addr)
#define MEM2(addr) *(volatile uint16_t*)(addr)
#define MEM1(addr) *(volatile uint8_t*)(addr)

#define DELAY(n) { \
	unsigned _count = n; \
	do __asm__ __volatile__(""); while (--_count); \
}

#define NAND_BASE 0xc0150000
#define NAND_REG(x) MEM4(NAND_BASE + x)

#define RTC_BASE 0xc0030000
#define DMA_BASE 0xc0070000

#define REG_SAVE_AREA 0x100000

#define DEF_CONST_FN(addr, ret, name, args) \
	static ret (* const name) args = (ret (*) args)(addr | 1);

// watchdog
static void wd_clear(void) {
	MEM4(RTC_BASE + 0x1c) = 0x1d;
}

#if 0
DEF_CONST_FN(0x4ca0, int, wait_bits, (uint32_t, uint32_t, uint32_t, unsigned))
#else
static int wait_bits(uint32_t addr, uint32_t mask, uint32_t val, unsigned n) {
	n *= 2180;
	do if ((MEM4(addr) & mask) == val) return 0;
	while (--n);
	return 1;
}
#endif

#if 0
DEF_CONST_FN(0x25fc, void, device_disable, (uint32_t, uint32_t))
#else
static void device_disable(uint32_t msk, uint32_t idx) {
	MEM4(0xc0000000 + idx * 4) &= ~msk;
	DELAY(0x10 * 8)
}
#endif

#if 0
DEF_CONST_FN(0x2620, void, device_reset, (uint32_t, uint32_t))
#else
static void device_reset(uint32_t msk, uint32_t idx) {
	device_disable(msk, idx);
	// device_enable
	MEM4(0xc0000000 + idx * 4) |= msk;
	DELAY(0x10 * 8)
}
#endif

#if 0
DEF_CONST_FN(0x26d8, void, nand_clean, (void))
#else
static void nand_clean(void) {
	NAND_REG(0) &= ~1;
	NAND_REG(0) |= 1;
	wait_bits(NAND_BASE + 4, 1u << 31, 0, 2);
}
#endif

#if 0
DEF_CONST_FN(0x2704, void, nand_power, (int, int, int))
#else
static void nand_power(int a0, int a1, int a2) {
	uint32_t buf = REG_SAVE_AREA;
	int i;
	for (i = 0; i < 0x48; i += 4) {
		if (i == 0x34) i += 0xc;
		MEM4(buf + 0x50 + i) = MEM4(0xc01c0004 + i);
	}

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
#endif

#if 0
DEF_CONST_FN(0x1430, void, nand_init, (void))
#else
static void nand_init(void) {
	uint32_t buf = REG_SAVE_AREA;

	MEM4(buf + 0x34) = MEM4(0xc0001008);
	MEM4(0xc0001008) |= 3;
	MEM4(buf + 0x44) = MEM4(0xc0001010);
	MEM4(0xc0001010) = 0x10;

	device_reset(2, 0); // nand reset
	device_reset(1, 0); // dma reset
	nand_power(3, 2, 1);

	NAND_REG(8) &= ~0xf0;
	NAND_REG(0x38) &= ~0x1c;
	NAND_REG(0x38) |= 0xc;
	NAND_REG(0) |= 0x100;

	nand_clean();
}
#endif

#if 0
DEF_CONST_FN(0x14ac, void, nand_deinit, (void))
#else
static void nand_deinit(void) {
	uint32_t buf = REG_SAVE_AREA;
	int i;

	device_disable(2, 0); // nand
	device_disable(1, 0); // dma

	// nand_power_restore
	for (i = 0; i < 0x48; i += 4) {
		if (i == 0x34) i += 0xc;
		MEM4(0xc01c0004 + i) = MEM4(buf + 0x50 + i);
	}
	MEM4(0xc0001010) = MEM4(buf + 0x44);
	MEM4(0xc0001008) = MEM4(buf + 0x34);
}
#endif

typedef struct {
	uint32_t rowaddr;
	void *buf;
	uint32_t readmsk; // 512 bytes per bit in mask
	uint8_t udata[8];
} nand_args_t;

typedef struct {
	uint8_t psec; // psize >> 9, sectors per page
	uint8_t b1, b2;
	uint8_t rsize; // read granularity (n * 256 bytes)
	uint8_t b4, b5, cmd_fe, unused;
	uint16_t h8; // pages per block
	uint16_t psize; // page size
} nand_conf_t;

static nand_args_t * const nand_args = (void*)0x100920;
static nand_conf_t * const nand_conf = (void*)0x100934;

#if 0
DEF_CONST_FN(0x14dc, int, nand_read, (void*, const void*))
#else
extern int nand_read0(nand_args_t *args, const nand_conf_t *conf);
static int nand_read(nand_args_t *args, const nand_conf_t *conf) {
	unsigned ret = 0, rsize, udata_size;
	uint8_t *dst;

	*(uint32_t*)&args->udata[0] = 0;
	*(uint32_t*)&args->udata[4] = 0;

	rsize = conf->rsize << 8;
	dst = args->buf;

	if (!conf->cmd_fe) {
		NAND_REG(0) &= ~0x3000;
		NAND_REG(0x5c) = 0;
	} else {
		MEM4(0xc01c0048) |= 0x200;
		NAND_REG(0) &= ~0x3000;
		NAND_REG(0) |= 0x2000;
		NAND_REG(0x5c) = 0x84011085;
	}

	NAND_REG(0x18) = args->rowaddr; // rowaddr0
	NAND_REG(0x14) = 0; // coladdr
	NAND_REG(8) &= ~0x70; // config_rowadd
	NAND_REG(8) |= (conf->b1 - 1) << 4;
	NAND_REG(0x38) |= 0x84;
	NAND_REG(8) &= ~0x303;

	if (!conf->b4) NAND_REG(0x38) &= ~4;
	udata_size = 2;
	if (conf->b4 > 0xc) {
		// x <= 0x18 : 0x101
		// x <= 0x28 : 0x102
		// else      : 0x103
		unsigned tmp = (conf->b4 + 7) >> 4;
		if (tmp > 3) tmp = 3;
		NAND_REG(8) |= tmp | 0x100;
		udata_size = 4;
	}
	NAND_REG(0xc) = udata_size << 8;

	if (conf->b5 < 3) {
		uint32_t readmsk, coladdr;
		unsigned i, n;

		if (conf->b5 == 1) {
			NAND_REG(0x20) = 0x3000a2;
			NAND_REG(0x24) = 0x626c60;
		} else {
			NAND_REG(0x20) = 0x3000;
			NAND_REG(0x24) = 0x626c;
		}
		NAND_REG(0x38) &= ~2;
		NAND_REG(0x38) |= 1;
		wait_bits(NAND_BASE + 4, 1u << 31, 0, 2);
		wait_bits(NAND_BASE + 4, 2, 2, 2);

		n = conf->psec;
		if (conf->rsize == 4) n >>= 1;

		readmsk = args->readmsk;
		coladdr = conf->psize << 16;

		for (i = 0; i < n; i++) {
			NAND_REG(0x14) = coladdr;
			if (readmsk & 1) {
				NAND_REG(0x20) = 0xe005e005;
				if (conf->b5 == 2)
					NAND_REG(0x24) = 0x5141;
				else if (conf->b5 == 1)
					NAND_REG(0x24) = 0x717c616c;
				else
					NAND_REG(0x24) = 0x71746164;
				NAND_REG(0x38) &= ~2;
				NAND_REG(0x38) |= 1;

				MEM4(DMA_BASE + 0x100) = 0x98;
				MEM4(DMA_BASE + 0x108) = NAND_BASE + 0x10;
				MEM4(DMA_BASE + 0x110) = (uint32_t)dst;
				MEM4(DMA_BASE + 0x118) = rsize;
				MEM4(DMA_BASE + 0x104) |= 1;

				wait_bits(NAND_BASE + 4, 1u << 31, 0, 2);
				if (wait_bits(DMA_BASE + 0x104, 1, 0, 2)) {
					device_reset(2, 0); // nand reset

					NAND_REG(8) &= ~0xf0;
					NAND_REG(0x38) &= ~0x1c;
					NAND_REG(0x38) |= 0xc;
					NAND_REG(0) |= 0x100;
					nand_clean();
					NAND_REG(0) &= ~0x78;
					NAND_REG(0) |= 9;

					MEM4(DMA_BASE + 0x104) &= 0xfe;
					ret = 1;
					break;
				}
				if ((NAND_REG(4) & 0x3f0000) == 0x3f0000) {
					ret = 1;
				}
				dst += rsize;
				if (conf->b4 < 0xd) {
					if (i < 4)
						*(uint16_t*)&args->udata[i << 1] = NAND_REG(0x4c);
				} else if (i < 2)
					*(uint32_t*)&args->udata[i << 2] = NAND_REG(0x4c);
			}
			readmsk >>= 1;
			coladdr += rsize;
			coladdr += (udata_size + conf->b2) << 16;
		}
	} else ret = 1;
	nand_clean();
	return ret;
}
#endif

#if 0
DEF_CONST_FN(0x2520, int, test_checksum, (const void*, unsigned))
#else
static int test_checksum(const void *addr, unsigned n) {
	const uint16_t *p = addr;
	unsigned i, sum = 0;
	for (i = 1; i < n >> 1; i++) sum += p[i];
	return *p != (uint16_t)(sum + 0x1234);
}
#endif

#if 0
DEF_CONST_FN(0x1890, int, try_read_mbrec, (void))
#else
static int try_read_mbrec(void) {
	unsigned i;
	static const uint32_t tab[][3] = {
		{ 0x4460310,  0x28, 0x20000080 },
		{ 0x42a0310,  0x18, 0x20000080 },
		{ 0x42a0308,  0x18, 0x10000080 },
		{ 0x46a0310,  0x3c, 0x20000080 },
		{ 0x20e0308,     8, 0x10000080 },
		{ 0x2000308,     0, 0x10000080 },
		{ 0x20e0304,     8,  0x8000080 },
#if 0 // 2127
		{ 0x20e0401, 0x308,  0x2000020 },
#endif
		{ 0x4460320,  0x28, 0x40000080 },
		{ 0x46a0320,  0x3c, 0x40000080 },
		{ 0x2000308, 0x200, 0x10000080 },
		{ 0x46a0310, 0x13c, 0x20000080 },
		{ 0x4460310, 0x128, 0x20000080 },
		{ 0x46a0320, 0x13c, 0x40000080 },
		{ 0x4460320, 0x128, 0x40000080 },
#if 1 // 2157
		{ 0x46a0340,  0x3c, 0x80000080 },
		{ 0x4460340,  0x28, 0x80000080 },
#endif
	};

	for (i = 0; i < sizeof(tab) / sizeof(*tab); i++) {
		uint8_t *buf; int n;

		wd_clear();
		MEM4(nand_conf) = tab[i][0];
		MEM2(&nand_conf->b4) = tab[i][1];
		MEM4(&nand_conf->h8) = tab[i][2];
#if 1 // 2157
		nand_args->readmsk = nand_conf->b4 < 0x18 ? 3 : 1;
#endif
		nand_read(nand_args, nand_conf);
		buf = nand_args->buf;
		if (buf[2] == 0xa5) n = 0x200;
		else if (buf[2] == 0x5a) n = 0x400;
		else continue;
		if (!test_checksum(buf, n)) return 0;
	}
	return 1;
}
#endif

#if 0
DEF_CONST_FN(0x2638, void, nand_reset, (void))
#else
static void nand_reset(void) {
	NAND_REG(0x20) = 0xff; // Reset
	NAND_REG(0x24) = 0x62;
	NAND_REG(0x38) |= 1;
	wait_bits(NAND_BASE + 4, 1u << 31, 0, 100);
	wait_bits(NAND_BASE + 4, 2, 2, 2);
}
#endif

#if 0
DEF_CONST_FN(0x1d14, int, nand_set_features, (void))
#else
static int nand_set_features(void) {
	uint32_t old0, old1, old2, old3;
	int ret;

	old0 = NAND_REG(0);
	old1 = NAND_REG(8);
	old2 = NAND_REG(0xc);
	old3 = NAND_REG(0x38);

	NAND_REG(0) &= ~0x3100;
	NAND_REG(8) &= ~0x70;
	NAND_REG(0x18) = 2; // rowaddr0
	NAND_REG(0x38) &= ~0x1c;
	NAND_REG(0xc) = 4;
	NAND_REG(0x20) = 0xef; // Set Features
	NAND_REG(0x24) = 0x69;
	NAND_REG(0x38) |= 3;

	ret = wait_bits(NAND_BASE + 4, 0x10, 0x10, 2);
	NAND_REG(0x10) = 0;
	ret |= wait_bits(NAND_BASE + 4, 1u << 31, 0, 2);
	ret |= wait_bits(NAND_BASE + 4, 2, 2, 2);

	NAND_REG(0) = old0;
	NAND_REG(8) = old1;
	NAND_REG(0xc) = old2;
	NAND_REG(0x38) = old3;
	return ret;
}
#endif

#if 0
DEF_CONST_FN(0x17ec, int, read_mbrec, (void))
#else
static int read_mbrec(void) {
	int i, j, ret;

	wd_clear();
	NAND_REG(0) &= ~0x78;
	NAND_REG(0) |= 9;

	nand_args->buf = (void*)MEM4(REG_SAVE_AREA);
	nand_args->readmsk = 3;

	for (j = 0; j < 2; j++) {
		nand_reset();
		if (j) nand_set_features();
		nand_conf->cmd_fe = j;
		for (i = 0; i < 6; i++) {
			nand_args->rowaddr = 0x20 << i & ~0x20; // rowaddr0
			ret = try_read_mbrec();
			if (!ret) goto end;
		}
	}
	ret = 1;
end:
	NAND_REG(0) &= ~0x79;
	return ret;
}
#endif

void* entry_main(void) {
	uint32_t *p = (void*)0x11ffe0;
	int ret, flags = p[4];
	p[0] = (uint32_t)p + 8;
	p[1] = 0;

#if 0 // debug
	flags = 7;
	p[5] = 0x11a000;
#endif

	if (flags & 1) {
		MEM4(REG_SAVE_AREA) = p[5];
		nand_init();
		ret = read_mbrec();
		p[1] = 4;
		p[2] = 0x12345678 ^ -ret;
		if (ret) goto end;
		p[4] = 2 | 4;
	}
	if (flags & 2) {
		uint8_t *mbrec = (void*)p[5];
		wd_clear();

		NAND_REG(8) &= ~0xf0;
		NAND_REG(0x38) &= ~0x1c;
		NAND_REG(0x38) |= 0x9c;
		NAND_REG(0) |= 0x100;

		NAND_REG(0) &= ~0x78;
		NAND_REG(0) |= 9;
		nand_reset();
		DELAY(0x500 * 2)
		if (mbrec[0x3c0] == 3) {
			NAND_REG(0x20) = 0x38;
			NAND_REG(0x24) = 0x62;
			NAND_REG(0x38) |= 1;
			while ((int32_t)NAND_REG(4) < 0);
		}
		// ED 60 5A 03  04 10 03 46  04 28 00 00  00 01 00 20
		// CHECK XX B0  B1 00 01 02  03 04 05 06  08_09 0A_0B
		nand_conf->psec = mbrec[5];
		nand_conf->b1 = mbrec[6];
		nand_conf->b2 = mbrec[7];
		nand_conf->rsize = mbrec[8];
		nand_conf->b4 = mbrec[9];
		nand_conf->b5 = mbrec[10];
		nand_conf->cmd_fe = mbrec[11];
		nand_conf->h8 = *(uint16_t*)&mbrec[12];
		nand_conf->psize = *(uint16_t*)&mbrec[14];

		if (NAND_REG(0) & 0x2000) nand_conf->cmd_fe = 1;
		if (nand_conf->h8 == 0xc0) nand_conf->h8 = 0x100;
		nand_args->rowaddr = mbrec[3 + 0] * nand_conf->h8;
		{
			unsigned x = nand_conf->psec;
			if (nand_conf->b4 >= 0x18) x >>= 1;
			nand_args->readmsk = (2u << (x - 1)) - 1;
		}
		p[4] = 4;
	}
	if (flags & 4) {
		wd_clear();
		nand_read(nand_args, nand_conf);
	}
end:
	if (flags & 0x80) {
		NAND_REG(0) &= ~0x78;
		nand_deinit();
		p[4] = 0;
	}
	return p;
}

