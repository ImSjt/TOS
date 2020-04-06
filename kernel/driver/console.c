#include "driver/cga.h"
#include "driver/kbd.h"
#include "sync/sync.h"

#define CONSBUFSIZE 512

// 构建一个循环buf
static struct {
    uint8_t buf[CONSBUFSIZE];
    uint32_t rpos;
    uint32_t wpos;
} cons;

void cons_init(void) {
    cga_init();
    kbd_init();
}

void cons_putc(int c) {
    cga_putc(c);
}

static void cons_intr(int (*proc)(void)) {
    int c;
    while ((c = (*proc)()) != -1) {
        if (c != 0) {
            cons.buf[cons.wpos ++] = c;
            if (cons.wpos == CONSBUFSIZE) {
                cons.wpos = 0;
            }
        }
    }
}

static void kbd_intr(void) {
    cons_intr(kbd_proc_data);
}

int cons_getc(void) {
    int c = 0;
    bool intr_flag;
    
    local_intr_save(intr_flag);
    {
        kbd_intr();

        if (cons.rpos != cons.wpos) {
            c = cons.buf[cons.rpos ++];
            if (cons.rpos == CONSBUFSIZE) {
                cons.rpos = 0;
            }
        }
    }
    local_intr_restore(intr_flag);

    return c;
}