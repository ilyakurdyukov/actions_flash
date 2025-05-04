#include <stdint.h>
#include <stddef.h>

void* entry_main(void) {
	uint32_t *p = (void*)0x9fc24000;
	p[0] = (uint32_t)"Hello, World!";
	p[1] = 14;
	return p;
}

