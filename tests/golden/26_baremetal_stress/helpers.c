// helpers.c — minimal console output for the stress test.
#include <stdio.h>
#include <ovic/runtime.h>

void kputs(const char *s) { fputs(s, stdout); }
void kputdec(int v)        { fprintf(stdout, "%d", v); }
void kputhex(unsigned v)   { fprintf(stdout, "%x", v); }
void kputc(char c)         { putchar(c); }