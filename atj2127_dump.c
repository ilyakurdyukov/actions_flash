/*
// Actions ATJ2127 firmware dumper for Linux.
//
// THE SOFTWARE IS PROVIDED "AS IS", WITHOUT WARRANTY OF ANY KIND, EXPRESS
// OR IMPLIED, INCLUDING BUT NOT LIMITED TO THE WARRANTIES OF MERCHANTABILITY,
// FITNESS FOR A PARTICULAR PURPOSE AND NONINFRINGEMENT. IN NO EVENT SHALL THE
// AUTHORS OR COPYRIGHT HOLDERS BE LIABLE FOR ANY CLAIM, DAMAGES OR OTHER
// LIABILITY, WHETHER IN AN ACTION OF CONTRACT, TORT OR OTHERWISE, ARISING FROM,
// OUT OF OR IN CONNECTION WITH THE SOFTWARE OR THE USE OR OTHER DEALINGS IN
// THE SOFTWARE.
*/

#define _GNU_SOURCE 1

#include <stdlib.h>
#include <stdio.h>
#include <stdint.h>
#include <string.h>

#ifndef LIBUSB_DETACH
/* detach the device from crappy kernel drivers */
#define LIBUSB_DETACH 1
#endif

#if USE_LIBUSB
#include <libusb-1.0/libusb.h>
#else
#include <termios.h>
#include <fcntl.h>
#include <poll.h>
#endif
#include <unistd.h>

static void print_mem(FILE *f, const uint8_t *buf, size_t len) {
	size_t i; int a, j, n;
	for (i = 0; i < len; i += 16) {
		n = len - i;
		if (n > 16) n = 16;
		for (j = 0; j < n; j++) fprintf(f, "%02x ", buf[i + j]);
		for (; j < 16; j++) fprintf(f, "   ");
		fprintf(f, " |");
		for (j = 0; j < n; j++) {
			a = buf[i + j];
			fprintf(f, "%c", a > 0x20 && a < 0x7f ? a : '.');
		}
		fprintf(f, "|\n");
	}
}

static void print_string(FILE *f, uint8_t *buf, size_t n) {
	size_t i; int a, b = 0;
	fprintf(f, "\"");
	for (i = 0; i < n; i++) {
		a = buf[i]; b = 0;
		switch (a) {
		case '"': case '\\': b = a; break;
		case 0: b = '0'; break;
		case '\b': b = 'b'; break;
		case '\t': b = 't'; break;
		case '\n': b = 'n'; break;
		case '\f': b = 'f'; break;
		case '\r': b = 'r'; break;
		}
		if (b) fprintf(f, "\\%c", b);
		else if (a >= 32 && a < 127) fprintf(f, "%c", a);
		else fprintf(f, "\\x%02x", a);
	}
	fprintf(f, "\"\n");
}

#define ERR_EXIT(...) \
	do { fprintf(stderr, __VA_ARGS__); exit(1); } while (0)

#define DBG_LOG(...) fprintf(stderr, __VA_ARGS__)

#define RECV_BUF_LEN 1024
#define TEMP_BUF_LEN (64 << 10)

typedef struct {
	uint8_t *recv_buf, *buf;
#if USE_LIBUSB
	libusb_device_handle *dev_handle;
	int endp_in, endp_out;
#else
	int serial;
#endif
	int flags, recv_len, recv_pos, nread;
	int verbose, timeout;
} usbio_t;

#if USE_LIBUSB
static void find_endpoints(libusb_device_handle *dev_handle, int result[2]) {
	int endp_in = -1, endp_out = -1;
	int i, k, err;
	//struct libusb_device_descriptor desc;
	struct libusb_config_descriptor *config;
	libusb_device *device = libusb_get_device(dev_handle);
	if (!device)
		ERR_EXIT("libusb_get_device failed\n");
	//if (libusb_get_device_descriptor(device, &desc) < 0)
	//	ERR_EXIT("libusb_get_device_descriptor failed");
	err = libusb_get_config_descriptor(device, 0, &config);
	if (err < 0)
		ERR_EXIT("libusb_get_config_descriptor failed : %s\n", libusb_error_name(err));

	for (k = 0; k < config->bNumInterfaces; k++) {
		const struct libusb_interface *interface;
		const struct libusb_interface_descriptor *interface_desc;
		int claim = 0;
		interface = config->interface + k;
		if (interface->num_altsetting < 1) continue;
		interface_desc = interface->altsetting + 0;
		for (i = 0; i < interface_desc->bNumEndpoints; i++) {
			const struct libusb_endpoint_descriptor *endpoint;
			endpoint = interface_desc->endpoint + i;
			if (endpoint->bmAttributes == 2) {
				int addr = endpoint->bEndpointAddress;
				err = 0;
				if (addr & 0x80) {
					if (endp_in >= 0) ERR_EXIT("more than one endp_in\n");
					endp_in = addr;
					claim = 1;
				} else {
					if (endp_out >= 0) ERR_EXIT("more than one endp_out\n");
					endp_out = addr;
					claim = 1;
				}
			}
		}
		if (claim) {
			i = interface_desc->bInterfaceNumber;
#if LIBUSB_DETACH
			err = libusb_kernel_driver_active(dev_handle, i);
			if (err > 0) {
				DBG_LOG("kernel driver is active, trying to detach\n");
				err = libusb_detach_kernel_driver(dev_handle, i);
				if (err < 0)
					ERR_EXIT("libusb_detach_kernel_driver failed : %s\n", libusb_error_name(err));
			}
#endif
			err = libusb_claim_interface(dev_handle, i);
			if (err < 0)
				ERR_EXIT("libusb_claim_interface failed : %s\n", libusb_error_name(err));
			break;
		}
	}
	if (endp_in < 0) ERR_EXIT("endp_in not found\n");
	if (endp_out < 0) ERR_EXIT("endp_out not found\n");
	libusb_free_config_descriptor(config);

	//DBG_LOG("USB endp_in=%02x, endp_out=%02x\n", endp_in, endp_out);

	result[0] = endp_in;
	result[1] = endp_out;
}
#else
static void init_serial(int serial) {
	struct termios tty = { 0 };

	// B921600
	cfsetispeed(&tty, B115200);
	cfsetospeed(&tty, B115200);

	tty.c_cflag = CS8 | CLOCAL | CREAD;
	tty.c_iflag = IGNPAR;
	tty.c_oflag = 0;
	tty.c_lflag = 0;

	tty.c_cc[VMIN] = 1;
	tty.c_cc[VTIME] = 0;

	tcflush(serial, TCIFLUSH);
	tcsetattr(serial, TCSANOW, &tty);
}
#endif

