// vim: ts=2:sw=2
#include "core.h"

long  name(item* x, char* key, size_t len) {
  if(x->len != len) return 0;
  char* p = x->key;
  while(len && *p++==*key++) len--;
  return !len || *p==*key;
}

item* hget(hash map, char* key, long len) {
  unsigned long idx = 0x1;
  for(int i = 0; i < len; i++) idx += (idx << 5) + (idx >> 27) + key[i];

  item* seek = map + (idx & (POOL - 1));
  while(seek->key && !name(seek, key, len)) {
    if(!seek->next) {
      seek->next = malloc(sizeof(item));
      *seek->next = (item){};
    }
    seek = seek->next;
  }
  if(!seek->key) {
    seek->key = key;
    seek->len = len;
  }

  return seek;
}

void  hrid(hash map) {
  for(int i = 0; i < POOL; i++) {
    item* next = map[i].next;
    while(next) {
      item* p = next;
      next = next->next;
      free(p);
    }
  }
}

