// vim: ts=2:sw=2
#include "core.h"

extern emit arm8;
extern emit i386;

int main(int argc, char** argv) {
  if(argc != 3) return 1;

  mode self;
  init(&self);

  if(argv[1][0] == 'a') self.arch = arm8;
  if(argv[1][0] == 'i') self.arch = i386;

  file* code = scan(argv[2]);
  self.list = chop(code, &self.pile);
  make(&self);
  drop(code);

  return 0;
}