#if USE_LIBUSB
static usbio_t* usbio_init(libusb_device_handle *dev_handle, int flags) {
#else
static usbio_t* usbio_init(int serial, int flags) {
#endif
	uint8_t *p; usbio_t *io;

#if USE_LIBUSB
	int endpoints[2];
	find_endpoints(dev_handle, endpoints);
#else
	init_serial(serial);
	// fcntl(serial, F_SETFL, FNDELAY);
	tcflush(serial, TCIOFLUSH);
#endif

	p = (uint8_t*)malloc(sizeof(usbio_t) + RECV_BUF_LEN + TEMP_BUF_LEN);
	io = (usbio_t*)p; p += sizeof(usbio_t);
	if (!p) ERR_EXIT("malloc failed\n");
	io->flags = flags;
#if USE_LIBUSB
	io->dev_handle = dev_handle;
	io->endp_in = endpoints[0];
	io->endp_out = endpoints[1];
#else
	io->serial = serial;
#endif
	io->recv_len = 0;
	io->recv_pos = 0;
	io->recv_buf = p; p += RECV_BUF_LEN;
	io->buf = p;
	io->verbose = 0;
	io->timeout = 1000;
	return io;
}

static void usbio_free(usbio_t* io) {
	if (!io) return;
#if USE_LIBUSB
	libusb_close(io->dev_handle);
#else
	close(io->serial);
#endif
	free(io);
}

#define WRITE16_BE(p, a) do { \
  uint32_t __tmp = a; \
	((uint8_t*)(p))[0] = (uint8_t)(__tmp >> 8); \
	((uint8_t*)(p))[1] = (uint8_t)(a); \
} while (0)

#define WRITE32_LE(p, a) do { \
  uint32_t __tmp = a; \
	((uint8_t*)(p))[0] = (uint8_t)__tmp; \
	((uint8_t*)(p))[1] = (uint8_t)(__tmp >> 8); \
	((uint8_t*)(p))[2] = (uint8_t)(__tmp >> 16); \
	((uint8_t*)(p))[3] = (uint8_t)(__tmp >> 24); \
} while (0)

#define READ16_LE(p) ( \
	((uint8_t*)(p))[1] << 8 | \
	((uint8_t*)(p))[0])

#define READ32_LE(p) ( \
	((uint8_t*)(p))[3] << 24 | \
	((uint8_t*)(p))[2] << 16 | \
	((uint8_t*)(p))[1] << 8 | \
	((uint8_t*)(p))[0])

static int usb_send(usbio_t *io, const void *data, int len) {
	const uint8_t *buf = (const uint8_t*)data;
	int ret;

	if (!buf) buf = io->buf;
	if (!len) ERR_EXIT("empty message\n");
	if (io->verbose >= 2) {
		DBG_LOG("send (%d):\n", len);
		print_mem(stderr, buf, len);
	}

#if USE_LIBUSB
	{
		int err = libusb_bulk_transfer(io->dev_handle,
				io->endp_out, (uint8_t*)buf, len, &ret, io->timeout);
		if (err < 0)
			ERR_EXIT("usb_send failed : %s\n", libusb_error_name(err));
	}
#else
	ret = write(io->serial, buf, len);
#endif
	if (ret != len)
		ERR_EXIT("usb_send failed (%d / %d)\n", ret, len);

#if !USE_LIBUSB
	tcdrain(io->serial);
	// usleep(1000);
#endif
	return ret;
}

static int usb_recv(usbio_t *io, int plen) {
	int a, pos, len, nread = 0;
	if (plen > TEMP_BUF_LEN)
		ERR_EXIT("target length too long\n");

	len = io->recv_len;
	pos = io->recv_pos;
	while (nread < plen) {
		if (pos >= len) {
#if USE_LIBUSB
			int err = libusb_bulk_transfer(io->dev_handle, io->endp_in, io->recv_buf, RECV_BUF_LEN, &len, io->timeout);
			if (err == LIBUSB_ERROR_NO_DEVICE)
				ERR_EXIT("connection closed\n");
			else if (err == LIBUSB_ERROR_TIMEOUT) break;
			else if (err < 0)
				ERR_EXIT("usb_recv failed : %s\n", libusb_error_name(err));
#else
			if (io->timeout >= 0) {
				struct pollfd fds = { 0 };
				fds.fd = io->serial;
				fds.events = POLLIN;
				a = poll(&fds, 1, io->timeout);
				if (a < 0) ERR_EXIT("poll failed, ret = %d\n", a);
				if (fds.revents & POLLHUP)
					ERR_EXIT("connection closed\n");
				if (!a) break;
			}
			len = read(io->serial, io->recv_buf, RECV_BUF_LEN);
#endif
			if (len < 0)
				ERR_EXIT("usb_recv failed, ret = %d\n", len);

			if (io->verbose >= 2) {
				DBG_LOG("recv (%d):\n", len);
				print_mem(stderr, io->recv_buf, len);
			}
			pos = 0;
			if (!len) break;
		}
		a = io->recv_buf[pos++];
		io->buf[nread++] = a;
	}
	io->recv_len = len;
	io->recv_pos = pos;
	io->nread = nread;
	return nread;
}

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

#define USBC_SIG 0x43425355
#define USBS_SIG 0x53425355
#define USBC_LEN 31
#define USBS_LEN 13

