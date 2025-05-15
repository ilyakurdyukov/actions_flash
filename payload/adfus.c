#include <stdint.h>
#include <stddef.h>

#define MEM4(addr) *(volatile uint32_t*)(addr)
#define MEM2(addr) *(volatile uint16_t*)(addr)
#define MEM1(addr) *(volatile uint8_t*)(addr)

#define DELAY(n) { \
	unsigned _count = n; \
	do __asm__ __volatile__(""); while (--_count); \
}

#define USBC_SIG 0x43425355
#define USBS_SIG 0x53425355

#define USB_BASE 0xc0080000
#define USB_REG(x) MEM4(USB_BASE + x)
#define USB_REG1(x) MEM1(USB_BASE + x)

#define DMA_BASE 0xc00c0000
#define DMA_REG(x) MEM4(DMA_BASE + x)

#define RTC_BASE 0xc0120000

extern void int_enable(void);

static uint32_t switch_addr;
static struct { void *addr; unsigned size; } *exec_result;
static uint16_t usb_blksize;
static uint8_t switch_loop;
static uint8_t switch_flag;
static char usbs_error;
static uint32_t scsi_tag;

// watchdog
static void wd_clear(void) {
	MEM4(RTC_BASE + 0x1c) |= 1;
}

static void init_chip(void) {
	uint32_t tmp;
	MEM4(0xc0000104) |= 1;
	MEM4(0xc0000100) = 0x46;
	MEM4(0xc0000004) = 3;
	MEM4(0xc0000008) = 0xc41;
	MEM4(0xc000002c) = ~0;
	MEM4(0xc0000030) = 0x880000;
	MEM4(0xc002004c) = 0xe0;
	DELAY(0x100 * 4)
	(void)MEM4(0xc002002c); // useless read?
	MEM4(0xc002002c) = 0;
	tmp = MEM4(0xc002002c);
	MEM4(0xc002002c) = 0x80;
	MEM4(0xc002002c) |= tmp;
	DELAY(0x100 * 4)
}

static void init_usb(void) {
	USB_REG1(0x418) = 0xf;
	USB_REG(0x344) = 0x200;
	USB_REG(0x308) = 0x800;

	USB_REG1(0x400) = 0xf0;
	USB_REG1(0x18c) = 0xff;

	USB_REG1(0x198) = 1;
	USB_REG1(0x400) = 9;
	MEM4(0xc00b0004) = 0x400;
	USB_REG1(0x1a3) = 0;

	usb_blksize = USB_REG1(0x1e3) << 8 | USB_REG1(0x1e2);
}

__attribute__((noreturn))
static void cmd_reset(void) {
	USB_REG1(0x400) = 0xf0;
	USB_REG1(0x18c) = 0xff;

	USB_REG1(0x1a3) = 0x40;
	USB_REG1(0x418) &= 0xfa;
	for (;;);
}

void entry_main(void) {
	wd_clear();
	init_chip();

	switch_loop = 0x55;
	switch_addr = 0;
	switch_flag = 0;
	exec_result = NULL;
	usbs_error = 0;

	init_usb();
#ifdef __mips16
	int_enable();
#else
	__asm__ __volatile__("ei");
#endif
	do {
		wd_clear();
		if (switch_flag == 1) {
			((void (*)(void))switch_addr)();
			switch_flag = 0;
		}
	} while (switch_loop != 0x77);
	cmd_reset();
}

static void usb_write_end(void) {
	USB_REG1(0x400) |= 0x80; // usbeirq.usbirq = 1
	USB_REG1(0x403) |= 0x40; // oshrtpack.ir2
}

static void usb_read_end(void) {
	USB_REG1(0x13) |= 2; // out2cs_hcin2cs.busy_hcbusy = 1
	USB_REG1(0x400) |= 0x80;
	USB_REG1(0x403) |= 0x40;
}

static void ret_usbs(void) {
	USB_REG(0x84) = USBS_SIG;
	USB_REG(0x84) = scsi_tag; // the original code returns 0
	USB_REG(0x84) = 0;
	USB_REG1(0x84) = usbs_error;

	USB_REG1(0xf) |= 2;
	while ((USB_REG1(0xf) & 0xe) != 8) wd_clear();
}

static void usb_recv_buf(void *addr, uint32_t len) {
#if 0
	uint8_t *p = addr;
	while (len--) *p++ = USB_REG1(0x88);
#else
	USB_REG1(0x40c) = 5;
	MEM2(USB_BASE + 0x41c) = len - 1;
	DMA_REG(0x14) = USB_BASE + 0x88;
	DMA_REG(0x20) = (uint32_t)addr;
	DMA_REG(0x28) = len;

	USB_REG1(0x40a) = 1;
	DMA_REG(0x10) = 0x41;
	while (DMA_REG(0x10) & 1) wd_clear();
#endif
}

