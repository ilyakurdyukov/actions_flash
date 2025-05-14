OBJDIR = obj
NAME = adfus
SRCS = start $(NAME)
OBJS = $(SRCS:%=$(OBJDIR)/%.o)
LDSCRIPT = simple.ld

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

CFLAGS += -Wall -Wextra -std=c99 -pedantic
CFLAGS += -fPIE -ffreestanding -march=armv7-m -mthumb $(EXTRA_CFLAGS) -fno-strict-aliasing
LFLAGS = -nostartfiles -nodefaultlibs -nostdlib -Wl,-T,$(LDSCRIPT) -Wl,-z,notext

ifeq ($(NAME), adfus)
LFLAGS += -Wl,--defsym,IMAGE_START=0x118000
else
LFLAGS += -Wl,--defsym,IMAGE_START=0x11e000
endif

# Clang's LTO doesn't work with the GCC toolchain
ifeq ($(findstring -gcc-toolchain,$(notdir $(CC))),)
CFLAGS += -flto
endif

ifdef SYSROOT
CFLAGS += --sysroot="$(SYSROOT)"
endif

.PHONY: all clean

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
	$(CC) $< -c -o $@

$(OBJDIR)/%.o: %.S | $(OBJDIR)
	$(CC) $< -c -o $@

$(OBJDIR)/$(NAME).elf: $(OBJS)
	$(CC) $(LFLAGS) $(OBJS) -o $@

$(OBJDIR)/$(NAME).asm: $(OBJDIR)/$(NAME).elf
	"$(TOOLCHAIN)"-objdump -D $< > $@

%.bin: $(OBJDIR)/%.elf
	$(OBJCOPY) -O binary -R .bss $< $@

