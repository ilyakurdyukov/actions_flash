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

#define USB_BASE 0xc0140000
#define USB_REG(x) MEM4(USB_BASE + x)

#define RTC_BASE 0xc0030000
#define PMU_BASE 0xc0010000

static volatile uint32_t switch_addr;
static struct { void *addr; unsigned size; } *exec_result;
static uint8_t blk_size;
static volatile uint8_t fwsc_flag;
static volatile uint8_t switch_loop;
static volatile uint8_t switch_flag;
static char usbs_error;

typedef void (*handler_t)(void);
static handler_t vec_table[0x3a] __attribute__((aligned(0x100)));
static uint8_t usb_buf[0x1f];

// watchdog
static void wd_clear(void) {
	MEM4(RTC_BASE + 0x1c) |= 1;
}

static void error_vec(void) {
	(void)MEM4(0xe000ed04); // ICSR
	for (;;);
}

static void int_main(void);

void entry_main(void) {
	wd_clear();
	{
		unsigned i;
		for (i = 0; i < 0x3a; i++) vec_table[i] = error_vec;
	}
	USB_REG(0x130) |= 1;
	vec_table[0x1c] = int_main;
	MEM4(0xe000ed24) |= 0x40000; // SHCSR
	MEM4(0xe000ed08) = (uint32_t)vec_table; // VTOR

	blk_size = 0x20;
	fwsc_flag = 0;
	switch_loop = 0x55;
	switch_addr = 0;
	switch_flag = 0;
	exec_result = NULL;
	usbs_error = 0;

	MEM4(0xe000e100) = 0x1000; // usb int enable

	do {
		wd_clear();
		if (switch_flag == 1) {
			((void (*)(void))switch_addr)();
			switch_flag = 0;
		}
	} while (switch_loop != 0x77);

	MEM4(0xe000e100) = 0; // usb int disable

	{
		uint32_t tmp = MEM4(RTC_BASE + 0x1c);
		if (fwsc_flag == 1) {
			MEM4(RTC_BASE + 0x1c) = (tmp & ~0x2e) | 0x11;
		} else if (!(tmp & 0x10)) {
			while (MEM4(PMU_BASE + 0x28) & 1 << 30);
			MEM4(PMU_BASE + 0x20) &= ~2;
			DELAY(0x8000)
			MEM4(PMU_BASE + 0x20) &= ~1;
		}
	}
	for (;;);
}

static void usb_wait(int a1) {
	uint32_t t1 = 2 << a1, t2;
	while (!(USB_REG(0x328) & t1)) {
		t2 = USB_REG(0x328) & t1 << 16;
		if (t2) {
			USB_REG(0x330) = 0;
			USB_REG(0x308 + a1 * 8) = 0;
			USB_REG(0x328) = t2;
			wd_clear();
			for (;;);
		}
	}
	USB_REG(0x328) = t1;
	wd_clear();
}

static int usb_send_recv(void *p0, int a1, unsigned len, int a3) {
	unsigned n; char *p = p0;
	while (len) {
		n = len;
		if (n > 0x10000) n = 0x10000;
		len -= n;
		USB_REG(0x308 + a1 * 8) = n << 8 | a3 << 1;
		USB_REG(0x30c + a1 * 8) = (uint32_t)p; p += n;
		USB_REG(0x330) = 1;
		USB_REG(0x308 + a1 * 8) |= 1;
		usb_wait(a1);
	}
	return 0;
}

#define usb_send_buf(addr, len) usb_send_recv(addr, 0, len, 1)
#define usb_recv_buf(addr, len) usb_send_recv(addr, 2, len, 0)

static void ret_usbs(void) {
	usb_buf[3] = USBS_SIG >> 24;
	*(uint32_t*)(usb_buf + 8) = 0;
	usb_buf[12] = usbs_error;
	usb_send_buf(usb_buf, 13);
}

#define DEF_CONST_FN(addr, ret, name, args) \
	static ret (* const name) args = (ret (*) args)(addr | 1);

DEF_CONST_FN(0x11e400, void, flash_fn, (int type, uint32_t sector, uint32_t len, void *buf))

static void cmd_flash(uint32_t cmd, uint32_t addr, uint32_t len) {
	uint32_t n;
	void *buf_addr = (void*)0x11a000;

	if (cmd == 0xff) flash_fn(cmd, 0, 0, 0);
	for (; len; addr += n, len -= n) {
		n = len;
		if (n > blk_size) n = blk_size;
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
	uint32_t cmd = *(uint32_t*)&usb_buf[0x10];
	uint32_t len = *(uint32_t*)&usb_buf[0x14];
	uint32_t addr = *(uint32_t*)&usb_buf[0x18];

	switch (cmd & 0x7f) {
	case 0x10:
		cmd_flash(addr >> 24, addr & 0xffffff, len);
		break;
	case 0x13:
		if (cmd & 0x80) usb_send_buf((void*)addr, len);
		else usb_recv_buf((void*)addr, len);
		break;
	case 0x20:
		switch_addr = addr | 1;
		switch_flag = 1;
		break;
	case 0x21:
		wd_clear();
		{
			void *r; char *p;
			exec_result = r = ((void* (*)(void))(addr | 1))();
			p = *(char**)r;
			if (p[6] == 'N') {
				uint32_t x = *(uint32_t*)&p[0x58];
				if (x - 1 < 32) // sanity check (not present in original code)
					blk_size = x;
			}
		}
		break;
	case 0x22: usb_send_buf(&exec_result->size, len); break;
	case 0x23:
		{
			char *p = exec_result->addr;
			if (p[8] == 'F' && p[9] == 'W' && p[10] == 'S' && p[11] == 'C')
				fwsc_flag = p[12];
			usb_send_buf(p, len);
		}
		break;
	case 0x60:
		cmd_flash((cmd >> 8 & 0xff) | 0xc0, addr, len);
		break;
	default:
		usbs_error = 2;
	}
	ret_usbs();
}

static void parse_usb_cmd(void) {
	if (usbs_error) {
		switch_loop = 0x77;
		return;
	}
	wd_clear();
	if (USB_REG(0x1b8) != 0x1f ||
			usb_recv_buf(&usb_buf, 0x1f) ||
			*(uint32_t*)usb_buf != USBC_SIG) {
		usbs_error = 2;
		return;
	}
	switch (usb_buf[15]) {
	// case 0xcc: break;
	case 0xcd: cmd_vendor(); break;
	case 0xb0:
		ret_usbs();
		switch_loop = 0x77;
		break;
	default:
		usbs_error = 2;
		ret_usbs();
	}
}

static void int_main(void) {
	uint32_t t0, t1;

	wd_clear();
	t1 = USB_REG(0x140) & ~USB_REG(0x130);
	if (t1 & 4) {
		t0 = USB_REG(0x14c) & ~USB_REG(0x13c);
		if (t0 & 1) USB_REG(0x14c) = 1;
		if (t0 & 2) {
			switch_loop = 0x77;
			USB_REG(0x14c) = 2;
		}
		if (t0 & 4) USB_REG(0x14c) = 4;
	}
	if (t1 & 2) {
		t0 = USB_REG(0x148) & ~USB_REG(0x138);
		if (t0 & 0x20) parse_usb_cmd();
	}
}
