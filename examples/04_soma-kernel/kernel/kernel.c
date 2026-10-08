#include <stdarg.h>
#include <stddef.h>
#include <stdint.h>
#include <stdbool.h>
#include <string.h>
#include <nepa/runtime.h>
#include "kernel.h"

/* ================= freestanding libc bits ================= */

// memcpy is provided by runtime_freestanding.c (used by @try and the allocator).

void *memmove(void *dst, const void *src, size_t n) {
    unsigned char *d = dst;
    const unsigned char *s = src;
    if (d < s) {
        while (n--) *d++ = *s++;
    } else {
        d += n;
        s += n;
        while (n--) *--d = *--s;
    }
    return dst;
}

void *memset(void *s, int c, size_t n) {
    unsigned char *p = s;
    while (n--) *p++ = (unsigned char)c;
    return s;
}

int memcmp(const void *a, const void *b, size_t n) {
    const unsigned char *x = a;
    const unsigned char *y = b;
    while (n--) {
        if (*x != *y) return (int)*x - (int)*y;
        x++;
        y++;
    }
    return 0;
}

size_t strlen(const char *s) {
    const char *p = s;
    while (*p) p++;
    return (size_t)(p - s);
}

int strcmp(const char *a, const char *b) {
    while (*a && *a == *b) {
        a++;
        b++;
    }
    return (int)(unsigned char)*a - (int)(unsigned char)*b;
}

int strncmp(const char *a, const char *b, size_t n) {
    while (n && *a && *a == *b) {
        a++;
        b++;
        n--;
    }
    if (n == 0) return 0;
    return (int)(unsigned char)*a - (int)(unsigned char)*b;
}

char *strcpy(char *dst, const char *src) {
    char *d = dst;
    while ((*d++ = *src++)) {}
    return dst;
}

char *strncpy(char *dst, const char *src, size_t n) {
    char *d = dst;
    while (n && *src) {
        *d++ = *src++;
        n--;
    }
    while (n--) *d++ = '\0';
    return dst;
}

char *strcat(char *dst, const char *src) {
    char *d = dst;
    while (*d) d++;
    while ((*d++ = *src++)) {}
    return dst;
}

char *strchr(const char *s, int c) {
    char ch = (char)c;
    while (*s) {
        if (*s == ch) return (char *)s;
        s++;
    }
    return (ch == '\0') ? (char *)s : NULL;
}

char *strstr(const char *haystack, const char *needle) {
    if (*needle == '\0') return (char *)haystack;
    while (*haystack) {
        const char *h = haystack;
        const char *n = needle;
        while (*h && *n && *h == *n) {
            h++;
            n++;
        }
        if (*n == '\0') return (char *)haystack;
        haystack++;
    }
    return NULL;
}

/* ================= VGA text console ================= */

#define VGA_MEM ((volatile uint16_t *)0xB8000)
#define VGA_W   80
#define VGA_H   25

static int vga_row = 0;
static int vga_col = 0;
static uint8_t vga_attr = 0x07;

static void vga_scroll(void) {
    int r, c;
    for (r = 1; r < VGA_H; r++)
        for (c = 0; c < VGA_W; c++)
            VGA_MEM[(r - 1) * VGA_W + c] = VGA_MEM[r * VGA_W + c];
    for (c = 0; c < VGA_W; c++)
        VGA_MEM[(VGA_H - 1) * VGA_W + c] = (uint16_t)((vga_attr << 8) | ' ');
}

/* Reverse-video software cursor (replaces the hardware block which hides chars). */
static uint16_t vga_reverse(uint16_t cell) {
    uint16_t fg = (cell >> 8) & 0x0F;
    uint16_t bg = (cell >> 12) & 0x0F;
    return (cell & 0x00FF) | (fg << 12) | (bg << 8);
}

static uint16_t vga_cur_saved;
static int      vga_cur_on = 0;
static unsigned vga_cur_idx = 0;

static void vga_cursor_hide(void) {
    if (vga_cur_on) {
        VGA_MEM[vga_cur_idx] = vga_cur_saved;
        vga_cur_on = 0;
    }
}

static void vga_cursor_show(void) {
    vga_cur_idx = (unsigned)(vga_row * VGA_W + vga_col);
    vga_cur_saved = VGA_MEM[vga_cur_idx];
    VGA_MEM[vga_cur_idx] = vga_reverse(vga_cur_saved);
    vga_cur_on = 1;
}

static void serial_putc(char c);

void kcur_move(int dx) {
    int nc = vga_col + dx;
    if (nc >= 0 && nc < VGA_W) {
        vga_cursor_hide();
        vga_col = nc;
        vga_cursor_show();
        /* keep the serial terminal cursor in sync (ANSI cursor move) */
        serial_putc('\x1B');
        serial_putc('[');
        serial_putc(dx < 0 ? 'D' : 'C');
    }
}

