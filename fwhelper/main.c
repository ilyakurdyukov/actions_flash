#include <stdlib.h>
#include <stdio.h>
#include <string.h>
#include <stdint.h>

static uint8_t* loadfile(const char *fn, size_t *num) {
	size_t n, j = 0; uint8_t *buf = 0;
	FILE *fi = fopen(fn, "rb");
	if (fi) {
		fseek(fi, 0, SEEK_END);
		n = ftell(fi);
		if (n) {
			fseek(fi, 0, SEEK_SET);
			buf = (uint8_t*)malloc(n);
			if (buf) j = fread(buf, 1, n, fi);
		}
		fclose(fi);
	}
	if (num) *num = j;
	return buf;
}

#define ERR_EXIT(...) \
	do { fprintf(stderr, __VA_ARGS__); return 1; } while (0)

#define READ16_LE(p) ( \
	((uint8_t*)(p))[1] << 8 | \
	((uint8_t*)(p))[0])

#define READ32_LE(p) ( \
	((uint8_t*)(p))[3] << 24 | \
	((uint8_t*)(p))[2] << 16 | \
	((uint8_t*)(p))[1] << 8 | \
	((uint8_t*)(p))[0])

static uint32_t fw_checksum16(const uint8_t *p, unsigned size) {
	const uint8_t *e = p + (size & ~1);
	uint32_t sum = 0;
	for (; p != e; p += 2) sum += READ16_LE(p);
	return sum & 0xffff;
}

static uint32_t fw_checksum32(const uint8_t *p, unsigned size) {
	const uint8_t *e = p + (size & ~3);
	uint32_t sum = 0;
	for (; p != e; p += 4) sum += READ32_LE(p);
	return sum;
}

static int scan_yqhx(uint8_t *mem, unsigned size) {
	uint32_t len;
	printf("yqhx v%u.%u, type = ", mem[2], mem[3]);
	{
		unsigned a = mem[0];
		printf(a - 'A' < 26 ? "'%c'" : "0x%02x", a);
	}
	printf(", group = %u\n", mem[1]);
	printf("code: offset = 0x%x, size = 0x%x, base = 0x%x\n",
			READ32_LE(mem + 8), READ32_LE(mem + 0xc), READ32_LE(mem + 0x10));

	len = READ32_LE(mem + 0x18);
	if (len)
		printf("data: offset = 0x%x, size = 0x%x, base = 0x%x\n",
				READ32_LE(mem + 0x14), len, READ32_LE(mem + 0x1c));

	len = READ32_LE(mem + 0x20);
	if (len)
		printf("bss: size = 0x%x, base = 0x%x\n",
				len, READ32_LE(mem + 0x24));

	printf("init = 0x%x, exit = 0x%x, export = 0x%x\n",
			READ32_LE(mem + 0x28), READ32_LE(mem + 0x2c), READ32_LE(mem + 0x3c));

	len = READ32_LE(mem + 0x34);
	if (len)
		printf("extra: offset = 0x%x, size = 0x%x\n",
				READ32_LE(mem + 0x30), len);

	return 0;
}

static int scan_file(uint8_t *mem, unsigned size) {
	if (size >= 0x40 && READ32_LE(mem + 4) == 0x78687179) // "yqhx"
		scan_yqhx(mem, size);
	return 0;
}

