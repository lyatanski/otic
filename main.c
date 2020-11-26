// vim: ts=2:sw=2
#include "core.h"

int main(int argc, char** argv) {
  if(argc != 2) return 1;

  mode self;

  hash* map;
  hnew(&map);

  self.pile = map;
  file* code = scan(argv[1]);
  self.list = chop(code, &map);
  make(&self);
  drop(code);

  return 0;
}

