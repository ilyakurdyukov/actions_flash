#include <stdint.h>
#include <stddef.h>

#define MEM4(addr) *(volatile uint32_t*)(addr)
#define MEM2(addr) *(volatile uint16_t*)(addr)
#define MEM1(addr) *(volatile uint8_t*)(addr)

#define DELAY(n) { \
	unsigned _count = n; \
	do __asm__ __volatile__(""); while (--_count); \
}

#define NAND_BASE 0xc0070000
#define NAND_REG(x) MEM4(NAND_BASE + x)

#define REG_SAVE_AREA 0xbfc34000

#if 0
static int (* const wait_bits)(uint32_t, uint32_t, uint32_t, uint32_t) = (int(*)())(0xbfc01194 + 1);
#else
static int wait_bits(uint32_t addr, uint32_t mask, uint32_t val, unsigned n) {
	n <<= 10;
	do if ((MEM4(addr) & mask) == val) return 0;
	while (--n);
	return 1;
}
#endif

#if 0
static void (* const device_reset)(uint32_t) = (void(*)())(0xbfc01144 + 1);
#else
static void device_reset(uint32_t msk) {
	MEM4(0xc0000000) &= ~msk; // mrcr
	DELAY(0x10 * 0x80)
	MEM4(0xc0000000) |= msk;
	DELAY(0x10 * 0x80)
}
#endif

#if 0
static void (* const nand_clean)(void) = (void(*)())(0xbfc010a8 + 1);
#else
static void nand_clean(void) {
	NAND_REG(0) &= ~1;
	NAND_REG(0) |= 1;
	wait_bits(NAND_BASE + 4, 1u << 31, 0, 2);
}
#endif

#if 0
static void (* const nand_init)(void) = (void(*)())(0xbfc00ec8 + 1);
#else
static void nand_init(void) {
	uint32_t buf = REG_SAVE_AREA;

	MEM4(buf + 0x3c) = MEM4(0xc0000008);
	MEM4(0xc0000008) |= 0x201; // clkenctl
	MEM4(buf + 0x48) = MEM4(0xc0000030);
	MEM4(0xc0000030) &= ~0x03007000; // memclkctl1
	MEM4(buf + 0x4c) = MEM4(0xc0000024);
	MEM4(0xc0000024) = 8; // nandclkctl

	device_reset(1 << 8); // nand reset
	device_reset(1); // dma reset

	MEM4(buf + 0x50) = MEM4(0xc009002c);
	MEM4(buf + 0x54) = MEM4(0xc0090030);
	MEM4(buf + 0x60) = MEM4(0xc0090034);
	MEM4(0xc009002c) &= 0xc000003f;
	MEM4(0xc0090030) &= 0xc00003ff;
	MEM4(0xc0090030) |= 0x12400;
	MEM4(0xc0090034) &= ~0x1fff;
	MEM4(buf + 0x58) = MEM4(0xc0090040);
	MEM4(0xc0090040) &= ~0xe04;
	MEM4(0xc0090040) |= 0x204;

	NAND_REG(8) &= ~0xf0;
	NAND_REG(0x38) &= ~0x1c;
	NAND_REG(0x38) |= 0xc;
	NAND_REG(0) |= 0x100;

	nand_clean();
}
#endif

#if 0
static void (* const nand_deinit)(void) = (void(*)())(0xbfc01020 + 1);
#else
static void nand_deinit(void) {
	uint32_t buf = REG_SAVE_AREA;

	device_reset(1 << 8); // nand reset
	MEM4(0xc009002c) = MEM4(buf + 0x50);
	MEM4(0xc0090030) = MEM4(buf + 0x54);
	MEM4(0xc0090034) = MEM4(buf + 0x60);
	MEM4(0xc0090040) = MEM4(buf + 0x58);

	MEM4(0xc0000030) = MEM4(buf + 0x48);
	MEM4(0xc0000024) = MEM4(buf + 0x4c);
	MEM4(0xc0000008) = MEM4(buf + 0x3c);
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

static nand_args_t * const nand_args = (void*)0xbfc341e0;
static nand_conf_t * const nand_conf = (void*)0xbfc341f4;

#if 0
static int (* const nand_read)(void*, const void*) = (int(*)())(0xbfc012f8 + 1);
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
		MEM4(0xc0090040) |= 0x400;
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
	NAND_REG(0xc) = 0x400;

	udata_size = 4;
	if (!conf->b4) NAND_REG(0x38) &= ~4;
	if (conf->b4 < 0xd) {
		NAND_REG(0xc) = 0x200;
		udata_size = 2;
	} else if (conf->b4 < 0x19) {
		NAND_REG(8) |= 0x101;
	} else if (conf->b4 < 0x29) {
		NAND_REG(8) |= 0x102;
	} else {
		NAND_REG(8) |= 0x103;
	}

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

				MEM4(0xc00c0010) &= ~0xff0;
				MEM4(0xc00c0010) |= 0x80;
				MEM4(0xc00c0014) = NAND_BASE + 0x10;
				MEM4(0xc00c0020) = (uint32_t)dst;
				MEM4(0xc00c0028) = rsize;
				MEM4(0xc00c0010) |= 1;

				wait_bits(NAND_BASE + 4, 1u << 31, 0, 2);
				if (wait_bits(0xc00c0010, 1, 0, 2))
					device_reset(1); // dma reset
				(void)NAND_REG(4);

				dst += rsize;
				if (conf->b4 < 0xd) {
					if (i < 4)
						*(uint16_t*)&args->udata[i << 1] = NAND_REG(0x4c);
				} else if (i < 2)
					*(uint32_t*)&args->udata[i << 2] = NAND_REG(0x4c);
			}
			coladdr += 0x200; readmsk >>= 1;
			if (conf->rsize == 4) {
				coladdr += 0x200; readmsk >>= 1;
			}
			coladdr += (udata_size + conf->b2) << 16;
		}
	} else if (conf->b5 == 3) {
		NAND_REG(0x18) = args->rowaddr << 8; // rowaddr0
		NAND_REG(0x20) = 0;
		NAND_REG(0x24) = 0x516b;
		NAND_REG(0x38) &= ~2;
		NAND_REG(0x38) |= 1;
		MEM4(0xc00c0010) &= ~0xff0;
		MEM4(0xc00c0010) |= 0x80;
		MEM4(0xc00c0014) = NAND_BASE + 0x10;
		MEM4(0xc00c0020) = (uint32_t)dst;
		MEM4(0xc00c0028) = 0x200;
		MEM4(0xc00c0010) |= 1;

		wait_bits(NAND_BASE + 4, 2, 2, 2);
		wait_bits(NAND_BASE + 4, 1u << 31, 0, 2);
		if (wait_bits(0xc00c0010, 1, 0, 2))
			device_reset(1); // dma reset
		(void)NAND_REG(4);
		*(uint16_t*)args->udata = NAND_REG(0x4c);
	} else ret = 1;
	nand_clean();
	return ret;
}
#endif