static int unpack_lfi(uint8_t *mem, unsigned size, int flags) {
	unsigned i;
	uint32_t chk1, chk2;

	if (READ32_LE(mem) != 0x0ff0aa55)
		ERR_EXIT("!!! wrong firmware magic\n");

	chk1 = READ16_LE(mem + 0x1fe);
	chk2 = fw_checksum16(mem, 0x1fe);
	if (chk1 != chk2)
		ERR_EXIT("!!! wrong head checksum (expected 0x%04x, got 0x%04x)\n", chk1, chk2);

	chk1 = READ32_LE(mem + 0x10);
	chk2 = fw_checksum32(mem + 0x200, 0x1e00);
	if (chk1 != chk2)
		ERR_EXIT("!!! wrong dir checksum (expected 0x%04x, got 0x%04x)\n", chk1, chk2);

	for (i = 0x200; i < 0x2000; i += 0x20) {
		char name[13];
		uint32_t off, len;
		if (!mem[i]) break;
		{
			unsigned a, j, k = 0, l;
			for (j = 0; j < 11; j++) {
				a = mem[i + j];
				if (a - 0x20 >= 0x5f || strchr("./\\'\"?*", a)) break;
			}
			if (j < 11) {
				fprintf(stderr, "!!! 0x%x: invalid character in the name (0x%02x)\n", i, a);
				continue;
			}
			for (k = j = 0; j < 8; j++) {
				name[j] = a = mem[i + j];
				if (a != ' ') k = j + 1;
			}
			name[l = k++] = '.';
			for (; j < 11; j++) {
				name[k++] = a = mem[i + j];
				if (a != ' ') l = k;
			}
			name[l] = 0;
		}
		off = READ32_LE(mem + i + 0x10);
		len = READ32_LE(mem + i + 0x14);
		if (off >> (32 - 9)) {
			fprintf(stderr, "!!! 0x%x: offset too big\n", i);
			continue;
		}
		off <<= 9;
		printf("0x%x: \"%s\", offset = 0x%x, size = 0x%x\n", i, name, off, len);
		if (off >= size || size - off < len) {
			fprintf(stderr, "!!! data outside the file\n");
			continue;
		}
		chk1 = READ32_LE(mem + i + 0x1c);
		chk2 = fw_checksum32(mem + off, len);
		if (chk1 != chk2)
			fprintf(stderr, "!!! wrong file checksum (expected 0x%08x, got 0x%08x)\n", chk1, chk2);
		scan_file(mem + off, len);
		if (flags & 1) {
			FILE *fo;
			if (!(fo = fopen(name, "wb")))
				ERR_EXIT("fopen(output) failed\n");
			fwrite(mem + off, 1, len, fo);
			fclose(fo);
		}
	}
	return 0;
}

static int check_lfi_head(uint8_t *mem) {
	if (READ32_LE(mem) != 0x0ff0aa55) return -1;
	if ((int)fw_checksum16(mem, 0x1fe) != READ16_LE(mem + 0x1fe))
		return -1;
	return 0;
}

static int repair_file(uint8_t *mem, uint8_t *mem2, uint32_t psize,
		uint32_t off, uint32_t len, uint32_t chk0) {
	uint32_t bad_blocks[31][2];
	uint32_t i, j = 0, k, n, chk1, chk2, found;
	int ret;

	for (i = off; i < off + len; i += n) {
		n = psize - i % psize;
		k = off + len - i;
		if (n > k) n = k;
		if (!memcmp(mem + i, mem2 + i, n)) continue;
		k = i + n - psize;
		printf("damaged page (offset = 0x%x or 0x%x)\n", k, (int)(mem2 - mem) + k);
		if (j >= 31) {
			printf("too many damaged pages (offset = 0x%x, size = 0x%x)\n", off, len);
			return 1;
		}
		bad_blocks[j][0] = k;
		bad_blocks[j][1] = fw_checksum32(mem2 + i, n) - fw_checksum32(mem + i, n);
		j++;
	}
	if (!j) return 0;

	chk1 = fw_checksum32(mem + off, len) - chk0;
	found = ~0;
	for (i = 0; !(i >> j); i++) {
		chk2 = chk1;
		for (k = 0; k < j; k++)
			if (i >> k & 1) chk2 += bad_blocks[k][1];
		if (chk2) continue;
		if (~found) {
			printf("ambiguity found, cannot repair (offset = 0x%x, size = 0x%x)\n", off, len);
			return 1;
		}
		found = i;
	}
	if (!~found) {
		printf("no solutions found, cannot repair (offset = 0x%x, size = 0x%x)\n", off, len);
		return 1;
	}
	ret = 0;
	for (k = 0; k < j; k++)
		if (found >> k & 1) {
			i = bad_blocks[k][0];
			printf("page replaced (offset = 0x%x)\n", i);
			memcpy(mem + i, mem2 + i, psize);
			ret = 2;
		}
	return ret;
}

