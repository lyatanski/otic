// vim: ts=2:sw=2
#include "core.h"

item* match(item* x, char* key, u64i len) {
  if(!x->key) {
    x->key = key;
    x->len = len;
    return x;
  }
  if(x->len != len) return NULL;
  for(char* p = x->key; len && *p++==*key++; len--);
  return !len? x: NULL;
}

item* hget(hash self, char* mark, u64i size) {
  u64i  clue = 0x01;
  item* seek = NULL;

  // calculate string hash
  for(u64i i = 0; i < size; clue += (clue << 5) + (clue >> 27) + mark[i++]);
  //u64i  clue = 5381;
  //for(u64i i = 0; i < size; clue += (clue << 5) + clue + mark[i++]);
  //u64i  clue = 0;
  //for(u64i i = 0; i < size; clue += (clue << 6) + (clue << 16)i - clue + mark[i++]);
  //printf("clue %016lx %08lx % 4ld %.*s\n", clue, clue & (POOL - 1), size, (int)size, mark);

  // lookup free spot or the same string
  for(u64i i = 1; !seek && i <= 10; i++)
    seek = match(self + ((i * clue) & (POOL - 1)), mark, size);

  return seek;
}

