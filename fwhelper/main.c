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
		} else {
			ERR_EXIT("unknown command\n");
		}
	}
}

