// vim: ts=2:sw=2
#include "core.h"

int main(int argc, char** argv)
{
  if(argc != 2) return 1;
  file* code = read(argv[1]);
  word* list = chop(code);
  sift(list);
  //drop(code);
  //printf("%s\n", f->head);
  return 0;
}