static void usb_send_buf1(const void *addr, uint32_t len, int extra) {
#if 0
	const uint8_t *p = addr;
	while (len--) USB_REG1(0x84) = *p++;
	if (extra) USB_REG1(0xf) |= 2;
	while ((USB_REG1(0xf) & 0xe) != 8) wd_clear();
#else
	USB_REG1(0x40c) = 2;
	MEM2(USB_BASE + 0x41c) = len - 1;
	DMA_REG(0x14) = (uint32_t)addr;
	DMA_REG(0x20) = USB_BASE + 0x84;
	DMA_REG(0x28) = len;

	USB_REG1(0x40a) = 1;
	DMA_REG(0x10) = 0x401;
	while (DMA_REG(0x10) & 1) wd_clear();
	while (USB_REG1(0x40a) & 1) wd_clear();
	if (extra) USB_REG1(0xf) = 2;
	while ((USB_REG1(0xf) & 0xe) != 8) wd_clear();
#endif
}

static void usb_send_buf(const void *addr, uint32_t len) {
	uint32_t rem = len % usb_blksize;
	if (len -= rem) usb_send_buf1(addr, len, 0);
	if (rem) usb_send_buf1((char*)addr + len, rem, 1);
}

#define DEF_CONST_FN(addr, ret, name, args) \
	static ret (* const name) args = (ret (*) args)(addr);

DEF_CONST_FN(0xbfc1e400, void, flash_fn, (int type, uint32_t sector, uint32_t len, void *buf))

static void cmd_flash(uint32_t cmd, uint32_t addr, uint32_t len) {
	uint32_t n;
	void *buf_addr = (void*)0xbfc1a000;

	if (cmd == 0xff) flash_fn(cmd, 0, 0, 0);
	for (; len; addr += n, len -= n) {
		n = len;
		if (n > 32) n = 32;
		if (cmd == 0x80) {
			flash_fn(cmd, addr, n, buf_addr);
			usb_send_buf(buf_addr, n << 9);
		} else {
			usb_recv_buf(buf_addr, n << 9);
			if (cmd != 0xff)
				flash_fn(cmd, addr, n, buf_addr);
		}
	}
}

static void cmd_vendor(void) {
	uint32_t cmd, len, addr;
	cmd = USB_REG(0x88);
	len = USB_REG(0x88);
	addr = USB_REG(0x88);
	usb_read_end();

	switch (cmd & 0x7f) {
	case 0x10:
		cmd_flash(addr >> 24, addr & 0xffffff, len);
		break;
	case 0x13:
		if (cmd & 0x80) usb_send_buf((void*)addr, len);
		else usb_recv_buf((void*)addr, len);
		break;
	case 0x20:
		switch_addr = addr;
		switch_flag = 1;
		break;
	case 0x21:
		wd_clear();
		exec_result = ((void* (*)(void))addr)();
		break;
	case 0x22: usb_send_buf(&exec_result->size, len); break;
	case 0x23: usb_send_buf(exec_result->addr, len); break;
	case 0x60:
		cmd_flash((cmd >> 8 & 0xff) | 0xc0, addr, len);
		break;
	default:
		usbs_error = 2;
		ret_usbs();
		return;
	}
	ret_usbs();
	usb_write_end();
}

static void parse_usb_cmd(void) {
	uint32_t cmd;
	if (usbs_error) return;
	cmd = USB_REG(0x88);
	if (cmd != USBC_SIG) {
		usbs_error = 2;
		return;
	}
	scsi_tag = USB_REG(0x88);
	(void)USB_REG(0x88); // data_len
	cmd = USB_REG(0x88) >> 24;

	switch (cmd) {
	// case 0xcc: break;
	case 0xcd: cmd_vendor(); break;
	case 0xb0:
		ret_usbs();
		cmd_reset();
		break;
	default:
		usbs_error = 2;
	}
}

void int_main(void) {
	wd_clear();
	if (USB_REG1(0x403) & 0x40) {
		parse_usb_cmd();
		return;
	}
	if (!(USB_REG1(0x18c) & 1) &&
			!(USB_REG1(0x18c) & 0x10) &&
			(USB_REG1(0x400) & 0x10)) {
		USB_REG1(0x400) = 0x99;
		switch_loop = 0x77;
	}
}
