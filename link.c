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
  u32i  dir = 0, room = 0, n = quote(s, NULL) - 1;
  for(u32i i = 0; self->name[i]; i++) if(self->name[i] == '/') dir = i + 1;
  for(u32i k = 0, d; k < self->dirn; k++) {
    for(d = 0; self->dirs[k][d]; d++);
    if(d > room) room = d;
  }
  char* path = malloc((dir > room? dir: room) + n + 40);
  if(!path) fail("out of memory\n");
  for(u32i k = 0; k < 3 + self->dirn; k++) {
    char* at = k < 3? self->name: self->dirs[k - 3];
    u32i  d = dir, b = 0, lib = k > 1? 3: 0;
    if(k > 2) for(d = 0; at[d]; d++);
    byte* p = copy((byte*)path, at, d);
    if(k > 2 && d && at[d - 1] != '/') { *p++ = '/'; d++; } // -L dir
    quote(s, p);
    if(lib) { // "lib" before the last segment
      for(u32i i = 0; i < n; i++) if(p[i] == '/') b = i + 1;
      for(u32i i = n; i-- > b; ) p[i + 3] = p[i];
      copy(p + b, "lib", 3);
    }
    copy(p + n + lib, ends[k < 3? k: 2], 3);
    u64i  size = tidy(path, d + n + lib + 2);
    file* f = look(path);
    if(!f) continue;
    if(!join(self, path, size, f)) free(path);
    return;
  }
  flaw(self, "cannot find %.*s\n", nick(s));
}

// objects are loaded before the sources, which call them like any function
// defined before them
void  bolt(mode* self, char* name, file* code) {
  if(self->objn == self->objc) {
    self->objc = self->objc * 2 + 16;
    self->objs = realloc(self->objs, self->objc * sizeof(item));
    if(!self->objs) fail("out of memory\n");
  }
  self->objs[self->objn++] = (item){.name = name, .data = code};
}

// take the objects from index from on into the image
void  stow(mode* self, u32i from) {
  char* name = self->name;
  for(item* o = self->objs + from; o < self->objs + self->objn; o++) {
    if(!self->form.take) fail("%s: no objects in this form\n", o->name);
    self->name = o->name;
    self->form.take(self, o->data);
  }
  self->name = name;
}

// hide the static names of source file path, or show them again
void  veil(mode* self, char* path, bool show) {
  for(meta* x = self->pile; x < self->pile + self->top; x++) {
    if(!x->own || x->path != path) continue;
    if(show) { x->hide = x->name->data; x->name->data = x; }
    else if(x->name->data == x) x->name->data = x->hide;
  }
}

// 1 + the index of library s, added to libs when new
u32i  rack(mode* self, item* s) {
  u32i i = 0;
  for(; i < self->libn && self->libs[i] != s; i++);
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

// bind the forward calls from fix `from` on whose function is known: the rest
// stay pending, or fail when all sources are parsed (last)
void  tie(mode* self, u32i from, bool last) {
  u32i keep = from;
  for(fix* f = self->link + from; f < self->link + self->fixs; f++) {
    meta* func = f->name->data;
    while(func && func->own && func->path != f->path) func = func->hide; // static elsewhere
    if(!last && (!func || func->kind != call)) { self->link[keep++] = *f; continue; }
    self->name = f->path;
    self->line = f->line;
    bind(self, f->at, f->name, func, f->argc);
    if(wide(func->type)) // its result was taken as an int
      flaw(self, "'%.*s' does not return an int, define it before the call\n", nick(f->name));
  }
  self->fixs = keep;
}

// where s is from the start of text, with data at at, bss at bss and the
// import slots after it
u64i  pose(mode* self, seat s, u64i at, u64i bss) {
  return s.at? (u64i)(s.at - self->text->head): s.off & ZERO? bss + (s.off & ~ZERO):
         s.off & SLOT? bss + ((self->zero + 7) & -8) + 8 * (s.off & ~SLOT): at + s.off;
}

void  dock(mode* self, u64i at) {
  u64i bss = at + ((self->data->body - self->data->head + 15) & -16);
  for(seat* s = self->aims; s < self->aims + self->seats; s++)
    emit(self, site, s->at, pose(self, (seat){NULL, s->off}, at, bss)
                            - (s->at - self->text->head));
  for(hook* h = self->hooks; h < self->hooks + self->hookn; h++) {
    seat  aim = h->aim;
    meta* m   = h->name? h->name->data: NULL;
    if(h->name && (!m || (m->kind != glob && (m->kind != call || !m->addr))))
      fail("undefined: %.*s\n", nick(h->name));
    if(m) aim = m->kind == call? (seat){m->addr, 0}: (seat){NULL, m->slot};
    s64i  rel = pose(self, aim, at, bss) + h->add - pose(self, h->site, at, bss);
    s32i  r   = rel;
    if(r != rel) fail("object reference too far\n");
    copy(h->site.at? h->site.at: self->data->head + h->site.off, &r, sizeof(r));
  }
}
