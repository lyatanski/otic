// vim: ts=2:sw=2
// the encoder specs: a target alone, driven through its emit table as core
// drives it. A case runs calls on a fresh text and compares the bytes, in hex.
// The bytes were checked against objdump, which shows them in the comments
#include <stdio.h>
#include <string.h>
#include <time.h> // test.h's, before the encoder: its macros clash with system headers
#include "../util.c"

static byte  textb[4096];
static file* t = (file*)textb;

static void  start(void) {
  t->body = t->head;
  t->tail = textb + sizeof textb;
}

// the bytes emitted since start are want, else both are shown
static bool  hex(const char* want) {
  char got[512] = "";
  u32i n = 0;
  for(byte* p = t->head; p < t->body && n < sizeof got - 4; p++)
    n += sprintf(got + n, n? " %02x": "%02x", *p);
  if(!strcmp(got, want)) return 1;
  printf("    want %s\n    got  %s\n", want, got);
  return 0;
}

// emits("b8 2a 00 00 00", e.move(t, 42)): the calls, in order, emit want
#define emits(want, ...) (start(), __VA_ARGS__, hex(want))
// where the next call emits, for the calls that patch
#define here (t->body)
