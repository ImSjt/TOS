#include "defs.h"
#include "x86.h"
#include "string.h"
#include "mm/memlayout.h"
#include "driver/cga.h"

#define CGA_BASE        0x3D4
#define CGA_BUF         0xB8000
#define CRT_ROWS        25
#define CRT_COLS        80
#define CRT_SIZE        (CRT_ROWS * CRT_COLS)

static uint16_t addr_6845;
static uint16_t* crt_buf;
static uint16_t crt_pos;

void cga_init(void) {
    volatile uint16_t* cp = (uint16_t*)(CGA_BUF + KERNBASE);

    addr_6845 = (uint16_t)CGA_BASE;

    uint32_t pos;
    outb(addr_6845, 14);
    pos = inb(addr_6845 + 1) << 8;
    outb(addr_6845, 15);
    pos |= inb(addr_6845 + 1);

    crt_buf = (uint16_t*)cp;
    crt_pos = pos;
}

void cga_putc(int c) {
   if (!(c & ~0xFF)) {
        c |= 0x0700;
    }

    switch (c & 0xFF) {
    case '\b':
        if (crt_pos > 0) {
            crt_pos--;
            crt_buf[crt_pos] = (c & ~0xff) | ' ';
        }
        break;
    
    case '\n':
        crt_pos += CRT_COLS;
    
    case '\r':
        crt_pos -= (crt_pos % CRT_COLS);
        break;

    default:
        crt_buf[crt_pos++] = c;
        break;
    }

    if (crt_pos > CRT_SIZE) {
        int i;
        memmove(crt_buf, crt_buf + CRT_COLS, (CRT_SIZE - CRT_COLS) * sizeof(uint16_t));
        for (i = CRT_SIZE - CRT_COLS; i < CRT_SIZE; i ++) {
            crt_buf[i] = 0x0700 | ' ';
        }
        crt_pos -= CRT_COLS;
    }

    outb(addr_6845, 14);
    outb(addr_6845 + 1, crt_pos >> 8);
    outb(addr_6845, 15);
    outb(addr_6845 + 1, crt_pos);
}