#define CMD_INQUIRY 0x12

enum {
	CMD_ADFU_FLASH = 0x10,
	CMD_ADFU_FLASH32 = 0x60,
	CMD_ADFU_WRITERAM = 0x13,
	CMD_ADFU_READRAM = 0x93,
	CMD_ADFU_SWITCH = 0x20,
	CMD_ADFU_EXEC = 0x21,
	CMD_ADFU_RETSIZE = 0x22, // addr = (uint8_t*)ret + 4
	CMD_ADFU_READRET = 0x23, // addr = *(uint32_t*)ret
};

typedef struct {
	uint32_t sig;
	uint32_t tag;
	uint32_t data_len;
	uint8_t	flags;
	uint8_t	lun;
	uint8_t	cdb_len;
	uint8_t	cdb[16];
} usbc_cmd_t;

typedef struct {
	uint32_t sig;
	uint32_t tag;
	uint32_t residue;
	uint8_t	status;
} usbs_cmd_t;

static uint32_t scsi_tag = 1;

static int check_usbs(usbio_t *io, void *ptr) {
	usbs_cmd_t *usbs = (usbs_cmd_t*)(ptr ? ptr : io->buf);
	do {
		if (!ptr && usb_recv(io, USBS_LEN) != USBS_LEN) break;
		if (READ32_LE(&usbs->sig) != USBS_SIG) break;
		if (READ32_LE(&usbs->tag) != (int)scsi_tag++) break;
		return 0;
	} while (0);
	DBG_LOG("unexpected status\n");
	return 1;
}

static void actions_cmd(usbio_t *io, int cmd,
		uint32_t len, uint32_t addr, int recv, int data_len) {
	usbc_cmd_t usbc;
	scsi_tag = 0; // important
	WRITE32_LE(&usbc.sig, USBC_SIG);
	WRITE32_LE(&usbc.tag, scsi_tag);
	WRITE32_LE(&usbc.data_len, data_len);
	usbc.flags = recv << 7;
	usbc.lun = 0;
	usbc.cdb_len = 16;
	memset(usbc.cdb, 0, 16);
	usbc.cdb[0] = 0xcd;
	WRITE32_LE(usbc.cdb + 1, cmd);
	WRITE32_LE(usbc.cdb + 5, len);
	WRITE32_LE(usbc.cdb + 9, addr);
	usb_send(io, &usbc, USBC_LEN);
}

static void write_mem_buf(usbio_t *io,
		uint32_t addr, unsigned size, const void *mem, unsigned step) {
	uint32_t i, n;

	for (i = 0; i < size; i += n) {
		n = size - i;
		if (n > step) n = step;
		actions_cmd(io, CMD_ADFU_WRITERAM, n, addr + i, 0, n);
		usb_send(io, (uint8_t*)mem + i, n);
		if (check_usbs(io, NULL))
			ERR_EXIT("write_mem failed\n");
	}
}

static void write_mem(usbio_t *io,
		uint32_t addr, unsigned src_offs, unsigned src_size,
		const char *fn, unsigned step) {
	uint8_t *mem; size_t size = 0;
	mem = loadfile(fn, &size);
	if (!mem) ERR_EXIT("loadfile(\"%s\") failed\n", fn);
	if (size >> 32) ERR_EXIT("file too big\n");
	if (size < src_offs)
		ERR_EXIT("data outside the file\n");
	size -= src_offs;
	if (src_size) {
		if (size < src_size)
			ERR_EXIT("data outside the file\n");
		size = src_size;
	}
	write_mem_buf(io, addr, size, mem + src_offs, step);
	free(mem);
}

static unsigned dump_mem2(usbio_t *io,
		uint32_t addr, uint32_t size, const char *fn, unsigned step) {
	unsigned i, n, n2;
	uint32_t code_addr = 0xbfc1e000 + 1;
	const uint32_t buf_addr = 0xbfc1e020;
	static const uint16_t code[] = {
		0xb207, /* lw $v0, 2f */
		0x9a80, /* lw $a0, 0($v0) # addr */
		0x9aa1, /* lw $a1, 4($v0) # num */
		0xf000, 0x42c8, /* addiu $a2, $v0, 8 # buf */
		0xe595, /* addu $a1, $a0 # end */
		0xdac0, /* sw $a2, 0($v0) */
		/* 1: */
		0xa460, /* lbu $v1, 0($a0) */
		0x4c01, /* addiu $a0, 1 */
		0xc660, /* sb $v1, 0($a2) */
		0x4e01, /* addiu $a0, 1 */
		0xecaa, /* cmp $a0, $a1 */
		0x61fa, /* btnez 1b */
		0xe8a0, /* jrc $ra */
		/* 2: */
		(buf_addr & 0xffff), buf_addr >> 16,
	};

	FILE *fo = fopen(fn, "wb");
	if (!fo) ERR_EXIT("fopen(wb) failed\n");

	if (step > 0x200 - 0x28) step = 0x200 - 0x28;

	for (i = 0; i < size; i += n) {
		n = size - i;
		if (n > step) n = step;

		if (!i) {
			actions_cmd(io, CMD_ADFU_WRITERAM, sizeof(code), code_addr & ~1, 0, sizeof(code));
			usb_send(io, code, sizeof(code));
			if (check_usbs(io, NULL)) break;
		}
		{
			uint32_t buf[2] = { addr + i, n };
			actions_cmd(io, CMD_ADFU_WRITERAM, 8, buf_addr, 0, 8);
			usb_send(io, buf, 8);
			if (check_usbs(io, NULL)) break;
		}
		actions_cmd(io, CMD_ADFU_EXEC, 0, code_addr, 0, 0);
		if (check_usbs(io, NULL)) break;
		actions_cmd(io, CMD_ADFU_READRET, n, 0, 1, n);

		n2 = n + USBS_LEN;
		if (usb_recv(io, n2) != (int)n2) {
			ERR_EXIT("unexpected length\n");
			break;
		}
		if (check_usbs(io, io->buf + n)) break;
		if (fwrite(io->buf, 1, n, fo) != n)
			ERR_EXIT("fwrite failed\n");
	}
	DBG_LOG("dump_mem: 0x%08x, target: 0x%x, read: 0x%x\n", addr, size, i);
	fclose(fo);
	return i;
}

