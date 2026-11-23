// vim: ts=2:sw=2
// a target driven through its emit table as core drives it; bytes checked
// against objdump
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

// bytes since start == want, else prints both
static bool  hex(const char* want) {
  char got[512] = "";
  u32i n = 0;
  for(byte* p = t->head; p < t->body && n < sizeof got - 4; p++)
    n += sprintf(got + n, n? " %02x": "%02x", *p);
  if(!strcmp(got, want)) return 1;
  printf("    want %s\n    got  %s\n", want, got);
  return 0;
}

// emits("b8 2a 00 00 00", e.move(t, 42))
#define emits(want, ...) (start(), __VA_ARGS__, hex(want))
// for the calls that patch
#define here (t->body)