static void vga_putc(char ch) {
    vga_cursor_hide();
    if (ch == '\n') {
        vga_col = 0;
        vga_row++;
    } else if (ch == '\r') {
        vga_col = 0;
    } else if (ch == '\t') {
        vga_col = (vga_col + 4) & ~3;
    } else if (ch == '\b') {
        if (vga_col > 0) {
            vga_col--;
            VGA_MEM[vga_row * VGA_W + vga_col] = (uint16_t)((vga_attr << 8) | ' ');
        }
    } else {
        if (vga_col >= VGA_W) {
            vga_col = 0;
            vga_row++;
        }
        if (vga_row >= VGA_H) {
            vga_scroll();
            vga_row = VGA_H - 1;
        }
        VGA_MEM[vga_row * VGA_W + vga_col] = (uint16_t)((vga_attr << 8) | (uint8_t)ch);
        vga_col++;
    }
    if (vga_row >= VGA_H) {
        vga_scroll();
        vga_row = VGA_H - 1;
    }
    vga_cursor_show();
}

void vga_clear(void) {
    int i;
    vga_cursor_hide();
    for (i = 0; i < VGA_W * VGA_H; i++)
        VGA_MEM[i] = (uint16_t)((vga_attr << 8) | ' ');
    vga_row = 0;
    vga_col = 0;
    /* disable hardware block cursor (bit 5 of cursor start reg 0x0A) */
    outb(0x3D4, 0x0A);
    outb(0x3D5, 0x20);
    vga_cursor_show();
}

void vga_set_attr(uint8_t attr) {
    vga_attr = attr;
}

/* ================= serial (COM1) ================= */

static void serial_putc(char c) {
    while ((inb(0x3F8 + 5) & 0x20) == 0) {}
    outb(0x3F8, (uint8_t)c);
}

void serial_init(void) {
    outb(0x3F8 + 1, 0x00);
    outb(0x3F8 + 3, 0x80);
    outb(0x3F8 + 0, 0x03);
    outb(0x3F8 + 1, 0x00);
    outb(0x3F8 + 3, 0x03);
    outb(0x3F8 + 2, 0xC7);
    outb(0x3F8 + 4, 0x0B);
}

/* ================= kernel console ================= */

void kputc(char c) {
    vga_putc(c);
    serial_putc(c);
}

void kputs(const char *s) {
    while (*s) kputc(*s++);
}

void kputdec(int v) {
    char buf[12];
    int i = 0;
    unsigned int u;
    if (v < 0) {
        kputc('-');
        u = (unsigned int)(-v);
    } else {
        u = (unsigned int)v;
    }
    do {
        buf[i++] = (char)('0' + u % 10);
        u /= 10;
    } while (u);
    while (i) kputc(buf[--i]);
}

void kputhex(unsigned int v) {
    char buf[11];
    int i = 0;
    static const char digits[] = "0123456789abcdef";
    do {
        buf[i++] = digits[v & 0xF];
        v >>= 4;
    } while (v);
    while (i) kputc(buf[--i]);
}

static void kprint_num(unsigned int v, unsigned int base, int width, int zero_pad, int upper) {
    char buf[34];
    int i = 0;
    static const char low[] = "0123456789abcdef";
    static const char up[] = "0123456789ABCDEF";
    const char *digits = upper ? up : low;
    do {
        buf[i++] = digits[v % base];
        v /= base;
    } while (v);
    while (i < width) buf[i++] = zero_pad ? '0' : ' ';
    while (i) kputc(buf[--i]);
}

void kprintf(const char *fmt, ...) {
    va_list ap;
    va_start(ap, fmt);
    for (; *fmt; fmt++) {
        if (*fmt != '%') {
            kputc(*fmt);
            continue;
        }
        fmt++;
        if (*fmt == '\0') break;

        int width = 0;
        int zero_pad = 0;
        while (*fmt == '0') {
            zero_pad = 1;
            fmt++;
        }
        while (*fmt >= '0' && *fmt <= '9') {
            width = width * 10 + (*fmt - '0');
            fmt++;
        }

        switch (*fmt) {
        case 'd':
        case 'i': {
            int v = va_arg(ap, int);
            if (v < 0) {
                kputc('-');
                kprint_num((unsigned int)(-v), 10, width - 1, zero_pad, 0);
            } else {
                kprint_num((unsigned int)v, 10, width, zero_pad, 0);
            }
            break;
        }
        case 'u':
            kprint_num(va_arg(ap, unsigned int), 10, width, zero_pad, 0);
            break;
        case 'x':
            kprint_num(va_arg(ap, unsigned int), 16, width, zero_pad, 0);
            break;
        case 'X':
            kprint_num(va_arg(ap, unsigned int), 16, width, zero_pad, 1);
            break;
        case 'p':
            kprint_num((unsigned int)(uintptr_t)va_arg(ap, void *), 16, 8, 1, 0);
            break;
        case 'c':
            kputc((char)va_arg(ap, int));
            break;
        case 's': {
            const char *s = va_arg(ap, const char *);
            if (!s) s = "(null)";
            while (*s) kputc(*s++);
            break;
        }
        case '%':
            kputc('%');
            break;
        default:
            kputc('%');
            kputc(*fmt);
            break;
        }
    }
    va_end(ap);
}