static void read_mem_buf(usbio_t *io,
		uint32_t addr, unsigned size, void *mem, unsigned step) {
	uint32_t i, n, n2;

	for (i = 0; i < size; i += n) {
		n = size - i;
		if (n > step) n = step;
		actions_cmd(io, CMD_ADFU_READRAM, n, addr + i, 1, n);
		n2 = n + USBS_LEN;
		if (usb_recv(io, n2) != (int)n2) {
			ERR_EXIT("unexpected length\n");
			break;
		}
		if (check_usbs(io, io->buf + n))
			ERR_EXIT("read_mem failed\n");
		memcpy((uint8_t*)mem + i, io->buf, n);
	}
}

static unsigned dump_mem(usbio_t *io,
		uint32_t addr, uint32_t size, const char *fn, unsigned step) {
	unsigned i, n, n2;

	FILE *fo = fopen(fn, "wb");
	if (!fo) ERR_EXIT("fopen(wb) failed\n");

	for (i = 0; i < size; i += n) {
		n = size - i;
		if (n > step) n = step;
		actions_cmd(io, CMD_ADFU_READRAM, n, addr + i, 1, n);
		n2 = n + USBS_LEN;
		if (usb_recv(io, n2) != (int)n2) {
			ERR_EXIT("unexpected length\n");
			break;
		}
		if (check_usbs(io, io->buf + n)) break;
		if (fwrite(io->buf, 1, n, fo) != n)
			ERR_EXIT("fwrite failed\n");
	}
	DBG_LOG("dump_mem: 0x%08x, target: 0x%x, read: 0x%x\n", addr, size, i);
	fclose(fo);
	return i;
}

static unsigned dump_lfi(usbio_t *io,
		uint32_t addr, uint32_t size, const char *fn, unsigned step) {
	unsigned i, n0, n, n2;

	FILE *fo = fopen(fn, "wb");
	if (!fo) ERR_EXIT("fopen(wb) failed\n");

	step >>= 9;
	if (!step) step = 1;

	for (i = 0; i < size; i += n0) {
		n0 = size - i;
		if (n0 > step) n0 = step;
		n = n0 << 9;
		actions_cmd(io, CMD_ADFU_FLASH, n0, (addr + i) | 0x80 << 24, 1, n);
		n2 = n + USBS_LEN;
		if (usb_recv(io, n2) != (int)n2) {
			ERR_EXIT("unexpected length\n");
			break;
		}
		if (check_usbs(io, io->buf + n)) break;
		if (fwrite(io->buf, 1, n, fo) != n)
			ERR_EXIT("fwrite failed\n");
	}
	DBG_LOG("dump_lfi: 0x%08llx, target: 0x%llx, read: 0x%llx\n",
			(long long)addr << 9, (long long)size << 9, (long long)i << 9);
	fclose(fo);
	return i;
}

static void write_flash(usbio_t *io,
		uint32_t addr, unsigned src_offs, unsigned src_size,
		const char *fn, unsigned step) {
	uint8_t *mem; size_t size = 0;
	unsigned i, n0, n;
	mem = loadfile(fn, &size);
	if (!mem) ERR_EXIT("loadfile(\"%s\") failed\n", fn);
	if (size >> 32) ERR_EXIT("file too big\n");
	if (size < src_offs)
		ERR_EXIT("data outside the file\n");
	size -= src_offs;
	if (src_size) {
		if (size < src_size)
			ERR_EXIT("data outside the file\n");
		size = src_size;
	}

	if (size & 0x1ff)
		ERR_EXIT("must be aligned by 512\n");
	size >>= 9;
	step >>= 9;
	if (!step) step = 1;

	for (i = 0; i < size; i += n0) {
		n0 = size - i;
		if (n0 > step) n0 = step;
		n = n0 << 9;
		actions_cmd(io, CMD_ADFU_FLASH, n0, addr + i, 0, n);
		usb_send(io, mem + (i << 9), n);
		if (check_usbs(io, NULL))
			ERR_EXIT("write_flash failed\n");
	}
	free(mem);
}

static void adfu_switch(usbio_t *io, uint32_t addr) {
	actions_cmd(io, CMD_ADFU_SWITCH, 0, addr, 0, 0);
	if (check_usbs(io, NULL))
		ERR_EXIT("switch failed\n");
	usleep(10000); // 10ms timeout for proper init
}

static unsigned adfu_checksum(const void *addr, unsigned n) {
	const uint16_t *p = addr;
	unsigned i, sum = 0;
	for (i = 1; i < n >> 1; i++) sum += p[i];
	return *p ^ (uint16_t)(sum + 0x1234);
}

typedef struct {
	uint32_t code_addr, buf_addr, args_addr, nand_args;
	uint8_t *mem; unsigned psize, blk_size;
} nandread_t;

