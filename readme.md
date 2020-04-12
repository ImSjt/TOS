gcc -I/work/tos/01_boot/include/ -fno-builtin -fno-PIC -Wall -ggdb -m32 -gstabs -nostdinc -fno-stack-protector -Os -c bootasm.S -o bootasm.o

gcc -I/work/tos/01_boot/include/ -fno-builtin -fno-PIC -Wall -ggdb -m32 -gstabs -nostdinc -fno-stack-protector -Os -c bootmain.c -o bootmain.o

ld -m elf_i386 -nostdlib -N -T boot.ld bootasm.o bootmain.o -o bootblock.o

objcopy -S -O binary bootblock.o bootblock.out
