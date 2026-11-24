// vim: ts=2:sw=2
#include "core.h"

// drops empty and . segments and folds dir/.., so a source file has one name
u64i  tidy(char* path, u64i size) {
  u64i n = 0, keep = 0; // the first keep bytes are a root / or ../s
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

// joins s with end e (".ot", ".o", or ".a" as libname.a) from dir, or beside
// this source when dir is NULL: 0 when it is not there
static bool nigh(mode* self, const item* s, const char* dir, u32i e) {
  static char* const ends[] = {".ot", ".o", ".a"};
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
  u32i w = e? 2: 3;
  copy(p + n + lib, ends[e], w + 1);
  if(land(self, path, d + n + lib + w, NULL, (byte)(un_code + e))) return 1;
  self->heap(path, 0);
  return 0;
}

// link "otic": the compiler's own functions, for code run at compile time.
// The source below calls them through hooks that rig points at the compiler
// before each # runs; a program keeps them null, so only # code may call them
static const char otic_api[] =
  "// a member of a struct, as member gives it\n"
  "struct field {\n"
  "  char* name;\n"
  "  long  type;   // its type, for members and spell (a bit-field's: its integer type)\n"
  "  int   kind;   // 'i' integer, 'r' real, 'p' pointer, 'f' function pointer,\n"
  "                // 'a' array, 's' struct, 'b' bit-field\n"
  "  int   offset; // bytes from the start of the struct (a bit-field: of its unit)\n"
  "  int   size;   // bytes (a bit-field: of its unit)\n"
  "  int   count;  // elements of an array, else 1\n"
  "  int   bit;    // a bit-field: its first bit in the unit, and its width\n"
  "  int   width;\n"
  "};\n"
  "\n"
  "static int   (*otic_arch)(void);\n"
  "static void  (*otic_emit)(byte* code, int n);\n"
  "static long  (*otic_type)(char* name);\n"
  "static int   (*otic_members)(long t);\n"
  "static int   (*otic_member)(long t, int i, field* f);\n"
  "static char* (*otic_spell)(long t);\n"
  "static int   (*otic_offset)(char* type, char* name);\n"
  "\n"
  "// the cpu the source is compiled for, also when a # runs on another one:\n"
  "// 0 ia64 (x86-64), 1 arm8 (AArch64)\n"
  "int  arch() { return otic_arch(); }\n"
  "\n"
  "// n bytes of machine code for that cpu, placed where the # running this is,\n"
  "// in a function. They run there as a call would: they may change the\n"
  "// registers a C function may, and keep the others and the stack\n"
  "void emit(byte* code, int n) { otic_emit(code, n); }\n"
  "\n"
  "// a type declared before the # by its name (a struct, int, real...), 0 when\n"
  "// there is none. A type is good for the # run that got it\n"
  "long type(char* name) { return otic_type(name); }\n"
  "\n"
  "// the members of struct t, in order (those of a struct member without a\n"
  "// declarator among them); -1 when t is no struct\n"
  "int  members(long t) { return otic_members(t); }\n"
  "\n"
  "// member i of struct t in f: 0, f as it was, past the end\n"
  "int  member(long t, int i, field* f) { return otic_member(t, i, f); }\n"
  "\n"
  "// t as a declaration spells it: \"int\", \"vec*\", \"char[16]\", \"int (*)(int, long)\"\n"
  "// (a function pointer's parameters by kind: int, long or a pointer, real)\n"
  "char* spell(long t) { return otic_spell(t); }\n"
  "\n"
  "// the offset of member name in struct type, as offsetof; an error when\n"
  "// there is none\n"
  "int  offset(char* type, char* name) { return otic_offset(type, name); }\n";

static mode* live; // the mode whose # runs

static int   otic_arch(void) { return (int)live->aim; }

static void  otic_emit(const byte* code, int n) {
  blob* b = &live->spit;
  if(n < 0) fail("emit: %d bytes\n", n);
  if(b->n + (u64i)n > 1u << 24) fail("emit: more than 16 MB of machine code from one #\n");
  if(b->n + (u32i)n > b->c) b->p = live->heap(b->p, b->c = 2 * (b->n + (u32i)n));
  copy(b->p + b->n, code, (u32i)n);
  b->n += (u32i)n;
}

// field of the otic source, as C lays it out
typedef struct { const char* name; u64i type; s32i kind, offset, size, count, bit, width; } field;

// a string for # code, good for the whole run: n bytes and a NUL interned in
// the root's table, where no name of a source has a NUL
static char* otic_word(const char* s, u64i n) {
  char  b[1024];
  if(n >= sizeof b) n = sizeof b - 1;
  copy((byte*)b, s, n);
  b[n] = 0;
  return hget(live, 0, b, n + 1)->name;
}

// what name means where the # is, a type, else NULL
static meta* otic_find(const char* name) {
  u64i  n = slen(name), k = clue(name, n);
  item* s = hsee(home(live), name, n, k);
  meta* x = s? find(live, live->unit, s): (s = hsee(&live->names, name, n, k))? find(live, 0, s): NULL;
  return x && (x->kind == base || x->kind == list)? x: NULL;
}

static u64i  otic_type(const char* name) { return (u64i)(uintptr_t)otic_find(name); }

static s32i  otic_members(u64i t) {
  const meta* x = (const meta*)(uintptr_t)t;
  if(!x || x->kind != list) return -1;
  s32i  n = 0;
  for(const meta* m = x->type; m; m = m->hide) n++;
  return n;
}

static s32i  otic_member(u64i t, s32i i, field* f) {
  const meta* x = (const meta*)(uintptr_t)t;
  if(!x || x->kind != list || i < 0) return 0;
  const meta* m = x->type;
  while(m && i--) m = m->hide;
  if(!m) return 0;
  const meta* y = m->type;
  *f = (field){.name = otic_word(m->name->name, m->name->size), .type = (u64i)(uintptr_t)y,
               .offset = m->slot, .size = (s32i)y->size, .count = 1};
  switch(y->kind) {
    case refs: f->kind = y->type->kind == proc? 'f': 'p'; break;
    case many: f->kind = 'a'; f->count = y->type->size? (s32i)(y->size / y->type->size): 0; break;
    case list: f->kind = 's'; break;
    case bitf: f->kind = 'b'; f->type = (u64i)(uintptr_t)y->type; f->bit = y->slot; f->width = y->longs; break;
    default:   f->kind = y == live->breal? 'r': 'i';
  }
  return 1;
}

static u32i  otic_say(char* b, u32i n, const char* s, u64i size) {
  if(n + size >= 1024) fail("spell: a type too long\n");
  copy((byte*)b + n, s, size);
  return n + (u32i)size;
}

// t as a declaration spells it, at b + n: its end
static u32i  otic_spelt(const meta* t, char* b, u32i n) {
  if(t->kind == bitf) return otic_spelt(t->type, b, n);
  if(t->kind == refs && t->type->kind == proc) { // result (*)(kinds)
    const meta* f = t->type;
    n = otic_say(b, otic_spelt(f->type, b, n), " (*)(", 5);
    for(s32i i = 0; i < f->slot && i < 6; i++) {
      if(i) n = otic_say(b, n, ", ", 2);
      n = f->reals >> i & 1? otic_say(b, n, "real", 4): f->longs >> i & 1? otic_say(b, n, "long", 4): otic_say(b, n, "int", 3);
    }
    if(f->more) n = f->slot? otic_say(b, n, ", ...", 5): otic_say(b, n, "...", 3);
    return otic_say(b, n, ")", 1);
  }
  if(t->kind == refs) return otic_say(b, otic_spelt(t->type, b, n), "*", 1);
  if(t->kind == many) { // the element, then each count, outermost first
    const meta* e = t;
    while(e->kind == many) e = e->type;
    n = otic_spelt(e, b, n);
    for(const meta* a = t; a->kind == many; a = a->type) {
      char d[24];
      n = otic_say(b, n, d, (u64i)snprintf(d, sizeof d, "[%llu]", (unsigned long long)(a->type->size? a->size / a->type->size: 0)));
    }
    return n;
  }
  return t->name? otic_say(b, n, t->name->name, t->name->size): otic_say(b, n, "struct", 6);
}

static char* otic_spell(u64i t) {
  char  b[1024];
  if(!t) return otic_word("", 0);
  return otic_word(b, otic_spelt((const meta*)(uintptr_t)t, b, 0));
}

static s32i  otic_offset(const char* type, const char* name) {
  const meta* x = otic_find(type);
  if(!x || x->kind != list) flaw(live, "offset: no struct '%s' here\n", type);
  u64i  n = slen(name);
  for(const meta* m = x->type; m; m = m->hide)
    if(m->name->size == n && alike(m->name->name, name, n)) return m->slot;
  flaw(live, "offset: no member '%s' in '%s'\n", name, type);
}

// points the hooks of link "otic" at the compiler, in an image of the code
// with data at data and bss at bss, for the # of self about to run
void  rig(mode* self, byte* data, byte* bss) {
  static char* const names[] = {"otic_arch", "otic_emit", "otic_type", "otic_members", "otic_member",
                                "otic_spell", "otic_offset"};
  const u64i fns[] = {(u64i)(uintptr_t)otic_arch, (u64i)(uintptr_t)otic_emit, (u64i)(uintptr_t)otic_type,
                      (u64i)(uintptr_t)otic_members, (u64i)(uintptr_t)otic_member,
                      (u64i)(uintptr_t)otic_spell, (u64i)(uintptr_t)otic_offset};
  live = self;
  if(!self->api) return;
  for(u32i i = 0; i < sizeof fns / sizeof *fns; i++) {
    const meta* x = hget(self, self->api, names[i], slen(names[i]))->data;
    if(!x || x->kind != glob) fail("link \"otic\": no hook %s\n", names[i]);
    u32i  off = (u32i)x->slot;
    copy(off & ZERO? bss + (off & ~ZERO): data + off, &fns[i], sizeof *fns);
  }
}

// link "name": name.ot, name.o, libname.a beside this source, then name.ot and
// name.o in mode.dirs, then libname.a in mode.dirs. Static only: a shared
// library is named by load. "otic" is the compiler's own source (otic_api)
void  haul(mode* self, item* s) {
  if(s->size == 6 && alike(s->name, "\"otic\"", 6)) {
    if(self->api) return;
    u64i  n = sizeof otic_api - 1;
    file* code = self->heap(NULL, sizeof(file) + n);
    code->body = code->head;
    code->tail = code->head + n;
    copy(code->head, otic_api, n + 1);
    char* path = self->heap(NULL, 7);
    copy((byte*)path, "<otic>", 7);
    self->api = self->unitn;
    if(!land(self, path, 6, code, un_code)) fail("link \"otic\": cannot join it\n");
    return;
  }
  for(u32i e = 0; e < 3; e++) if(nigh(self, s, NULL, e)) return;
  for(u32i i = 0; i < self->dirn; i++)
    for(u32i e = 0; e < 2; e++) if(nigh(self, s, self->dirs[i], e)) return;
  for(u32i i = 0; i < self->dirn; i++) if(nigh(self, s, self->dirs[i], 2)) return;
  flaw(self, "cannot find %.*s: link takes a source, an object or an archive, a shared library is a load\n", nick(s));
}

u32i  nest(mode* self, unit x) {
  self->units = swell(self, self->units, self->unitn, &self->unitc, sizeof(unit), 16);
  self->units[self->unitn] = x;
  self->units[self->unitn].line = 1;
  return self->unitn++;
}

bool  akin(const item* x, const item* y) {
  return x == y || (x->size == y->size && alike(x->name, y->name, x->size));
}

// how x defines its name to other units: 0 not at all (static, local), 1
// declared (import, C prototype), 2 in an unpulled archive member, 3 weak (or
// tentative), 4 defined
u32i  heft(const meta* x) {
  if(x->own || x->kind == data) return 0;
  if(x->kind == pend) return 2;
  if(x->lib || (x->kind == call && !x->addr)) return 1;
  return x->weak? 3: 4;
}

// unit at's own meaning of name unless weak, else the other units' in join
// order by heft (first definition, weak, archive name, declaration), else the
// root's builtin. At 0: the public meaning of name's spelling
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

static bool  asked(const void* ctx, u32i i) {
  const mode* self = ((const void**)ctx)[0];
  return self->asks[i] == ((const void**)ctx)[1];
}

// the member defining archive name x is pulled later by reel. The index of
// the ask, for a seat (WANT)
u32i  ask(mode* self, meta* x) {
  const void* ctx[] = {self, x->name};
  u32i* at = dsee(self, &self->askx, (u64i)(uintptr_t)x->name, asked, ctx);
  if(*at) return *at - 1;
  self->asks = swell(self, self->asks, self->askn, &self->askc, sizeof(item*), 64);
  self->asks[self->askn] = x->name;
  *at = self->askn + 1;
  return self->askn++;
}

// only between functions or when the run ends: take appends to text
void  reel(mode* self) {
  for(; self->askd < self->askn; self->askd++) {
    meta* x = self->asks[self->askd]->data; // NULL once its member is in
    if(x && x->kind == pend) self->ario.pull(self, x);
  }
}

// one per library name for all units, bound outside the scopes of the pile:
// the first unit to name it keeps it for the whole run
meta* rack(mode* self, item* s) {
  meta* x = find(self, 0, s);
  if(x) return x;
  x = mint(self, (meta){.name = s, .kind = dlib, .unit = self->unit});
  s->data = x;
  return x;
}

u32i  port(mode* self, meta* x) {
  if(!x->slot) {
    self->imps = swell(self, self->imps, self->impn, &self->impc, sizeof(meta*), 16);
    self->imps[self->impn++] = x;
    x->slot = self->impn;
  }
  return SLOT | (x->slot - 1);
}

// binds forward calls from fix `from` on whose function is now known; the rest
// stay pending, or fail when last. An overloaded name fails: overloads are
// picked by argument types, known only where they are. A function bound by
// name is marked so it may not be overloaded after
void  tie(mode* self, u32i from, bool last) {
  u32i keep = from, unit = self->unit;
  u64i line = home(self)->line;
  for(fix* f = self->link + from; f < self->link + self->fixs; f++) {
    const meta* func = find(self, f->ext? 0: f->unit, f->name);
    if(!last && (!func || func->kind != call || !func->addr)) { self->link[keep++] = *f; continue; }
    self->unit = f->unit;
    home(self)->line = f->line;
    if(!f->ext && func && func->kind == call && plural(func, f->name))
      flaw(self, "'%.*s' is overloaded, define it before the call\n", nick(f->name));
    bind(self, f->at, f->name, func, f->argc, f->ext);
    if(!f->ext && wide(func->type)) // its result was taken as an int
      flaw(self, "'%.*s' does not return an int, define it before the call\n", nick(f->name));
    if(!f->ext && func->name->unit) ((meta*)func)->fore = 1;
  }
  self->fixs = keep;
  self->unit = unit;
  home(self)->line = line;
}

static bool real(const meta* m, bool code) {
  return m && !m->lib && (m->kind == glob || (code && m->kind == call && m->addr));
}

seat  goal(mode* self, u32i at, item* name, bool code) {
  meta* m = find(self, at, name);
  if(!real(m, code)) fail("undefined: %.*s\n", nick(name));
  return m->kind == call? (seat){m->addr, 0}: (seat){NULL, m->slot};
}

// offset of s from text, data at at, bss at bss. Import slots after bss, fore
// bytes before data, or at slotat when set. A WANT seat resolves its ask
u64i  pose(mode* self, seat s, u64i at, u64i bss) {
  return s.at? (u64i)(s.at - self->text->head): s.off & ZERO? bss + (s.off & ~ZERO):
         s.off & SLOT? (self->slotat? self->slotat: self->fore? at - self->fore: bss + ((self->zero + 7) & -8)) + 8 * (s.off & ~SLOT):
         s.off & WANT? pose(self, goal(self, 0, self->asks[s.off & ~WANT], 0), at, bss): at + s.off;
}

u64i  gap(const mode* self) {
  return self->most > 16? self->most: 16;
}

void  dock(mode* self, u64i at, u64i org) {
  u64i bss = self->bssat? self->bssat: at + ((self->data->body - self->data->head + gap(self) - 1) & ~(gap(self) - 1));
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

// a library's exports: defined or weak, not imports. An overload is listed by
// its symbol (dub), not the name it shares
u32i  sign(mode* self, meta*** out) {
  meta** all = NULL;
  u32i   n = 0, cap = 0;
  for(u32i i = 0; i < self->names.size; i++) {
    item* s = self->names.pool[i];
    meta* m = s? find(self, 0, s): NULL;
    if(!real(m, 1) || heft(m) < 3 || !akin(m->name, s)) continue;
    all = swell(self, all, n, &cap, sizeof(meta*), 64);
    all[n++] = m;
  }
  *out = all;
  return n;
}

// the name of a global asked from an archive (aim), when nothing defines it,
// else NULL and aim is where the definition is
static item* want(mode* self, seat* aim) {
  if(aim->at || !(aim->off & WANT) || aim->off & (SLOT | ZERO)) return NULL;
  item* name = self->asks[aim->off & ~WANT];
  const meta* m = find(self, 0, name);
  if(!real(m, 0)) return name;
  *aim = (seat){NULL, m->slot};
  return NULL;
}

// references for a library object's linker: mode.aims, then mode.hooks. An
// undefined name stays a name
u32i  knit(mode* self, cord** out) {
  cord* all = self->heap(NULL, (self->seats + self->hookn) * sizeof(cord) + 1), *c = all;
  for(seat* s = self->aims; s < self->aims + self->seats; s++, c++) {
    *c = (cord){.site = {s->at, 0}, .aim = {NULL, s->off}, .kind = cd_glob};
    c->name = want(self, &c->aim);
  }
  for(hook* h = self->hooks; h < self->hooks + self->hookn; h++, c++) {
    *c = (cord){.site = h->site, .aim = h->aim, .add = h->add,
                .kind = h->wide? cd_wide: cd_rel, .weak = h->weak, .got = h->got};
    if(!h->name) { c->name = want(self, &c->aim); continue; }
    meta* m = find(self, h->unit, h->name);
    if(!real(m, 1)) c->name = h->name;
    else c->aim = m->kind == call? (seat){m->addr, 0}: (seat){NULL, m->slot};
  }
  *out = all;
  return (u32i)(c - all);
}