static void nandread_init(usbio_t *io, nandread_t *x, const char *nandread_fn,
		const char *mbrec_fn, unsigned blk_size) {
	unsigned n, psize;
	uint8_t *mem, buf[8];

	x->code_addr = 0xbfc1e000;
	x->buf_addr = 0xbfc1a000;
	x->args_addr = 0x9fc1fff0;
	x->nand_args = 0xbfc341e0;
	x->blk_size = blk_size;

	write_mem(io, x->code_addr, 0, 0, nandread_fn, blk_size);
	WRITE32_LE(buf, 3);
	WRITE32_LE(buf + 4, x->buf_addr);
	write_mem_buf(io, x->args_addr, 8, buf, 8);

	actions_cmd(io, CMD_ADFU_EXEC, 0, x->code_addr, 0, 0);
	if (check_usbs(io, NULL)) ERR_EXIT("exec failed\n");

	read_mem_buf(io, x->args_addr - 8, 4, buf, 4);
	if (READ32_LE(buf) != 0x12345678)
		ERR_EXIT("read mbrec failed\n");

	mem = malloc(0x400 + 0x10000);
	if (!mem) ERR_EXIT("malloc failed\n");
	x->mem = mem;
	read_mem_buf(io, x->buf_addr, 0x400, mem, blk_size);

	if (mem[2] == 0xa5) n = 0x200;
	else if (mem[2] == 0x5a) n = 0x400;
	else ERR_EXIT("bad mbrec\n");

	if (mbrec_fn) {
		FILE *fo = fopen(mbrec_fn, "wb");
		if (!fo) ERR_EXIT("fopen(mbrec) failed\n");
		if (fwrite(mem, 1, n, fo) != n)
			ERR_EXIT("fwrite failed\n");
		fclose(fo);
	}

	psize = READ16_LE(mem + 0xe); // pow2, 0x200..0x4000
	if ((psize & (psize - 1)) || (psize & ~0x7e00))
		ERR_EXIT("unexpected page size (0x%x)\n", psize);
	x->psize = psize;
	if (READ16_LE(mem + 0xc) == 0)
		ERR_EXIT("invalid nand config\n");
}

static void nandread_read(usbio_t *io, nandread_t *x, uint32_t rowaddr, uint8_t *mem, uint32_t size) {
	uint8_t buf[4];
	WRITE32_LE(buf, rowaddr);
	write_mem_buf(io, x->nand_args, 4, buf, 4);
	actions_cmd(io, CMD_ADFU_EXEC, 0, x->code_addr, 0, 0); 
	if (check_usbs(io, NULL)) ERR_EXIT("exec failed\n");
	if (mem && size)
		read_mem_buf(io, x->buf_addr, size, mem, x->blk_size);
}

static void nandread_end(usbio_t *io, nandread_t *x) {
	uint8_t buf[4];
	WRITE32_LE(buf, 0x80);
	write_mem_buf(io, x->args_addr, 4, buf, 4);
	actions_cmd(io, CMD_ADFU_EXEC, 0, x->code_addr, 0, 0);
	if (check_usbs(io, NULL)) ERR_EXIT("exec failed\n");
	free(x->mem);
}

static uint32_t dump_brec(usbio_t *io, nandread_t *x,
		unsigned brec_idx, const char *brec_fn) {
	uint8_t *mem = x->mem;
	unsigned i, n, psize = x->psize;
	uint32_t fw_size = 0;
	FILE *fo;

	do {
		unsigned n2, k, last;
		uint32_t addr; uint8_t *mem2;

		if (mem[2] != 0x5a) {
			DBG_LOG("unsupported mbrec size\n");
			break;
		}
		fo = NULL;
		if (brec_fn) {
			fo = fopen(brec_fn, "wb");
			if (!fo) ERR_EXIT("fopen(brec) failed\n");
		}

		n = 0x10000 / psize;
		n2 = READ16_LE(mem + 0xc);
		if (n2 == 0xc0) n2 = 0x100;
		addr = mem[3 + brec_idx] * n2;

		mem2 = mem + 0x400; last = psize;
		for (n2 = n, i = 0; i < n2; i++) {
			k = i + 1 == n2 ? last : psize;
			nandread_read(io, x, addr, mem2, k);
			if (fo && fwrite(mem2, 1, k, fo) != k)
				ERR_EXIT("fwrite failed\n");

			if (i + 1 < n) mem2 += psize;
			else if (i + 1 == n) {
				unsigned brec_sec = READ16_LE(mem + 0x404);
				unsigned brec2_sec = READ16_LE(mem + 0x406);
				k = brec_sec << 9;
				if (k > 0x10000) k = 0x10000;
				if (adfu_checksum(mem + 0x400, k)) {
					DBG_LOG("bad brec checksum\n");
					break;
				}
				if (brec2_sec < brec_sec) {
					DBG_LOG("unexpected brec size\n");
					break;
				}
				fw_size = READ32_LE(mem + 0x408);
				DBG_LOG("firmware size = 0x%llx\n", (long long)fw_size << 9);
				if (!fo) break;
				k = brec2_sec << 9;
				last = ((k - 1) & (psize - 1)) + 1;
				n2 = (k + psize - 1) / psize;
			}
			if (!mem[0x3c0]) addr++;
			else {
				if (i >= 0x3c) {
					DBG_LOG("unexpected brec size\n");
					break;
				}
				addr += mem[0x3c5 + i] - mem[0x3c4 + i];
			}
		}
		if (fo) fclose(fo);
	} while (0);
	return fw_size;
}

static void dump_nand(usbio_t *io, nandread_t *x,
		const char *fn, unsigned start, unsigned len, int print_tags) {
	uint8_t *mem = x->mem;
	unsigned i, psize = x->psize;
	FILE *fo = NULL;

	if (fo) {
		fo = fopen(fn, "wb");
		if (!fo) ERR_EXIT("fopen(wb) failed\n");
	} else if (!print_tags) return;

	for (i = 0; i < len; i++) {
		nandread_read(io, x, start + i, fo ? mem : NULL, psize);
		if (print_tags) {
			uint8_t buf[8];
			read_mem_buf(io, x->nand_args + 12, 8, buf, 8);
			printf("0x%x: %02x %02x %02x %02x  %02x %02x %02x %02x\n", start + i,
						buf[0], buf[1], buf[2], buf[3], buf[4], buf[5], buf[6], buf[7]);
		}
		if (fo && fwrite(mem, 1, psize, fo) != psize)
			ERR_EXIT("fwrite failed\n");
	}
	if (fo) fclose(fo);
}

