// helpers.c — host-side console stubs for the bare-metal stress test.
// Runtime (bump allocator, exception globals, autoreleasepool, alloc/
// retain/release/autorelease) comes from include/ovel/runtime_freestanding.c.
#include <stdio.h>

void kputs(const char *s) { fputs(s, stdout); }
void kputdec(int v)       { fprintf(stdout, "%d", v); }
void kputhex(unsigned v)  { fprintf(stdout, "%x", v); }

// Strong override of the runtime's weak console hook (NPLog + uncaught
// exceptions route through it).
void ovel_console_write(const char *s, unsigned len) {
    if (len) fwrite(s, 1, len, stdout);
    else fputs(s, stdout);
}
