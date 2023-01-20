// vim: ts=2:sw=2
#include "core.h"
#include "exec.h"

#ifndef part
#include "core.c"
#include "hash.c"

#include "ia64.c"
#include "arm8.c"

#include "exec.c"

//#include "elfs.c"
//#include "pe32.c"
//#include "mach.c"
#endif

extern const emit ia64;
extern const emit arm8;
#ifdef __aarch64__
#define base arm8
#else // __x86_64__
#define base ia64
#endif

int main(int argc, char** argv) {
  mode self = {};

  if(argc != 3) return 1;
  self.hold = hold;
  self.drop = drop;
  self.exec = show;
  switch(argv[1][0]) {
    case 'a': self.arch = arm8; break;
    case 'i': self.arch = ia64; break;
    case 'e': self.arch = base; self.exec = exec; break;
    default : return 1;
  }

  self.name = argv[2];
  make(&self);

  self.name = "bins/ia64.o";
  elfs(&self);

  return 0;
}

