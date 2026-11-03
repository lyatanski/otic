// vim: ts=2:sw=2
#include "core.h"

// the byte helpers of every part, declared in core.h
byte* copy(byte* dst, void* src, u64i size) {
  for(byte* s = src; size--; *dst++ = *s++);
  return dst;
}

u64i  slen(char* s) {
  u64i n = 0;
  while(s[n]) n++;
  return n;
}

void* swell(mode* self, void* p, u32i n, u32i* cap, u64i size, u32i least) {
  if(n < *cap) return p;
  *cap = *cap * 2 + least;
  return self->heap(p, *cap * size);
}

void* zero(mode* self, u64i size) {
  byte* p = self->heap(NULL, size);
  for(u64i i = 0; i < size; p[i++] = 0);
  return p;
}

bool  alike(void* x, void* y, u64i size) {
  byte *a = x, *b = y;
  for(; size && *a == *b; size--, a++, b++);
  return !size;
}


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
static void  hgrow(mode* m, unit* self) {
  item** pool = self->pool;
  u32i   size = self->size, i;
  self->size = size? size: 32;
  do {
    self->size *= 2;
    self->pool = zero(m, self->size * sizeof(item*));
    for(i = 0; i < size; i++) {
      if(!pool[i]) continue;
      item** at = hpos(self, pool[i]->name, pool[i]->size, clue(pool[i]->name, pool[i]->size));
      if(!at) break;
      *at = pool[i];
    }
    if(i < size) m->heap(self->pool, 0);
  } while(i < size);
  m->heap(pool, 0);
}

// the table grows when 3/4 full or when 10 probes fail. Items are allocated
// apart from it and never move: metas, words and fixes point to them
static item* hput(mode* m, unit* self, char* mark, u64i size, u64i k) {
  static item* spare;
  static u32i  left;
  item** at = hpos(self, mark, size, k);
  if(at && *at) return *at;
  while(!at || 4 * (self->used + 1) > 3 * self->size) {
    hgrow(m, self);
    at = hpos(self, mark, size, k);
  }
  if(!left) spare = m->heap(NULL, (left = 256) * sizeof(item));
  self->used++;
  *at = spare + --left;
  **at = (item){.size = size, .name = mark};
  return *at;
}

// a new item also joins the items of its spelling, in join order, so that
// find visits only the units that have the name
item* hget(mode* self, u32i at, char* mark, u64i size) {
  u64i  k = clue(mark, size);
  unit* u = self->units + at;
  u32i  used = u->used;
  item* s = hput(self, u, mark, size, k);
  if(u->used == used) return s;
  item** p = (item**)&hput(self, &self->names, mark, size, k)->data;
  for(; *p && (*p)->unit < at; p = &(*p)->next);
  s->unit = at;
  s->next = *p;
  return *p = s;
}
