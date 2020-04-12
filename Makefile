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

USRC_DIR := $(TOP_DIR)/user/
USER_OBJ := user/__user_exit.out
USRC_ASM_FILES := $(shell find $(USRC_DIR) -name *.S)
USRC_C_FILES := $(shell find $(USRC_DIR) -name *.c)
USRC_FILES := $(USRC_ASM_FILES) $(USRC_C_FILES)

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

$(KERNEL): $(KOBJS) $(LOBJS) $(USER_OBJ)
	$(LD) $(LDFLAGS) -T $(TOOLS_DIR)/kernel.ld -o $(KERNEL) $(KOBJS) $(LOBJS) -b binary $(USER_OBJ)

$(USER_OBJ): $(USRC_FILES)
	gcc -fno-builtin -Wall -ggdb -m32 -gstabs -nostdinc  -fno-stack-protector -I $(USRC_DIR)/include/ -c $(USRC_DIR)/exit.c -o $(USRC_DIR)/exit.o
	gcc -fno-builtin -Wall -ggdb -m32 -gstabs -nostdinc  -fno-stack-protector -I $(USRC_DIR)/include/ -c $(USRC_DIR)/libs/initcode.S -o $(USRC_DIR)/libs/initcode.o
	gcc -fno-builtin -Wall -ggdb -m32 -gstabs -nostdinc  -fno-stack-protector -I $(USRC_DIR)/include/ -c $(USRC_DIR)/libs/panic.c -o $(USRC_DIR)/libs/panic.o
	gcc -fno-builtin -Wall -ggdb -m32 -gstabs -nostdinc  -fno-stack-protector -I $(USRC_DIR)/include/ -c $(USRC_DIR)/libs/printfmt.c -o $(USRC_DIR)/libs/printfmt.o
	gcc -fno-builtin -Wall -ggdb -m32 -gstabs -nostdinc  -fno-stack-protector -I $(USRC_DIR)/include/ -c $(USRC_DIR)/libs/stdio.c -o $(USRC_DIR)/libs/stdio.o
	gcc -fno-builtin -Wall -ggdb -m32 -gstabs -nostdinc  -fno-stack-protector -I $(USRC_DIR)/include/ -c $(USRC_DIR)/libs/string.c -o $(USRC_DIR)/libs/string.o
	gcc -fno-builtin -Wall -ggdb -m32 -gstabs -nostdinc  -fno-stack-protector -I $(USRC_DIR)/include/ -c $(USRC_DIR)/libs/syscall.c -o $(USRC_DIR)/libs/syscall.o
	gcc -fno-builtin -Wall -ggdb -m32 -gstabs -nostdinc  -fno-stack-protector -I $(USRC_DIR)/include/ -c $(USRC_DIR)/libs/ulib.c -o $(USRC_DIR)/libs/ulib.o
	gcc -fno-builtin -Wall -ggdb -m32 -gstabs -nostdinc  -fno-stack-protector -I $(USRC_DIR)/include/ -c $(USRC_DIR)/libs/umain.c -o $(USRC_DIR)/libs/umain.o
	ld -m elf_i386 -nostdlib -N -T $(TOOLS_DIR)/user.ld -o $(USER_OBJ) $(USRC_DIR)/libs/initcode.o $(USRC_DIR)/libs/panic.o $(USRC_DIR)/libs/printfmt.o $(USRC_DIR)/libs/stdio.o $(USRC_DIR)/libs/string.o $(USRC_DIR)/libs/syscall.o $(USRC_DIR)/libs/ulib.o $(USRC_DIR)/libs/umain.o $(USRC_DIR)/exit.o

%.o: %.S
	$(CC) $(CFLAGS) -c -o $@ $<

%.o: %.c
	$(CC) $(CFLAGS) -c -o $@ $<

.PHONY: clean
clean:
	rm -f $(shell find $(TOP_DIR) -name *.o)
	rm -f $(shell find $(TOP_DIR) -name *.out)
	rm -f $(BOOT_BLOCK) $(KERNEL) $(SIGN)