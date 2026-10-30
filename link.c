// vim: ts=2:sw=2
#include "core.h"

// path without empty and . segments, and with dir/.. folded, so that a source
// file has one name: its size
u64i  tidy(char* path, u64i size) {
  u64i n = 0, keep = 0; // bytes kept, the first keep of them a root / or ../s
  if(size && path[0] == '/') n = keep = 1;
  for(u64i i = n, j; i < size; i = j + 1) {
    for(j = i; j < size && path[j] != '/'; j++);
    if(j == i || (j - i == 1 && path[i] == '.')) continue;
    if(j - i == 2 && path[i] == '.' && path[i + 1] == '.') {
      if(n > keep) { for(n--; n > keep && path[n - 1] != '/'; n--); continue; }
      if(keep == 1) continue; // /.. is /
      keep = n + 3;
    }
    copy((byte*)path + n, path + i, j - i);
    n += j - i;
    path[n++] = '/';
  }
  if(n > 1) n--; // the last /
  path[n] = 0;
  return n;
}

// link "name": the first of name.c, name.o and libname.a beside this source,
// then libname.a in the library directories (mode.dirs)
void  haul(mode* self, item* s) {
  static char* const ends[] = {".c", ".o", ".a"};
  char* from = home(self)->path;
  u32i  dir = 0, n = quote(s, NULL) - 1;
  for(u32i i = 0; from[i]; i++) if(from[i] == '/') dir = i + 1;
  for(u32i k = 0; k < 3 + self->dirn; k++) {
    char* at = k < 3? from: self->dirs[k - 3];
    u32i  d = k < 3? dir: slen(at), b = 0, lib = k > 1? 3: 0;
    char* path = malloc(d + n + 8); // '/', "lib", the end and its NUL
    if(!path) fail("out of memory\n");
    byte* p = copy((byte*)path, at, d);
    if(k > 2 && d && at[d - 1] != '/') { *p++ = '/'; d++; } // -L dir
    quote(s, p);
    if(lib) { // "lib" before the last segment
      for(u32i i = 0; i < n; i++) if(p[i] == '/') b = i + 1;
      for(u32i i = n; i-- > b; ) p[i + 3] = p[i];
      copy(p + b, "lib", 3);
    }
    copy(p + n + lib, ends[k < 3? k: 2], 3);
    if(land(self, path, d + n + lib + 2, NULL, un_code + (k < 3? k: 2))) return;
    free(path);
  }
  flaw(self, "cannot find %.*s\n", nick(s));
}

// a file named on the command line, joined before the source
void  bolt(mode* self, char* name, file* code, byte kind) {
  self->objs = swell(self->objs, self->objn, &self->objc, sizeof(unit), 16);
  self->objs[self->objn++] = (unit){.path = name, .code = code, .kind = kind};
}

// x added to the units: its index
u32i  nest(mode* self, unit x) {
  self->units = swell(self->units, self->unitn, &self->unitc, sizeof(unit), 16);
  self->units[self->unitn] = x;
  return self->unitn++;
}

// the same name, maybe in the tables of two units
bool  akin(item* x, item* y) {
  return x == y || (x->size == y->size && alike(x->name, y->name, x->size));
}

// how x defines its name to the other units: 0 not at all (a static, a
// local), 1 declared (an import, a prototype of C code), 2 in an archive
// member not pulled yet, 3 weak (or tentative), 4 defined
u32i  heft(meta* x) {
  if(x->own || x->kind == data) return 0;
  if(x->kind == pend) return 2;
  if(x->lib || (x->kind == call && !x->addr)) return 1;
  return x->weak? 3: 4;
}

// what name, in the table of unit at, means there: its own meaning unless
// that is weak, else the public one of the other units in join order: the
// first definition, else the first weak one, else the first archive name,
// else the first declaration. Last the builtins of the root. At 0: the public
// meaning of the spelling of name. Only the units with the name are visited
meta* find(mode* self, u32i at, item* name) {
  meta* x = at? name->data: NULL;
  if(x && !x->weak) return x;
  item* all = hsee(&self->names, name->name, name->size, clue(name->name, name->size));
  item* s = all? all->data: NULL, *root = s && !s->unit? s: NULL;
  u32i  top = x? 3: 0;
  for(; s; s = s->next) {
    meta* m = s->unit && s->unit != at? s->data: NULL;
    u32i  h = m? heft(m): 0;
    if(h == 4) return m;
    if(h > top) { x = m; top = h; }
  }
  if(x) return x;
  return root? root->data: NULL;
}

// ask for the member that defines archive name x: it is pulled by reel. The
// index of the ask, for a seat (WANT)
u32i  ask(mode* self, meta* x) {
  u32i i = 0;
  for(; i < self->askn && self->asks[i] != x->name; i++);
  if(i < self->askn) return i;
  self->asks = swell(self->asks, self->askn, &self->askc, sizeof(item*), 64);
  self->asks[self->askn] = x->name;
  return self->askn++;
}

