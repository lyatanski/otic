// vim: ts=2:sw=2
#include "core.h"

int main(int argc, char** argv) {
  if(argc != 2) return 1;

  hash* map;
  hnew(&map);

  file* code = scan(argv[1]);
  word* list = chop(code, &map);
  tend(list, &map);
  drop(code);

  return 0;
}

