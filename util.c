// vim: ts=2:sw=2
#include "core.h"

// the byte helpers of every part, declared in core.h
byte* copy(byte* dst, void* src, u64i size) {
  for(byte* s = src; size--; *dst++ = *s++);
  return dst;
}

u64i  slen(char* s) {
  u64i n = 0;
  while(s[n]) n++;
  return n;
}

void* swell(void* p, u32i n, u32i* cap, u64i size, u32i least) {
  if(n < *cap) return p;
  *cap = *cap * 2 + least;
  if(!(p = realloc(p, *cap * size))) fail("out of memory\n");
  return p;
}

bool  alike(void* x, void* y, u64i size) {
  byte *a = x, *b = y;
  for(; size && *a == *b; size--, a++, b++);
  return !size;
}
