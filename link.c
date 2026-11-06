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
      if(n > keep) { for(n--; n > keep && path[n - 1] != '/'; n--) {} continue; }
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

// the file name with end e (".c", ".o", or ".a" as libname.a) in dir, or
// beside this source when dir is NULL, joined as a unit: 0 when it is not there
static bool nigh(mode* self, const item* s, const char* dir, u32i e) {
  static char* const ends[] = {".c", ".o", ".a"};
  const char* at = dir? dir: home(self)->path;
  u32i  d = 0, n = quote(s, NULL) - 1, b = 0, lib = e == 2? 3: 0;
  if(dir) d = (u32i)slen(dir);
  else for(u32i i = 0; at[i]; i++) if(at[i] == '/') d = i + 1;
  char* path = self->heap(NULL, d + n + 8); // '/', "lib", the end and its NUL
  byte* p = copy((byte*)path, at, d);
  if(dir && d && at[d - 1] != '/') { *p++ = '/'; d++; } // -p dir
  quote(s, p);
  if(lib) { // "lib" before the last segment
    for(u32i i = 0; i < n; i++) if(p[i] == '/') b = i + 1;
    for(u32i i = n; i-- > b; ) p[i + 3] = p[i];
    copy(p + b, "lib", 3);
  }
  copy(p + n + lib, ends[e], 3);
  if(land(self, path, d + n + lib + 2, NULL, (byte)(un_code + e))) return 1;
  self->heap(path, 0);
  return 0;
}

// link "name": the first of name.c, name.o and libname.a beside this source,
// then name.c and name.o in the library directories (mode.dirs), each in
// order, then, when running, the host's shared library name, as load "name"
// takes it, then libname.a in the library directories. Code in a directory
// comes before the host's library, so running and writing a file pick the
// same module
void  haul(mode* self, item* s) {
  for(u32i e = 0; e < 3; e++) if(nigh(self, s, NULL, e)) return;
  for(u32i i = 0; i < self->dirn; i++)
    for(u32i e = 0; e < 2; e++) if(nigh(self, s, self->dirs[i], e)) return;
  if(self->have && self->have(s)) { home(self)->from = rack(self, s); return; }
  for(u32i i = 0; i < self->dirn; i++) if(nigh(self, s, self->dirs[i], 2)) return;
  flaw(self, "cannot find %.*s\n", nick(s));
}

// x added to the units, at its line 1: its index
u32i  nest(mode* self, unit x) {
  self->units = swell(self, self->units, self->unitn, &self->unitc, sizeof(unit), 16);
  self->units[self->unitn] = x;
  self->units[self->unitn].line = 1;
  return self->unitn++;
}

// the same name, maybe in the tables of two units
bool  akin(const item* x, const item* y) {
  return x == y || (x->size == y->size && alike(x->name, y->name, x->size));
}

// how x defines its name to the other units: 0 not at all (a static, a
// local), 1 declared (an import, a prototype of C code), 2 in an archive
// member not pulled yet, 3 weak (or tentative), 4 defined
u32i  heft(const meta* x) {
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
  item* s = hsee(&self->names, name->name, name->size, clue(name->name, name->size));
  item* root = s && !s->unit? s: NULL;
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
  self->asks = swell(self, self->asks, self->askn, &self->askc, sizeof(item*), 64);
  self->asks[self->askn] = x->name;
  return self->askn++;
}

// pull the members asked for, and those their undefined names ask for in
// turn. Only between functions or when the run ends: take appends to text
void  reel(mode* self) {
  for(; self->askd < self->askn; self->askd++) {
    meta* x = self->asks[self->askd]->data; // NULL once its member is in
    if(x && x->kind == pend) self->ario.pull(self, x);
  }
}

// the library named by the string s: one for every unit that names it, made
// when new. It is bound to its spelling like any name, but outside the scopes
// of the pile: the first unit to name it keeps it for the whole run
meta* rack(mode* self, item* s) {
  meta* x = find(self, 0, s);
  if(x) return x;
  if(self->defn == sizeof self->defs / sizeof *self->defs) flaw(self, "too many libraries\n");
  x = mint(self, (meta){.name = s, .kind = dlib, .unit = self->unit});
  s->data = x;
  return x;
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
  u64i line = home(self)->line;
  for(fix* f = self->link + from; f < self->link + self->fixs; f++) {
    const meta* func = find(self, f->ext? 0: f->unit, f->name);
    if(!last && (!func || func->kind != call || !func->addr)) { self->link[keep++] = *f; continue; }
    self->unit = f->unit;
    home(self)->line = f->line;
    bind(self, f->at, f->name, func, f->argc, f->ext);
    if(!f->ext && wide(func->type)) // its result was taken as an int
      flaw(self, "'%.*s' does not return an int, define it before the call\n", nick(f->name));
  }
  self->fixs = keep;
  self->unit = unit;
  home(self)->line = line;
}

// m is a global, or with code a function with a body, but not an import
static bool real(const meta* m, bool code) {
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

u64i  gap(const mode* self) {
  return self->most > 16? self->most: 16;
}

void  dock(mode* self, u64i at, u64i org) {
  u64i bss = at + ((self->data->body - self->data->head + gap(self) - 1) & ~(gap(self) - 1));
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
    s64i  to = null? 0 - org: pose(self, h->name? goal(self, h->unit, h->name, 1): h->aim, at, bss);
    if(h->wide) {
      u64i a = org + to + h->add;
      copy(site, &a, sizeof(a));
      continue;
    }
    s64i  rel = to + h->add - pose(self, h->site, at, bss);
    s32i  r   = (s32i)rel;
    if(r != rel) fail("object reference too far\n");
    copy(site, &r, sizeof(r));
  }
}

// the public definitions of the image, for a library's symbol table: what
// each spelling means to the other units, when it is a function with a body
// or a global, defined or weak, and not an import
u32i  sign(mode* self, meta*** out) {
  meta** all = NULL;
  u32i   n = 0, cap = 0;
  for(u32i i = 0; i < self->names.size; i++) {
    item* s = self->names.pool[i];
    meta* m = s? find(self, 0, s): NULL;
    if(!real(m, 1) || heft(m) < 3) continue;
    all = swell(self, all, n, &cap, sizeof(meta*), 64);
    all[n++] = m;
  }
  *out = all;
  return n;
}

// the references of the image, left for the linker of a library object: the
// globals of the source (mode.aims), then those of object code (mode.hooks).
// A name nothing defines stays a name
u32i  knit(mode* self, cord** out) {
  cord* all = self->heap(NULL, (self->seats + self->hookn) * sizeof(cord) + 1), *c = all;
  for(seat* s = self->aims; s < self->aims + self->seats; s++, c++) {
    *c = (cord){.site = {s->at, 0}, .aim = {NULL, s->off}, .kind = cd_glob};
    if(!(s->off & WANT) || s->off & (SLOT | ZERO)) continue;
    const meta* m = find(self, 0, c->name = self->asks[s->off & ~WANT]);
    if(real(m, 0)) { c->aim = (seat){NULL, m->slot}; c->name = NULL; }
  }
  for(hook* h = self->hooks; h < self->hooks + self->hookn; h++, c++) {
    *c = (cord){.site = h->site, .aim = h->aim, .add = h->add,
                .kind = h->wide? cd_wide: cd_rel, .weak = h->weak, .got = h->got};
    if(!h->name) continue;
    meta* m = find(self, h->unit, h->name);
    if(!real(m, 1)) c->name = h->name;
    else c->aim = m->kind == call? (seat){m->addr, 0}: (seat){NULL, m->slot};
  }
  *out = all;
  return (u32i)(c - all);
}
