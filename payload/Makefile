OBJDIR = obj
SRCS = start $(NAME)
OBJS = $(SRCS:%=$(OBJDIR)/%.o)
LDSCRIPT = simple.ld
NAME = adfus
MIPS = 16

ifdef TOOLCHAIN
CC = "$(TOOLCHAIN)"-gcc
OBJCOPY = "$(TOOLCHAIN)"-objcopy
endif

COMPILER = $(findstring clang,$(notdir $(CC)))
ifeq ($(COMPILER), clang)
# Clang
CFLAGS = -Oz
else
# GCC
CFLAGS = -Os
endif

ifeq ($(MIPS), 16)
MFLAGS = -mips16
else
MFLAGS = -mips32r2
endif
CFLAGS += -std=c99 -Wall -Wextra -pedantic
CFLAGS += -fno-PIE -ffreestanding -mno-abicalls $(MFLAGS) $(EXTRA_CFLAGS) -fno-strict-aliasing
LFLAGS = -nostartfiles -nodefaultlibs -nostdlib -Wl,-T,$(LDSCRIPT) -Wl,-z,notext
AFLAGS = -mno-abicalls -fno-PIE

ifeq ($(NAME), adfus)
CFLAGS += -ffunction-sections
LFLAGS += -Wl,--defsym,IMAGE_START=0xbfc18000
SRCS = adfus_asm adfus
else
LFLAGS += -Wl,--defsym,IMAGE_START=0xbfc1e000
endif

# Clang's LTO doesn't work with the GCC toolchain
ifeq ($(findstring -gcc-toolchain,$(notdir $(CC))),)
CFLAGS += -flto
LFLAGS += -march=mips32r2
endif

ifdef SYSROOT
CFLAGS += --sysroot="$(SYSROOT)"
endif

.PHONY: all clean asm

all: $(NAME).bin

clean:
	$(RM) -r $(OBJDIR) $(NAME).bin

asm: $(OBJDIR)/$(NAME).asm

$(OBJDIR):
	mkdir -p $(OBJDIR)

-include $(OBJS:.o=.d)

%.asm: %.c
	$(CC) $(CFLAGS) $< -S -o $@

$(OBJDIR)/%.o: %.c | $(OBJDIR)
	$(CC) $(CFLAGS) -MMD -MP -MF $(@:.o=.d) $< -c -o $@

$(OBJDIR)/%.o: %.s | $(OBJDIR)
	$(CC) $(AFLAGS) $< -c -o $@

$(OBJDIR)/%.o: %.S | $(OBJDIR)
	$(CC) $(AFLAGS) $< -c -o $@

$(OBJDIR)/$(NAME).elf: $(OBJS)
	$(CC) $(LFLAGS) $(OBJS) -o $@

$(OBJDIR)/$(NAME).asm: $(OBJDIR)/$(NAME).elf
	"$(TOOLCHAIN)"-objdump -D $< > $@

%.bin: $(OBJDIR)/%.elf
	$(OBJCOPY) -O binary -R .bss  $< $@

