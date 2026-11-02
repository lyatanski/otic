// vim: ts=2:sw=2
#include "core.h"

// string hash, computed once for every table a name is looked up in
u64i  clue(char* mark, u64i size) {
  u64i clue = 0x01;
  for(u64i i = 0; i < size; clue += (clue << 5) + (clue >> 27) + mark[i++]);
  return clue;
}

// the slot of the name in the table, or the free slot where it goes; NULL
// when 10 probes fail. The step is odd: it reaches every slot
static item** hpos(unit* self, char* mark, u64i size, u64i clue) {
  u64i step = (clue >> 16) | 1;
  for(u64i i = 0; i < 10 && self->size; i++) {
    item** at = self->pool + ((clue + i * step) & (self->size - 1));
    if(!*at || ((*at)->size == size && alike((*at)->name, mark, size))) return at;
  }
  return NULL;
}

item* hsee(unit* self, char* mark, u64i size, u64i clue) {
  item** at = hpos(self, mark, size, clue);
  return at? *at: NULL;
}

// double the table, again until each item is within 10 probes
static void  hgrow(unit* self) {
  item** pool = self->pool;
  u32i   size = self->size, i;
  self->size = size? size: 32;
  do {
    self->size *= 2;
    self->pool = calloc(self->size, sizeof(item*));
    if(!self->pool) fail("out of memory\n");
    for(i = 0; i < size; i++) {
      if(!pool[i]) continue;
      item** at = hpos(self, pool[i]->name, pool[i]->size, clue(pool[i]->name, pool[i]->size));
      if(!at) break;
      *at = pool[i];
    }
    if(i < size) free(self->pool);
  } while(i < size);
  free(pool);
}

// the table grows when 3/4 full or when 10 probes fail. Items are allocated
// apart from it and never move: metas, words and fixes point to them
static item* hput(unit* self, char* mark, u64i size, u64i k) {
  static item* heap;
  static u32i  left;
  item** at = hpos(self, mark, size, k);
  if(at && *at) return *at;
  while(!at || 4 * (self->used + 1) > 3 * self->size) {
    hgrow(self);
    at = hpos(self, mark, size, k);
  }
  if(!left) {
    heap = calloc(left = 256, sizeof(item));
    if(!heap) fail("out of memory\n");
  }
  self->used++;
  *at = heap + --left;
  **at = (item){.size = size, .name = mark};
  return *at;
}

// a new item also joins the items of its spelling, in join order, so that
// find visits only the units that have the name
item* hget(mode* self, u32i at, char* mark, u64i size) {
  u64i  k = clue(mark, size);
  unit* u = self->units + at;
  u32i  used = u->used;
  item* s = hput(u, mark, size, k);
  if(u->used == used) return s;
  item** p = (item**)&hput(&self->names, mark, size, k)->data;
  for(; *p && (*p)->unit < at; p = &(*p)->next);
  s->unit = at;
  s->next = *p;
  return *p = s;
}
