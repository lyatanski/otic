// vim: ts=2:sw=2
#include "core.h"

bool  name(item* x, char* key, u64i len) {
  if(x->len != len) return 0;
  char* p = x->key;
  while(len && *p++==*key++) len--;
  return !len || *p==*key;
}

item* hget(hash self, char* mark, u64i size) {
  u64i  clue = 0x01;
  item* seek = NULL;

  // TODO add retry count to avoid endless cycle
  do {
    for(u64i i = 0; i < size; clue += (clue << 5) + (clue >> 27) + mark[i++]);
    printf("clue %016lx %08lx\n", clue, clue & (POOL - 1));
    seek = self + (clue & (POOL - 1));
  } while(seek->key && !name(seek, mark, size));

  if(!seek->key) {
    seek->key = mark;
    seek->len = size;
  }

  return seek;
}