/* ================= Nepa module exports ================= */

void soma_core_boot(void);
void soma_io_wait(void);
int  soma_fib(int n);
int  soma_gcd(int a, int b);
unsigned int soma_xorshift(unsigned int seed);
unsigned int soma_fnv1a(const char *s);
unsigned int soma_rotl(unsigned int v, int sh);

/* Nepa @namespace SomaCore + @interface Calculator (implicit root class) */
struct SomaCore__Calculator {
    struct NPClass *isa;
    uint32_t retain_count;
    int total;
};
extern NPClass nepa_SomaCore__Calculator_class;
extern void nepa_meta_init(void);
void soma_class_demo(void);
void soma_instance_demo(struct SomaCore__Calculator *acc);

/* Nepa @interface NPIoError — exception object for @try/@catch */
struct SomaCore__NPIoError {
    struct NPClass *isa;
    uint32_t retain_count;
    int code;
};
extern NPClass nepa_SomaCore__NPIoError_class;
void soma_exc_demo(id err);
void soma_heap_demo(void);
void soma_advanced_demo(void);
void soma_kbd_demo(void);

/* Runtime globals (nepa___nepa_root_class, __nepa_exception_buf,
 * __nepa_exception_value, memcpy) are provided by runtime_freestanding.c. */
void kmain(void) {
    extern volatile uint32_t tick;

    vga_clear();
    serial_init();
    idt_init();
    pic_remap();
    pit_init(100);

    kputs("\n=== SOMA KERNEL (i686, 32-bit protected mode) ===\n");
    kputs("built: clang + nasm + nepac transpile, ran under qemu-system-i386\n");

    /* Nepa -> C : the Nepa module prints via kputs/kputdec/kputhex */
    soma_core_boot();

    /* Nepa advanced features: @namespace + @interface (implicit root class).
     * nepa_meta_init() (emitted weak by the transpiler) fills in class
     * metadata; the implicit root class metadata is defined above. */
    nepa_meta_init();
    soma_class_demo();

    struct SomaCore__Calculator acc;
    memset(&acc, 0, sizeof(acc));
    acc.isa = &nepa_SomaCore__Calculator_class;   /* hand-built instance */
    soma_instance_demo(&acc);

    /* @try/@catch/@finally on bare metal: throw a hand-built NPIoError */
    struct SomaCore__NPIoError err;
    memset(&err, 0, sizeof(err));
    err.isa = &nepa_SomaCore__NPIoError_class;
    err.code = 42;
    soma_exc_demo((id)&err);

    /* alloc+init via bump allocator: [[HeapCounter alloc] init] */
    soma_heap_demo();

    /* @protocol + @property + @synthesize + @public ivar access */
    soma_advanced_demo();

    /* C -> Nepa : kernel calls Nepa math functions directly */
    kprintf("[c] call Nepa: fib(15)=%d gcd(1071,462)=%d\n",
            soma_fib(15), soma_gcd(1071, 462));
    kprintf("[c] call Nepa: rotl(0x12345678,4)=0x%x\n", soma_rotl(0x12345678u, 4));
    kprintf("[c] call Nepa: fnv1a(\"nepa\")=0x%x\n", soma_fnv1a("nepa"));

    /* Nepa inline asm io_wait (outb to 0x80) used from C */
    for (int i = 0; i < 16; i++) soma_io_wait();

    asm volatile("sti");
    kprintf("[c] interrupts on; waiting for PIT ticks...\n");
    while (tick < 50) {}
    kprintf("[c] timer reached tick=%u (IRQ0+PIT ok)\n", (unsigned int)tick);

    /* keyboard input demo (IRQ1): type then Enter; times out headless */
    soma_kbd_demo();

    kputs("\nSOMA KERNEL OK\n");

    /* tell qemu to exit via isa-debug-exit (port 0xf4): exit code 33 */
    outl(0xF4, 0x10);
    while (1) {
        asm volatile("hlt");
    }
}