static int lfi_repair(uint8_t *mem, uint32_t size,
		uint32_t fw_size, uint32_t npages, uint32_t psize) {
	unsigned i;
	uint8_t *mem2;
	uint32_t bsize = psize * npages;
	int ret = 0;

#if 0
	{
		uint32_t fw_blocks = (fw_size + bsize - 1) / bsize;
		mem2 = mem + fw_blocks * bsize;
	}
#else
	mem2 = mem + size / 2;
#endif

	if (check_lfi_head(mem)) {
		if (check_lfi_head(mem2)) {
			printf("both LFI headers are damaged\n");
			return 1;
		}
		printf("page replaced (offset = 0x%x)", 0);
		memcpy(mem, mem2, psize);
	}
	if (repair_file(mem, mem2, psize, 0x200, 0x1e00, READ32_LE(mem + 0x10)) & 1)
		return 1;

	for (i = 0x200; i < 0x2000; i += 0x20) {
		uint32_t off, len;
		if (!mem[i]) break;
		off = READ32_LE(mem + i + 0x10);
		len = READ32_LE(mem + i + 0x14);
		if (off >> (32 - 9)) continue;
		off <<= 9;
		if (off >= size || size - off < len) continue;
		ret |= repair_file(mem, mem2, psize, off, len, READ32_LE(mem + i + 0x1c));
	}
	return ret;
}

int main(int argc, char **argv) {
	uint8_t *mem; size_t size = 0;

	if (argc < 3)
		ERR_EXIT("Usage: %s flash.bin cmd args...\n", argv[0]);

	mem = loadfile(argv[1], &size);
	if (!mem) ERR_EXIT("loadfile failed\n");
	argc -= 1; argv += 1;

	while (argc > 1) {
		if (!strcmp(argv[1], "scan_lfi")) {
			unpack_lfi(mem, size, 0);
			argc -= 1; argv += 1;
		} else if (!strcmp(argv[1], "unpack_lfi")) {
			unpack_lfi(mem, size, 1);
			argc -= 1; argv += 1;
		} else if (!strcmp(argv[1], "scan_file")) {
			scan_file(mem, size);
			argc -= 1; argv += 1;
		} else if (!strcmp(argv[1], "lfi_repair")) {
			uint32_t fw_size, npages, psize;
			const char *out_fn;
			if (argc <= 5) ERR_EXIT("bad command\n");
			fw_size = strtol(argv[2], NULL, 0);
			if (fw_size < 0x2200 >> 9)
				ERR_EXIT("firmware size is too small\n");
			if (fw_size >> (32 - 9))
				ERR_EXIT("firmware size is too big\n");
			fw_size <<= 9;
			if (size >> 1 < fw_size)
				ERR_EXIT("dump_size < fw_size * 2\n");

			npages = strtol(argv[3], NULL, 0);
			if (!npages || npages >> 16)
				ERR_EXIT("unexpected number of pages\n");

			psize = strtol(argv[4], NULL, 0); // pow2, 0x200..0x4000
			if (!psize || (psize & (psize - 1)) || (psize & ~0x7e00))
				ERR_EXIT("unexpected page size\n");

			out_fn = argv[5];
			if (!strcmp(out_fn, "-")) out_fn = NULL;

			if (lfi_repair(mem, size, fw_size, npages, psize) != 1 && out_fn) {
				FILE *fo = fopen(out_fn, "wb");
				if (!fo) ERR_EXIT("fopen(wb) failed\n");
				if (fwrite(mem, 1, fw_size, fo) != fw_size)
					ERR_EXIT("fwrite failed\n");
				fclose(fo);
			}
			size = fw_size;
			argc -= 5; argv += 5;
		} else {
			ERR_EXIT("unknown command\n");
		}
	}
}