static void find_lfi(usbio_t *io, nandread_t *x, int brec_idx, const char *dump_fn) {
	uint8_t *mem = x->mem;
	FILE *fo;
	unsigned i, j, k, err = 0, psize = x->psize;
	unsigned fw_size, npages, npages2, nblock;
	uint32_t tab[256];

	fw_size = dump_brec(io, x, brec_idx, NULL);
	if (!fw_size) ERR_EXIT("firmware size is unknown\n");

	npages = READ16_LE(mem + 0xc);
	npages2 = npages == 0xc0 ? 0x100 : npages;
	nblock = READ16_LE(mem + 0x48e);

	memset(tab, ~0, sizeof(tab));

	// for a faster scan
	{
		uint8_t buf[4];
		uint32_t n = 1;
		if (mem[7] < 0xd) n <<= 1;
		if (mem[8] == 4) n <<= 1;
		n = (1 << n) - 1;
		WRITE32_LE(buf, n);
		write_mem_buf(io, x->nand_args + 8, 4, buf, 4);
	}

	for (i = 0; i < nblock; i++) {
		uint8_t buf[8];
		nandread_read(io, x, i * npages2, NULL, 0);
		read_mem_buf(io, x->nand_args + 12, 8, buf, 8);
		if (buf[0] != 0xff || buf[1] != 0x40) continue;
		printf("0x%x: %02x %02x %02x %02x  %02x %02x %02x %02x\n", i * npages2,
				buf[0], buf[1], buf[2], buf[3], buf[4], buf[5], buf[6], buf[7]);
		if (buf[3] | *(uint32_t*)&buf[4]) continue;
		j = buf[2];
		if (~tab[j]) err = 1;
		tab[j] = i;
	}
	if (err) {
		DBG_LOG("!!! duplicate tags found\n");
		return;
	}
	for (k = 256; k; k--) if (~tab[k - 1]) break;
	if (!k) {
		DBG_LOG("!!! no LFI nodes found\n");
		return;
	}
	for (i = 0; i < k; i++) if (!~tab[i]) break;
	if (i != k) {
		DBG_LOG("!!! LFI chain with gaps\n");
		return;
	}
	if (!dump_fn) return;

	// restore old value
	{
		uint8_t buf[4];
		uint32_t n = mem[5];
		if (n > 31) n = 31;
		n = (1u << n) - 1;
		WRITE32_LE(buf, n);
		write_mem_buf(io, x->nand_args + 8, 4, buf, 4);
	}

	fo = fopen(dump_fn, "wb");
	if (!fo) ERR_EXIT("fopen(wb) failed\n");

	for (i = 0; i < k; i++) {
		uint32_t addr = tab[i] * npages2;
		for (j = 0; j < npages; j++) {
			nandread_read(io, x, addr + j, mem, psize);
			if (fwrite(mem, 1, psize, fo) != psize)
				ERR_EXIT("fwrite failed\n");
		}
	}
	fclose(fo);
}

static uint64_t str_to_size(const char *str) {
	char *end; int shl = 0; uint64_t n;
	n = strtoull(str, &end, 0);
	if (*end) {
		if (!strcmp(end, "K")) shl = 10;
		else if (!strcmp(end, "M")) shl = 20;
		else if (!strcmp(end, "G")) shl = 30;
		else ERR_EXIT("unknown size suffix\n");
	}
	if (shl) {
		int64_t tmp = n;
		tmp >>= 63 - shl;
		if (tmp && ~tmp)
			ERR_EXIT("size overflow on multiply\n");
	}
	return n << shl;
}

// need to take control quickly
#define REOPEN_FREQ 100

static const char* fn_helper(const char *name) {
	if (!strcmp(name, "-")) return NULL;
	return name;
}

