// vim: ts=2:sw=2
#include "core.h"

extern emit arm8;
extern emit ia64;

int main(int argc, char** argv) {
  if(argc != 3) return 1;

  mode self;
  init(&self);

  switch(argv[1][0]) {
    case 'a': self.arch = arm8; break;
    case 'i': self.arch = ia64; break;
    case 'e': {
      size_t size = 60;
      void* memory = hold(size);
#     ifdef __aarch64__
      arm8.text(memory);
#     else // __x86_64__
      ia64.text(memory);
#     endif
      exec(memory);
      drope(memory, size);
      return 0;
    }
    default : return 1;
  }

  file* code = scan(argv[2]);
  self.list = chop(code, &self.pile);
  make(&self);
  drop(code);

  return 0;
}