#if 0
static int (* const test_checksum)(const void*, unsigned) = (int(*)())(0xbfc00458 + 1);
#else
static int test_checksum(const void *addr, unsigned n) {
	const uint16_t *p = addr;
	unsigned i, sum = 0;
	for (i = 1; i < n >> 1; i++) sum += p[i];
	return *p != (uint16_t)(sum + 0x1234);
}
#endif

#if 0
static int (* const try_read_mbrec)(void) = (int(*)())(0xbfc00d94 + 1);
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
		{ 0x20e0401, 0x308,  0x2000020 },
		{ 0x4460320,  0x28, 0x40000080 },
		{ 0x46a0320,  0x3c, 0x40000080 },
		{ 0x2000308, 0x200, 0x10000080 },
		{ 0x46a0310, 0x13c, 0x20000080 },
		{ 0x4460310, 0x128, 0x20000080 },
		{ 0x46a0320, 0x13c, 0x40000080 },
		{ 0x4460320, 0x128, 0x40000080 }
	};

	for (i = 0; i < sizeof(tab) / sizeof(*tab); i++) {
		uint8_t *buf; int n;

		MEM4(0xc012001c) |= 1;
		MEM4(nand_conf) = tab[i][0];
		MEM2(&nand_conf->b4) = tab[i][1];
		MEM4(&nand_conf->h8) = tab[i][2];
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
static void (* const nand_reset)(void) = (void(*)())(0xbfc010ec + 1);
#else
static void nand_reset(void) {
	NAND_REG(0x20) = 0xff; // Reset
	NAND_REG(0x24) = 0x62;
	NAND_REG(0x38) |= 1;
#if 1
	wait_bits(NAND_BASE + 4, 1u << 31, 0, 100);
	wait_bits(NAND_BASE + 4, 2, 2, 2);
#else
	while ((int32_t)NAND_REG(4) < 0);
	while (!(NAND_REG(4) & 2));
#endif
}
#endif

#if 0
static int (* const nand_set_features)(void) = (int(*)())(0xbfc011ec + 1);
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
#if 1
	ret = wait_bits(NAND_BASE + 4, 0x10, 0x10, 2);
	ret |= wait_bits(NAND_BASE + 4, 1u << 31, 0, 2);
	ret |= wait_bits(NAND_BASE + 4, 2, 2, 2);
#else
	while (!(NAND_REG(4) & 0x10));
	while ((int32_t)NAND_REG(4) < 0);
	while (!(NAND_REG(4) & 2));
	ret = 0;
#endif
	NAND_REG(0) = old0;
	NAND_REG(8) = old1;
	NAND_REG(0xc) = old2;
	NAND_REG(0x38) = old3;
	return ret;
}
#endif

#if 0
static int (* const read_mbrec)(void) = (int(*)())(0xbfc00cb0 + 1);
#else
static int read_mbrec(void) {
	int i, j, ret;

	MEM4(0xc012001c) |= 1;
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
	uint32_t *p = (void*)0x9fc1ffe0;
	int ret, flags = p[4];
	p[0] = (uint32_t)p + 8;
	p[1] = 0;

#if 0 // debug
	flags = 7;
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
		MEM4(0xc012001c) |= 1; // watchdog clear
#if 0
		NAND_REG(0) &= ~0x78;
		NAND_REG(0) |= 9;
		nand_reset();
		if (nand_conf->cmd_fe) nand_set_features();
#else
		NAND_REG(8) &= ~0xf0;
		NAND_REG(0x38) &= ~0x1c;
		NAND_REG(0x38) |= 0x9c;
		NAND_REG(0) |= 0x100;

		NAND_REG(0) &= ~0x78;
		NAND_REG(0) |= 9;
		nand_reset();
		DELAY(0x500 * 0x80)
		if (mbrec[0x3c0] == 3) {
			NAND_REG(0x20) = 0x38;
			NAND_REG(0x24) = 0x62;
			NAND_REG(0x38) |= 1;
			while ((int32_t)NAND_REG(4) < 0);
		}
		// 3A 03 5A 03  04 20 03 46  04 28 00 00  00 01 00 40
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
			int x = nand_conf->psec;
			if (x > 31) x = 31;
			nand_args->readmsk = (1u << x) - 1;
		}
#endif
		p[4] = 4;
	}
	if (flags & 4) {
		MEM4(0xc012001c) |= 1; // watchdog clear
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