// pull the members asked for, and those their undefined names ask for in
// turn. Only between functions or when the run ends: take appends to text
void  reel(mode* self) {
  for(; self->askd < self->askn; self->askd++) {
    meta* x = self->asks[self->askd]->data; // NULL once its member is in
    if(x && x->kind == pend) ario_pull(self, x);
  }
}

// 1 + the index of library s, added to libs when new
u32i  rack(mode* self, item* s) {
  u32i i = 0;
  for(; i < self->libn && !akin(self->libs[i], s); i++);
  if(i == self->libn) {
    if(self->libn == sizeof self->libs / sizeof *self->libs) flaw(self, "too many libraries\n");
    self->libs[self->libn++] = s;
  }
  return i + 1;
}

// the seat of the slot of import x, made when it is first used
u32i  port(mode* self, meta* x) {
  if(!x->slot) {
    if(self->impn == sizeof self->imps / sizeof *self->imps) flaw(self, "too many imports\n");
    self->imps[self->impn++] = x;
    x->slot = self->impn;
  }
  return SLOT | (x->slot - 1);
}

// bind the forward calls from fix `from` on whose function is known, as
// their unit sees it, or, for C code, its definition: the rest stay pending,
// or fail when all sources are parsed (last)
void  tie(mode* self, u32i from, bool last) {
  u32i keep = from, unit = self->unit;
  u64i line = self->line;
  for(fix* f = self->link + from; f < self->link + self->fixs; f++) {
    meta* func = find(self, f->ext? 0: f->unit, f->name);
    if(!last && (!func || func->kind != call || !func->addr)) { self->link[keep++] = *f; continue; }
    self->unit = f->unit;
    self->line = f->line;
    bind(self, f->at, f->name, func, f->argc, f->ext);
    if(!f->ext && wide(func->type)) // its result was taken as an int
      flaw(self, "'%.*s' does not return an int, define it before the call\n", nick(f->name));
  }
  self->fixs = keep;
  self->unit = unit;
  self->line = line;
}

// m is a global, or with code a function with a body, but not an import
static bool real(meta* m, bool code) {
  return m && !m->lib && (m->kind == glob || (code && m->kind == call && m->addr));
}

// where what name, in the table of unit at, means when the run ends is
seat  goal(mode* self, u32i at, item* name, bool code) {
  meta* m = find(self, at, name);
  if(!real(m, code)) fail("undefined: %.*s\n", nick(name));
  return m->kind == call? (seat){m->addr, 0}: (seat){NULL, m->slot};
}

// where s is from the start of text, with data at at, bss at bss and the
// import slots after it. Ask i is for a global that a member pulled since
// defines
u64i  pose(mode* self, seat s, u64i at, u64i bss) {
  return s.at? (u64i)(s.at - self->text->head): s.off & ZERO? bss + (s.off & ~ZERO):
         s.off & SLOT? bss + ((self->zero + 7) & -8) + 8 * (s.off & ~SLOT):
         s.off & WANT? pose(self, goal(self, 0, self->asks[s.off & ~WANT], 0), at, bss): at + s.off;
}

u64i  gap(mode* self) {
  return self->most > 16? self->most: 16;
}

void  dock(mode* self, u64i at, u64i org) {
  u64i bss = at + ((self->data->body - self->data->head + gap(self) - 1) & -gap(self));
  for(seat* s = self->aims; s < self->aims + self->seats; s++)
    emit(self, site, s->at, pose(self, (seat){NULL, s->off}, at, bss)
                            - (s->at - self->text->head));
  for(hook* h = self->hooks; h < self->hooks + self->hookn; h++) {
    byte* site = h->site.at? h->site.at: self->data->head + h->site.off;
    bool  null = h->weak && !real(find(self, h->unit, h->name), 1);
    if(null && h->load) { // lea r, [rip+x] -> mov r, 0: rex.r moves to rex.b
      site[-3] = (site[-3] & ~5) | (site[-3] >> 2 & 1);
      site[-1] = 0xc0 | (site[-1] >> 3 & 7);
      site[-2] = 0xc7;
      copy(site, &(s32i){0}, 4);
      continue;
    }
    s64i  to = null? -org: pose(self, h->name? goal(self, h->unit, h->name, 1): h->aim, at, bss);
    if(h->wide) {
      u64i a = org + to + h->add;
      copy(site, &a, sizeof(a));
      continue;
    }
    s64i  rel = to + h->add - pose(self, h->site, at, bss);
    s32i  r   = rel;
    if(r != rel) fail("object reference too far\n");
    copy(site, &r, sizeof(r));
  }
}
