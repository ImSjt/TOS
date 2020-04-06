TOP_DIR 	= $(shell pwd)

CC := gcc
CFLAGS := -fno-builtin -fno-PIC -Wall -ggdb -m32 -gstabs -nostdinc -fno-stack-protector -Os
CFLAGS += -I $(TOP_DIR)/include/

LD := ld
LDFLAGS := -m elf_i386 -nostdlib -N

DD := dd

OBJCOPY := objcopy
OBJCOPYFLAGS := -S

BOOT_DIR := $(TOP_DIR)/boot/
TOOLS_DIR := $(TOP_DIR)/tools/

TOS_IMG := $(TOP_DIR)/tos.img
BOOT_BLOCK := $(BOOT_DIR)/bootblock
KERNEL := $(TOP_DIR)/kern
SIGN := $(TOOLS_DIR)/sign

BOBJS := $(BOOT_DIR)/bootasm.o $(BOOT_DIR)/bootmain.o

KSRC_DIR := $(TOP_DIR)/kernel/
KSRC_ASM_FILES := $(shell find $(KSRC_DIR) -name *.S)
KSRC_C_FILES := $(shell find $(KSRC_DIR) -name *.c)
KOBJS := $(patsubst %.S,%.o,$(KSRC_ASM_FILES)) \
			$(patsubst %.c,%.o,$(KSRC_C_FILES))

LSRC_DIR := $(TOP_DIR)/libs/
LSRC_C_FILES := $(shell find $(LSRC_DIR) -name *.c)
LOBJS := $(patsubst %.c,%.o,$(LSRC_C_FILES))

.PHONY: all
all: $(TOS_IMG)

$(TOS_IMG): $(BOOT_BLOCK) $(KERNEL)
	$(DD) if=/dev/zero of=$@ count=10000
	$(DD) if=$(BOOT_BLOCK) of=$@ conv=notrunc
	$(DD) if=$(KERNEL) of=$@ seek=1 conv=notrunc

$(BOOT_BLOCK): $(SIGN) $(BOBJS)
	$(LD) $(LDFLAGS) -T $(TOOLS_DIR)/boot.ld -o $(BOOT_DIR)/bootblock.o $(BOBJS)
	$(OBJCOPY) $(OBJCOPYFLAGS) -O binary $(BOOT_DIR)/bootblock.o $(BOOT_DIR)/bootblock.out
	$(SIGN) $(BOOT_DIR)/bootblock.out $(BOOT_BLOCK)

$(SIGN): $(TOOLS_DIR)/sign.c
	$(CC) -o $(SIGN) $(TOOLS_DIR)/sign.c

$(KERNEL): $(KOBJS) $(LOBJS)
	$(LD) $(LDFLAGS) -T $(TOOLS_DIR)/kernel.ld -o $(KERNEL) $(KOBJS) $(LOBJS)

%.o: %.S
	$(CC) $(CFLAGS) -c -o $@ $<

%.o: %.c
	$(CC) $(CFLAGS) -c -o $@ $<

.PHONY: clean
clean:
	rm -f $(shell find $(TOP_DIR) -name *.o)
	rm -f $(shell find $(TOP_DIR) -name *.out)
	rm -f $(BOOT_BLOCK) $(KERNEL) $(SIGN)