// vim: ts=2:sw=2
#include "core.h"

// the byte helpers of every part, declared in core.h
byte* copy(byte* dst, const void* src, u64i size) {
  for(const byte* s = src; size--; *dst++ = *s++);
  return dst;
}

u64i  slen(const char* s) {
  u64i n = 0;
  while(s[n]) n++;
  return n;
}

void* swell(const mode* self, void* p, u32i n, u32i* cap, u64i size, u32i least) {
  if(n < *cap) return p;
  *cap = *cap * 2 + least;
  return self->heap(p, *cap * size);
}

void* zero(const mode* self, u64i size) {
  byte* p = self->heap(NULL, size);
  for(u64i i = 0; i < size; p[i++] = 0);
  return p;
}

bool  alike(const void* x, const void* y, u64i size) {
  const byte *a = x, *b = y;
  for(; size && *a == *b; size--, a++, b++);
  return !size;
}


// string hash, computed once for every table a name is looked up in
u64i  clue(const char* mark, u64i size) {
  u64i clue = 0x01;
  for(u64i i = 0; i < size; clue += (clue << 5) + (clue >> 27) + mark[i++]);
  return clue;
}

// the slot of the name in the table, or the free slot where it goes; NULL
// when the table is empty. The step is odd: it reaches every slot, and the
// table, at most 3/4 full, has a free one
static item** hpos(unit* self, const char* mark, u64i size, u64i clue) {
  u64i step = (clue >> 16) | 1;
  for(u64i i = 0; self->size; i++) {
    item** at = self->pool + ((clue + i * step) & (self->size - 1));
    if(!*at || ((*at)->size == size && alike((*at)->name, mark, size))) return at;
  }
  return NULL;
}

item* hsee(unit* self, const char* mark, u64i size, u64i clue) {
  item** at = hpos(self, mark, size, clue);
  return at? *at: NULL;
}

// double the table
static void  hgrow(const mode* m, unit* self) {
  item** pool = self->pool;
  u32i   size = self->size;
  self->size = size? 2 * size: 64;
  self->pool = zero(m, self->size * sizeof(item*));
  for(u32i i = 0; i < size; i++)
    if(pool[i]) *hpos(self, pool[i]->name, pool[i]->size, clue(pool[i]->name, pool[i]->size)) = pool[i];
  m->heap(pool, 0);
}

// the slot of the name, counted as used when it is free: the table grows
// first when that makes it more than 3/4 full
static item** hpos_make(const mode* m, unit* self, const char* mark, u64i size, u64i k) {
  item** at = hpos(self, mark, size, k);
  if(at && *at) return at;
  if(4 * (self->used + 1) > 3 * self->size) {
    hgrow(m, self);
    at = hpos(self, mark, size, k);
  }
  self->used++;
  return at;
}

// the item of the name, made when new. Items are allocated apart from the
// table and never move: metas, words and fixes point to them
static item* hput(const mode* m, unit* self, char* mark, u64i size, u64i k) {
  static item* spare;
  static u32i  left;
  item** at = hpos_make(m, self, mark, size, k);
  if(*at) return *at;
  if(!left) spare = m->heap(NULL, (left = 256) * sizeof(item));
  *at = spare + --left;
  **at = (item){.size = size, .name = mark};
  return *at;
}

// a new item also joins the items of its spelling, in join order, so that
// find visits only the units that have the name. The slot of the spelling in
// mode.names holds the first of them
item* hget(mode* self, u32i at, char* mark, u64i size) {
  u64i  k = clue(mark, size);
  unit* u = self->units + at;
  u32i  used = u->used;
  item* s = hput(self, u, mark, size, k);
  if(u->used == used) return s;
  item** p = hpos_make(self, &self->names, mark, size, k);
  for(; *p && (*p)->unit < at; p = &(*p)->next);
  s->unit = at;
  s->next = *p;
  return *p = s;
}
