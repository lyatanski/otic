// vim: ts=2:sw=2
#include "core.h"

#define SIZE (1 << 13)

struct hash {
  int   size;
  int   used;
  item* cask;
};

void hnew(hash** map) {
  (*map) = malloc(sizeof(hash) + SIZE * sizeof(item));
  (*map)->size = SIZE;
  (*map)->used = 0;
  (*map)->cask = (void*)((*map) + 1);
  for(int i = 0; i < (*map)->size; i++) (*map)->cask[i] = (item){};
}

static long  name(char* x, char* y, int l) {
  while(l && *x && *x++==*y++) l--;
  return !l || *x==*y;
}

item* hget(hash** map, char* key, long len) {
  unsigned long idx = 0x1;
  for(int i = 0; i < len; i++) idx += (idx << 5) + (idx >> 27) + key[i];

  item* seek = (*map)->cask + (idx & ((*map)->size - 1));
  while(seek->key && !name(seek->key, key, len)) {
    if(!seek->next) {
      seek->next = malloc(sizeof(item));
      *seek->next = (item){};
    }
    seek = seek->next;
  }
  if(!seek->key) {
    seek->key = malloc(len+1);
    for(int i = 0; i < len; i++) seek->key[i] = key[i];
    seek->key[len] = '\0';
    seek->len = len;
  }

  return seek;
}

void  hrid(hash** map) {
  for(int i = 0; i < (*map)->size; i++) free((*map)->cask[i].key);
  free(*map);
}