int main(int argc, char **argv) {
#if USE_LIBUSB
	libusb_device_handle *device;
#else
	int serial;
#endif
	usbio_t *io; int ret, i;
	int wait = 300 * REOPEN_FREQ;
	const char *tty = "/dev/ttyUSB0";
	int verbose = 0;
	// flashdisk = 10d6:1101, ADFU mode = 10d6:10d6
	int id_vendor = 0x10d6, id_product = 0x10d6;
	int blk_size = 0x200;

#if USE_LIBUSB
	ret = libusb_init(NULL);
	if (ret < 0)
		ERR_EXIT("libusb_init failed: %s\n", libusb_error_name(ret));
#endif

	while (argc > 1) {
		if (!strcmp(argv[1], "--tty")) {
			if (argc <= 2) ERR_EXIT("bad option\n");
			tty = argv[2];
			argc -= 2; argv += 2;
		} else if (!strcmp(argv[1], "--id")) {
			if (argc <= 2) ERR_EXIT("bad option\n");
			char *end;
			id_vendor = strtol(argv[2], &end, 16);
			if (end != argv[2] + 4 || !*end) ERR_EXIT("bad option\n");
			id_product = strtol(argv[2] + 5, &end, 16);
			if (end != argv[2] + 9) ERR_EXIT("bad option\n");
			argc -= 2; argv += 2;
		} else if (!strcmp(argv[1], "--wait")) {
			if (argc <= 2) ERR_EXIT("bad option\n");
			wait = atoi(argv[2]) * REOPEN_FREQ;
			argc -= 2; argv += 2;
		} else if (!strcmp(argv[1], "--verbose")) {
			if (argc <= 2) ERR_EXIT("bad option\n");
			verbose = atoi(argv[2]);
			argc -= 2; argv += 2;
		} else if (argv[1][0] == '-') {
			ERR_EXIT("unknown option\n");
		} else break;
	}

	for (i = 0; ; i++) {
#if USE_LIBUSB
		device = libusb_open_device_with_vid_pid(NULL, id_vendor, id_product);
		if (device) break;
		if (i >= wait)
			ERR_EXIT("libusb_open_device failed\n");
#else
		serial = open(tty, O_RDWR | O_NOCTTY | O_SYNC);
		if (serial >= 0) break;
		if (i >= wait)
			ERR_EXIT("open(ttyUSB) failed\n");
#endif
		if (!i) DBG_LOG("Waiting for connection (%ds)\n", wait / REOPEN_FREQ);
		usleep(1000000 / REOPEN_FREQ);
	}

#if USE_LIBUSB
	io = usbio_init(device, 0);
#else
	io = usbio_init(serial, 0);
#endif
	io->verbose = verbose;

	while (argc > 1) {
		
		if (!strcmp(argv[1], "verbose")) {
			if (argc <= 2) ERR_EXIT("bad command\n");
			io->verbose = atoi(argv[2]);
			argc -= 2; argv += 2;

		} else if (!strcmp(argv[1], "inquiry")) {
			usbc_cmd_t usbc; int len = 0x24;
			WRITE32_LE(&usbc.sig, USBC_SIG);
			WRITE32_LE(&usbc.tag, scsi_tag);
			WRITE32_LE(&usbc.data_len, len);
			usbc.flags = 0x80;
			usbc.lun = 0;
			usbc.cdb_len = 6;
			memset(usbc.cdb, 0, 16);
			usbc.cdb[0] = CMD_INQUIRY;
			WRITE16_BE(usbc.cdb + 3, len);
			usb_send(io, &usbc, USBC_LEN);

			if (usb_recv(io, len) != len) {
				DBG_LOG("unexpected response\n");
				break;
			}
			if (verbose < 2) {
				DBG_LOG("result (%d):\n", len);
				print_mem(stderr, io->buf, len);
			}
			if (check_usbs(io, NULL)) break;
			argc -= 1; argv += 1;

		} else if (!strcmp(argv[1], "adfu_reboot")) {
			usbc_cmd_t usbc; int len = 11;
			WRITE32_LE(&usbc.sig, USBC_SIG);
			WRITE32_LE(&usbc.tag, scsi_tag);
			WRITE32_LE(&usbc.data_len, len);
			usbc.flags = 0x80;
			usbc.lun = 0;
			usbc.cdb_len = 16;
			memset(usbc.cdb, 0, 16);
			usbc.cdb[0] = 0xcc;
			usbc.cdb[7] = len;
			usb_send(io, &usbc, USBC_LEN);
			if (usb_recv(io, len) != len ||
			  	memcmp(io->buf, "ACTIONSUSBD", len)) {
				DBG_LOG("unexpected response\n");
				break;
			}
			if (check_usbs(io, NULL)) break;

			len = 2;
			WRITE32_LE(&usbc.tag, scsi_tag);
			WRITE32_LE(&usbc.data_len, len);
			usbc.cdb[0] = 0xcb;
			usbc.cdb[1] = 0x21;
			usbc.cdb[7] = len;
			usb_send(io, &usbc, USBC_LEN);
			if (usb_recv(io, len) != len ||
					io->buf[0] != 0xff || io->buf[1]) {
				DBG_LOG("unexpected response\n");
				break;
			}
			if (check_usbs(io, NULL)) break;
			argc -= 1; argv += 1;

		} else if (!strcmp(argv[1], "rom_info")) {
			usbc_cmd_t usbc; int len = 0x12;
			scsi_tag = 0; // important
			WRITE32_LE(&usbc.sig, USBC_SIG);
			WRITE32_LE(&usbc.tag, scsi_tag);
			WRITE32_LE(&usbc.data_len, len);
			usbc.flags = 0x80;
			usbc.lun = 0;
			usbc.cdb_len = 16;
			memset(usbc.cdb, 0, 16);
			usbc.cdb[0] = 0xcc;
			usb_send(io, &usbc, USBC_LEN);

			if (usb_recv(io, len) != len) {
				DBG_LOG("unexpected response\n");
				break;
			}
			if (verbose < 2) {
				DBG_LOG("result (%d):\n", len);
				print_mem(stderr, io->buf, len);
			}
			if (check_usbs(io, NULL)) break;
			argc -= 1; argv += 1;

		} else if (!strcmp(argv[1], "write_mem")) {
			const char *fn; uint64_t addr, offset, size;
			if (argc <= 5) ERR_EXIT("bad command\n");

			addr = str_to_size(argv[2]);
			offset = str_to_size(argv[3]);
			size = str_to_size(argv[4]);
			fn = argv[5];
			if ((addr | size | offset | (addr + size)) >> 32)
				ERR_EXIT("32-bit limit reached\n");
			write_mem(io, addr, offset, size, fn, blk_size);
			argc -= 5; argv += 5;

		} else if (!strcmp(argv[1], "switch")) {
			uint64_t addr;
			if (argc <= 2) ERR_EXIT("bad command\n");

			addr = str_to_size(argv[2]);
			if (addr >> 32) ERR_EXIT("32-bit limit reached\n");
			adfu_switch(io, addr);
			blk_size = 0x4000;
			argc -= 2; argv += 2;

		} else if (!strcmp(argv[1], "simple_switch")) {
			const char *fn; uint64_t addr;
			if (argc <= 3) ERR_EXIT("bad command\n");

			addr = str_to_size(argv[2]);
			fn = argv[3];
			if (addr >> 32) ERR_EXIT("32-bit limit reached\n");
			write_mem(io, addr, 0, 0, fn, blk_size);
			adfu_switch(io, addr);
			blk_size = 0x4000;
			argc -= 3; argv += 3;

		} else if (!strcmp(argv[1], "exec_ret")) {
			uint64_t addr; int len;
			if (argc <= 2) ERR_EXIT("bad command\n");

			addr = str_to_size(argv[2]);
			len = strtol(argv[3], NULL, 0);
			if (addr >> 32) ERR_EXIT("32-bit limit reached\n");
			actions_cmd(io, CMD_ADFU_EXEC, 0, addr, 0, 0);
			if (check_usbs(io, NULL)) break;

			if (len == -1) {
				actions_cmd(io, CMD_ADFU_RETSIZE, 4, 0, 1, 4);
				if (usb_recv(io, 4) != 4) {
					DBG_LOG("unexpected response\n");
					break;
				}
				len = READ32_LE(io->buf);
				if (check_usbs(io, NULL)) break;
			}
			if (len > 0x40000)
				ERR_EXIT("requested length too long (0x%x)\n", len);
			else if (len) {
				actions_cmd(io, CMD_ADFU_READRET, len, 0, 1, len);
				if (usb_recv(io, len) != len) {
					DBG_LOG("unexpected response\n");
					break;
				}
				if (verbose < 2) {
					DBG_LOG("result (%d):\n", len);
					print_mem(stderr, io->buf, len);
				}
				if (check_usbs(io, NULL)) break;
			}
			argc -= 3; argv += 3;

		} else if (!strcmp(argv[1], "read_mem")) {
			const char *fn; uint64_t addr, size;
			if (argc <= 4) ERR_EXIT("bad command\n");

			addr = str_to_size(argv[2]);
			size = str_to_size(argv[3]);
			if ((addr | size | (addr + size)) >> 32)
				ERR_EXIT("32-bit limit reached\n");
			fn = argv[4];
			dump_mem(io, addr, size, fn, blk_size);
			argc -= 4; argv += 4;

		// the commands below are implemented only in the adfus binary
		} else if (!strcmp(argv[1], "reset")) {
			usbc_cmd_t usbc; int len = 0;
			scsi_tag = 0; // important
			WRITE32_LE(&usbc.sig, USBC_SIG);
			WRITE32_LE(&usbc.tag, scsi_tag);
			WRITE32_LE(&usbc.data_len, len);
			usbc.flags = 0;
			usbc.lun = 0;
			usbc.cdb_len = 16;
			memset(usbc.cdb, 0, 16);
			usbc.cdb[0] = 0xb0;
			usb_send(io, &usbc, USBC_LEN);
			if (check_usbs(io, NULL)) break;
			argc -= 1; argv += 1;

		} else if (!strcmp(argv[1], "read_mem2")) {
			const char *fn; uint64_t addr, size;
			if (argc <= 4) ERR_EXIT("bad command\n");

			addr = str_to_size(argv[2]);
			size = str_to_size(argv[3]);
			if ((addr | size | (addr + size)) >> 32)
				ERR_EXIT("32-bit limit reached\n");
			fn = argv[4];
			dump_mem2(io, addr, size, fn, blk_size);
			argc -= 4; argv += 4;

		// the commands below use payload/nandread.bin
		} else if (!strcmp(argv[1], "read_brec")) {
			unsigned brec_idx;
			nandread_t x;
			if (argc <= 5) ERR_EXIT("bad command\n");

			brec_idx = strtol(argv[4], NULL, 0);
			if (brec_idx >> 1)
				ERR_EXIT("brec_idx must be 0 or 1\n");

			nandread_init(io, &x, argv[2], fn_helper(argv[3]), blk_size);
			dump_brec(io, &x, brec_idx, fn_helper(argv[5]));
			nandread_end(io, &x);
			argc -= 5; argv += 5;

		} else if (!strcmp(argv[1], "read_nand")) {
			unsigned start, len;
			nandread_t x;
			if (argc <= 5) ERR_EXIT("bad command\n");
			start = strtol(argv[3], NULL, 0);
			len = strtol(argv[4], NULL, 0);

			nandread_init(io, &x, argv[2], NULL, blk_size);
			dump_nand(io, &x, fn_helper(argv[5]), start, len, 1);
			nandread_end(io, &x);
			argc -= 5; argv += 5;

		} else if (!strcmp(argv[1], "find_lfi")) {
			unsigned brec_idx;
			nandread_t x;
			if (argc <= 4) ERR_EXIT("bad command\n");

			brec_idx = strtol(argv[3], NULL, 0);
			if (brec_idx >> 1)
				ERR_EXIT("brec_idx must be 0 or 1\n");

			nandread_init(io, &x, argv[2], NULL, blk_size);
			find_lfi(io, &x, brec_idx, fn_helper(argv[4]));
			nandread_end(io, &x);
			argc -= 4; argv += 4;

		// the commands below require loading the correct fwscfNNN.bin
		} else if (!strcmp(argv[1], "read_lfi")) {
			const char *fn; uint64_t addr, size;
			if (argc <= 4) ERR_EXIT("bad command\n");

			addr = str_to_size(argv[2]);
			size = str_to_size(argv[3]);
			if ((addr | size) & 0x1ff)
				ERR_EXIT("must be aligned by 512\n");
			addr >>= 9; size >>= 9;
			if (!size) ERR_EXIT("zero size\n");
			if ((addr | size | (addr + size - 1)) >> 24)
				ERR_EXIT("24-bit limit reached\n");
			fn = argv[4];
			dump_lfi(io, addr, size, fn, blk_size);
			argc -= 4; argv += 4;

		} else if (!strcmp(argv[1], "write_flash")) {
			const char *fn; uint64_t addr, offset, size;
			if (argc <= 5) ERR_EXIT("bad command\n");

			addr = str_to_size(argv[2]);
			offset = str_to_size(argv[3]);
			size = str_to_size(argv[4]);
			fn = argv[5];
			if ((addr | size | offset | (addr + size)) >> 32)
				ERR_EXIT("32-bit limit reached\n");
			write_flash(io, addr, offset, size, fn, blk_size);
			argc -= 5; argv += 5;

		} else if (!strcmp(argv[1], "blk_size")) {
			if (argc <= 2) ERR_EXIT("bad command\n");
			blk_size = str_to_size(argv[2]);
			blk_size = blk_size < 0 ? 1 :
					blk_size > 0x4000 ? 0x4000 : blk_size;
			argc -= 2; argv += 2;

		} else if (!strcmp(argv[1], "timeout")) {
			if (argc <= 2) ERR_EXIT("bad command\n");
			io->timeout = atoi(argv[2]);
			argc -= 2; argv += 2;

		} else {
			ERR_EXIT("unknown command\n");
		}
	}

	usbio_free(io);
#if USE_LIBUSB
	libusb_exit(NULL);
#endif
	return 0;
}
