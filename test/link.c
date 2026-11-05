// vim: ts=2:sw=2
// link's spec: the units and their names, met without a parse. The tables are
// filled by hand, the files link looks for are mocks of the host, and the
// target only records the calls it binds and the globals it points. fail
// longjmps back here, so errors are checked in process
#include <setjmp.h>
#include <string.h>
#include "../core.h"

static jmp_buf bail;
static char    said[256]; // what fail said
#undef  fail
#define fail(...) (snprintf(said, sizeof said, __VA_ARGS__), longjmp(bail, 1))

#include "../util.c"
#include "../core.c"
#include "../link.c"
#include "test.h"

// what the mocks were asked, ", " between: "scan dir/vec.c", "link 3 9"
static char  trace[1024];
static u32i  traced;

static void  note(char* what, const char* arg, s64i a, s64i b) {
  char* sep = traced? ", ": "";
  if(arg) traced += snprintf(trace + traced, sizeof trace - traced, "%s%s %s", sep, what, arg);
  else traced += snprintf(trace + traced, sizeof trace - traced, "%s%s %lld %lld", sep, what,
                          (long long)a, (long long)b);
  if(traced >= sizeof trace) traced = sizeof trace - 1;
}

static byte  texts[256];
static file* text = (file*)texts;
// where p is in text
static s64i  in(const byte* p) { return p? p - text->head: -1; }

static void  mock_link(const file* t, byte* at, const byte* to) { (void)t; note("link", NULL, in(at), in(to)); }
static void  mock_site(const file* t, byte* glob, s64i dist) { (void)t; note("site", NULL, in(glob), dist); }

// the files there are, by path
static char* there[8];
static u32i  theren;
static file* mock_scan(const char* path) {
  note("scan", path, 0, 0);
  for(u32i i = 0; i < theren; i++) {
    if(strcmp(there[i], path)) continue;
    file* f = malloc(sizeof(file) + 1);
    f->body = f->head;
    f->tail = f->head;
    *f->tail = 0;
    return f;
  }
  return NULL;
}
static bool  hosts; // the host has every shared library
static bool  mock_have(const item* s) { note("have", NULL, s->size, 0); return hosts; }
static void* mock_heap(void* p, size_t size) { if(size) return realloc(p, size); free(p); return NULL; }
static void  mock_take(mode* m, file* f) { (void)m; (void)f; }
static void  mock_load(mode* m, file* f) { (void)m; (void)f; }
static void  mock_pull(mode* m, meta* x) {
  note("pull", NULL, x->name->size, 0);
  x->kind = call; // its member defines it
  if(x->type) ask(m, x->type); // and asks for another
}

static mode  self;

// a fresh front end: the root and main.c in dir, being parsed
static void  start(char* path) {
  free(self.units);
  free(self.asks);
  free(self.hooks);
  memset(&self, 0, sizeof self);
  memset(texts, 0, sizeof texts);
  text->body = text->head + 64;
  text->tail = texts + sizeof texts;
  trace[0] = said[0] = 0;
  traced = theren = hosts = 0;
  self = (mode){
    .name = path, .heap = mock_heap, .scan = mock_scan, .have = mock_have, .text = text,
    .arch = {.link = mock_link, .site = mock_site},
    .form = {NULL, mock_take, os_linux}, .ario = {mock_load, mock_pull},
  };
  nest(&self, (unit){.kind = un_root, .path = path});
  self.unit = nest(&self, (unit){.kind = un_code, .path = path});
}

// the trace is exactly calls, else it is shown
static bool  is(const char* all) {
  if(!strcmp(trace, all)) return 1;
  printf("    want %s\n    got  %s\n", all, trace);
  return 0;
}
// it failed with why
static bool  says(const char* why) {
  if(strstr(said, why)) return 1;
  printf("    want %s\n    got  %s\n", why, said[0]? said: "no error\n");
  return 0;
}

// path, tidy'd, is want
static bool  tidies(const char* path, const char* want) {
  char buf[64];
  u64i n = tidy(strcpy(buf, path), strlen(path));
  if(!strcmp(buf, want) && n == strlen(want)) return 1;
  printf("    %s: want %s, got %s (%llu)\n", path, want, buf, (unsigned long long)n);
  return 0;
}

// link name, a string as written: the unit it joined, 0 when it failed
static u32i  hauls(char* name) {
  u32i n = self.unitn;
  if(setjmp(bail)) return 0;
  haul(&self, hget(&self, self.unit, name, strlen(name)));
  return self.unitn > n? self.unitn - 1: self.unit;
}

// name in the table of unit at meaning x, else nothing
static item* say(u32i at, char* name, meta* x) {
  item* s = hget(&self, at, name, strlen(name));
  if(x) { s->data = x; x->name = s; x->unit = at; }
  return s;
}

// the kinds of meaning, a function at its offset in text
#define DEF  {.kind = call, .addr = text->head + 1}
#define WEAK {.kind = call, .addr = text->head + 2, .weak = 1}
#define PEND {.kind = pend, .ext = 1, .size = VARY}
#define DECL {.kind = call}

spec("link") {
  context("tidy") {
    it("drops empty and . segments") {
      check(tidies("a//b/./c", "a/b/c") && tidies("./a", "a") && tidies("a/.", "a"));
    }
    it("folds a directory and the .. after it") {
      check(tidies("a/b/../c", "a/c") && tidies("a/b/../../c", "c"));
    }
    it("keeps the .. that leave a relative path") {
      check(tidies("../a", "../a") && tidies("a/../../b", "../b") && tidies("../../a/../b", "../../b"));
    }
    it("keeps the root, where .. stays") {
      check(tidies("/a/b", "/a/b") && tidies("/../a", "/a") && tidies("/", "/") && tidies("/a/", "/a"));
    }
    it("drops the last /") {
      check(tidies("a/b/", "a/b") && tidies("a//", "a"));
    }
  }

  context("haul") {
    u32i u;
    it("joins name.c beside the source") {
      start("dir/main.c");
      there[theren++] = "dir/vec.c";
      check((u = hauls("\"vec\"")) == 2 && self.units[u].kind == un_code);
      check(!strcmp(self.units[u].path, "dir/vec.c") && is("scan dir/vec.c"));
    }
    it("tries name.c, name.o, then libname.a beside the source") {
      start("dir/main.c");
      there[theren++] = "dir/libvec.a";
      check((u = hauls("\"vec\"")) == 2 && self.units[u].kind == un_arch);
      check(is("scan dir/vec.c, scan dir/vec.o, scan dir/libvec.a"));
      start("dir/main.c");
      there[theren++] = "dir/vec.o";
      there[theren++] = "dir/libvec.a";
      check((u = hauls("\"vec\"")) == 2 && self.units[u].kind == un_objs);
    }
    it("puts lib before the last segment of the name") {
      start("main.c");
      there[theren++] = "sub/libvec.a";
      check(hauls("\"sub/vec\"") == 2 && !strcmp(self.units[2].path, "sub/libvec.a"));
    }
    it("tidies the path, so a file joins once") {
      start("dir/main.c");
      there[theren++] = "vec.c";
      check(hauls("\"../vec\"") == 2 && !strcmp(self.units[2].path, "vec.c"));
      check(hauls("\"./../dir/../vec\"") == 1 && self.unitn == 3);
    }
    it("looks in the library directories: sources and objects, the host, archives") {
      start("dir/main.c");
      self.dirs[self.dirn++] = "lib";
      self.dirs[self.dirn++] = "/usr/";
      check(!hauls("\"vec\"") && says("dir/main.c:1: cannot find \"vec\""));
      check(is("scan dir/vec.c, scan dir/vec.o, scan dir/libvec.a, scan lib/vec.c, scan lib/vec.o, "
               "scan /usr/vec.c, scan /usr/vec.o, have 5 0, scan lib/libvec.a, scan /usr/libvec.a"));
    }
    it("takes the first there") {
      start("dir/main.c");
      self.dirs[self.dirn++] = "lib";
      self.dirs[self.dirn++] = "usr";
      there[theren++] = "usr/vec.o";
      there[theren++] = "usr/libvec.a";
      check((u = hauls("\"vec\"")) == 2 && !strcmp(self.units[u].path, "usr/vec.o"));
    }
    it("takes the host's library before an archive in a directory") {
      start("dir/main.c");
      self.dirs[self.dirn++] = "lib";
      there[theren++] = "lib/libvec.a";
      hosts = 1;
      check(hauls("\"vec\"") == 1 && self.unitn == 2);
      meta* lib = self.units[1].from;
      check(lib && lib->kind == dlib && lib->name->size == 5);
      check(is("scan dir/vec.c, scan dir/vec.o, scan dir/libvec.a, scan lib/vec.c, scan lib/vec.o, have 5 0"));
    }
    it("but not when it is not running") {
      start("dir/main.c");
      self.have = NULL;
      self.dirs[self.dirn++] = "lib";
      there[theren++] = "lib/libvec.a";
      check((u = hauls("\"vec\"")) == 2 && self.units[u].kind == un_arch);
    }
  }

  context("units") {
    it("nests each unit at line 1, growing the list") {
      start("main.c");
      for(u32i i = 2; i < 40; i++) check(nest(&self, (unit){.path = "x.c", .line = 9}) == i);
      check(self.unitn == 40 && self.unitc >= 40 && self.units[39].line == 1);
      check(!strcmp(self.units[1].path, "main.c") && self.units[0].kind == un_root);
    }
    it("knows a name in two tables by its spelling") {
      start("main.c");
      nest(&self, (unit){.path = "b.c"});
      item* a = say(1, "f", NULL), *b = say(2, "f", NULL), *c = say(2, "g", NULL);
      check(a != b && akin(a, b) && akin(a, a) && !akin(a, c));
    }
  }

  context("heft") {
    it("weighs how firmly a meaning defines its name") {
      meta lib = {.kind = dlib};
      check(heft(&(meta){.kind = call, .addr = text->head}) == 4 && heft(&(meta){.kind = glob}) == 4);
      check(heft(&(meta){.kind = call, .addr = text->head, .weak = 1}) == 3);
      check(heft(&(meta){.kind = pend}) == 2);
      check(heft(&(meta){.kind = call}) == 1 && heft(&(meta){.kind = call, .addr = text->head, .lib = &lib}) == 1);
      check(heft(&(meta){.kind = call, .addr = text->head, .own = 1}) == 0 && heft(&(meta){.kind = data}) == 0);
    }
  }

  context("find") {
    item* s;
    // main.c, then a.c, b.c and c.c
    #define UNITS start("main.c"); for(u32i k = 0; k < 3; k++) nest(&self, (unit){.path = "x.c"})
    it("takes the unit's own meaning") {
      UNITS;
      meta own = DECL, other = DEF;
      s = say(1, "f", &own);
      say(2, "f", &other);
      check(find(&self, 1, s) == &own);
    }
    it("takes the first definition of the others, in join order") {
      UNITS;
      meta b = DEF, c = DEF;
      s = say(1, "f", NULL);
      say(3, "f", &c);
      say(2, "f", &b);
      check(find(&self, 1, s) == &b && find(&self, 0, s) == &b);
    }
    it("takes a definition over a weak one, a weak one over an archive name, that over a declaration") {
      UNITS;
      meta d = DECL, p = PEND, w = WEAK, x = DEF;
      s = say(1, "f", NULL);
      say(2, "f", &d);
      check(find(&self, 1, s) == &d);
      say(3, "f", &p);
      check(find(&self, 1, s) == &p);
      say(4, "f", &w);
      check(find(&self, 1, s) == &w);
      say(2, "f", &x);
      check(find(&self, 1, s) == &x);
    }
    it("takes another's definition over its own weak one, not another weak one") {
      UNITS;
      meta own = WEAK, w = WEAK, x = DEF;
      s = say(1, "f", &own);
      say(2, "f", &w);
      check(find(&self, 1, s) == &own);
      say(3, "f", &x);
      check(find(&self, 1, s) == &x);
    }
    it("hides another's statics") {
      UNITS;
      meta st = DEF;
      st.own = 1;
      s = say(1, "f", NULL);
      say(2, "f", &st);
      check(!find(&self, 1, s) && find(&self, 2, say(2, "f", NULL)) == &st);
    }
    it("takes the builtin of the root last") {
      UNITS;
      meta root = {.kind = base, .size = 4}, d = DECL;
      say(0, "int", &root);
      s = say(1, "int", NULL);
      check(find(&self, 1, s) == &root && find(&self, 0, s) == &root);
      say(2, "int", &d);
      check(find(&self, 1, s) == &d);
    }
  }

  context("archives") {
    item* s;
    it("asks once for each archive name") {
      start("main.c");
      meta f = PEND, g = PEND;
      say(1, "f", &f);
      say(1, "g", &g);
      check(ask(&self, &f) == 0 && ask(&self, &g) == 1 && ask(&self, &f) == 0 && self.askn == 2);
    }
    it("reels in the members asked for, and those they ask for") {
      start("main.c");
      meta f = PEND, gg = PEND, h = PEND;
      say(1, "f", &f);
      say(1, "gg", &gg);
      say(1, "hhh", &h);
      f.type = &h; // pulling f asks for h
      ask(&self, &f);
      ask(&self, &gg);
      reel(&self);
      check(is("pull 1 0, pull 2 0, pull 3 0") && self.askd == 3);
      reel(&self);
      check(is("pull 1 0, pull 2 0, pull 3 0"));
    }
    it("skips a name pulled with another member's") {
      start("main.c");
      meta f = PEND, g = PEND;
      say(1, "f", &f);
      s = say(1, "gg", &g);
      ask(&self, &f);
      ask(&self, &g);
      s->data = NULL;
      reel(&self);
      check(is("pull 1 0") && self.askd == 2);
    }
  }

  context("libraries") {
    it("racks a library once for its spelling") {
      start("main.c");
      nest(&self, (unit){.path = "b.c"});
      item* s = say(1, "\"c\"", NULL);
      meta* x = rack(&self, s);
      check(x->kind == dlib && x->name == s && s->data == x && x->unit == 1);
      self.unit = 2;
      check(rack(&self, say(2, "\"c\"", NULL)) == x && self.defn == 1);
    }
    it("gives an import its slot when first used") {
      start("main.c");
      meta f = {.kind = call}, g = {.kind = call};
      check(port(&self, &f) == (SLOT | 0) && port(&self, &g) == (SLOT | 1));
      check(port(&self, &f) == (SLOT | 0) && self.impn == 2 && self.imps[1] == &g);
    }
    it("fails past the import slots") {
      start("main.c");
      meta f[257] = {0};
      if(!setjmp(bail)) for(u32i i = 0; i < 257; i++) port(&self, f + i);
      check(self.impn == 256 && says("main.c:1: too many imports"));
    }
  }

  context("tie") {
    meta  f = DEF, g = {.kind = call, .addr = text->head + 3, .size = 2}, ext = {.kind = call, .addr = text->head + 4, .ext = 1, .size = VARY};
    meta  lng = {.kind = base, .size = 8};
    // a call at in text to name, from unit at its line
    #define FIX(off, s, u, l, ...) \
      (self.link[self.fixs++] = (fix){.at = text->head + (off), .name = (s), .unit = (u), .line = (l), __VA_ARGS__})
    it("binds the calls whose function is known, keeps the others in order") {
      start("main.c");
      say(1, "f", &f);
      say(1, "g", &g);
      FIX(10, say(1, "f", NULL), 1, 7);
      FIX(20, say(1, "h", NULL), 1, 7);
      FIX(30, say(1, "g", NULL), 1, 7, .argc = 2);
      FIX(40, say(1, "k", NULL), 1, 7);
      tie(&self, 0, 0);
      check(is("link 10 1, link 30 3") && self.fixs == 2);
      check(self.link[0].at == text->head + 20 && self.link[1].at == text->head + 40);
    }
    it("leaves the calls before from") {
      start("main.c");
      say(1, "f", &f);
      FIX(10, say(1, "f", NULL), 1, 7);
      FIX(20, say(1, "f", NULL), 1, 7);
      tie(&self, 1, 0);
      check(is("link 20 1") && self.fixs == 1 && self.link[0].at == text->head + 10);
    }
    it("binds a call the way its unit sees the name, C code's as all do") {
      start("main.c");
      nest(&self, (unit){.path = "b.c"});
      meta own = {.kind = call, .addr = text->head + 5};
      say(1, "f", &f);
      say(2, "f", &own);
      say(2, "e", &ext);
      FIX(10, say(1, "f", NULL), 1, 7);
      FIX(20, say(2, "f", NULL), 2, 7);
      FIX(30, say(1, "e", NULL), 1, 7, .ext = 1);
      tie(&self, 0, 1);
      check(is("link 10 1, link 20 5, link 30 4") && self.fixs == 0);
    }
    it("fails at the call when the pass ends with no function") {
      start("main.c");
      nest(&self, (unit){.path = "b.c"});
      FIX(10, say(2, "h", NULL), 2, 12);
      if(!setjmp(bail)) tie(&self, 0, 1);
      check(says("b.c:12: no function 'h'"));
    }
    it("checks the arguments, and that a call not declared returns an int") {
      start("main.c");
      say(1, "g", &g);
      FIX(10, say(1, "g", NULL), 1, 7, .argc = 1);
      if(!setjmp(bail)) tie(&self, 0, 1);
      check(says("main.c:7: 'g' takes 2 arguments, got 1"));
      start("main.c");
      meta l = DEF;
      l.type = &lng;
      say(1, "l", &l);
      FIX(10, say(1, "l", NULL), 1, 7);
      if(!setjmp(bail)) tie(&self, 0, 1);
      check(says("main.c:7: 'l' does not return an int, define it before the call"));
    }
    it("leaves the unit and its line as they were") {
      start("main.c");
      nest(&self, (unit){.path = "b.c"});
      self.units[1].line = 3;
      say(2, "f", &f);
      FIX(10, say(2, "f", NULL), 2, 9);
      tie(&self, 0, 0);
      check(self.unit == 1 && self.units[1].line == 3 && self.fixs == 0);
    }
  }

  context("dock") {
    meta  g = {.kind = glob, .slot = 8}, f = DEF, imp = {.kind = call};
    meta  lib = {.kind = dlib};
    byte  datas[64];
    file* data = (file*)datas;
    #define LAID(n) (data->body = data->head + (n), data->tail = datas + sizeof datas, self.data = data)
    it("finds a global by name, else fails") {
      start("main.c");
      imp.lib = &lib;
      say(1, "g", &g);
      say(1, "f", &f);
      say(1, "imp", &imp);
      seat a = goal(&self, 1, say(1, "g", NULL), 0), b = goal(&self, 1, say(1, "f", NULL), 1);
      check(!a.at && a.off == 8 && b.at == text->head + 1 && !b.off);
      if(!setjmp(bail)) goal(&self, 1, say(1, "f", NULL), 0);
      check(says("undefined: f"));
      said[0] = 0;
      if(!setjmp(bail)) goal(&self, 1, say(1, "imp", NULL), 1);
      check(says("undefined: imp"));
    }
    it("poses each seat from the start of text") {
      start("main.c");
      say(1, "g", &g);
      self.zero = 13;
      ask(&self, &g);
      check(pose(&self, (seat){text->head + 5, 0}, 100, 200) == 5);
      check(pose(&self, (seat){NULL, 12}, 100, 200) == 112);
      check(pose(&self, (seat){NULL, ZERO | 12}, 100, 200) == 212);
      check(pose(&self, (seat){NULL, SLOT | 2}, 100, 200) == 200 + 16 + 16);
      check(pose(&self, (seat){NULL, WANT | 0}, 100, 200) == 108);
    }
    it("aligns data, then bss, on the largest alignment asked") {
      start("main.c");
      check(gap(&self) == 16);
      self.most = 64;
      check(gap(&self) == 64);
    }
    it("points each global of the source at its seat") {
      start("main.c");
      LAID(20);
      self.aims[self.seats++] = (seat){text->head + 70, 8};
      self.aims[self.seats++] = (seat){text->head + 80, ZERO | 4};
      dock(&self, 1000, 0);
      // data at 1000, bss after its 20 bytes, aligned: 1032
      check(is("site 70 938, site 80 956"));
    }
    it("patches the references of object code, near or wide") {
      start("main.c");
      LAID(16);
      say(1, "f", &f);
      self.hooks = malloc(3 * sizeof(hook));
      self.hooks[self.hookn++] = (hook){.site = {text->head + 70, 0}, .name = say(1, "f", NULL), .unit = 1, .add = -4};
      self.hooks[self.hookn++] = (hook){.site = {NULL, 8}, .aim = {NULL, ZERO | 4}, .wide = 1, .add = 2};
      self.hooks[self.hookn++] = (hook){.site = {NULL, 0}, .aim = {text->head + 3, 0}};
      dock(&self, 512, 0x400000);
      s32i rel, near;
      u64i wide;
      memcpy(&rel, text->head + 70, 4);
      memcpy(&wide, data->head + 8, 8);
      memcpy(&near, data->head, 4);
      check(rel == 1 - 4 - 70 && wide == 0x400000 + 512 + 16 + 4 + 2 && near == 3 - 512);
    }
    it("makes a weak reference to nothing address 0") {
      start("main.c");
      LAID(16);
      self.hooks = malloc(2 * sizeof(hook));
      self.hooks[self.hookn++] = (hook){.site = {NULL, 0}, .name = say(1, "w", NULL), .unit = 1, .weak = 1, .wide = 1};
      // lea rcx, [rip+w] -> mov rcx, 0
      copy(text->head + 100, "\x48\x8d\x0d", 3);
      self.hooks[self.hookn++] = (hook){.site = {text->head + 103, 0}, .name = say(1, "w", NULL), .unit = 1, .weak = 1, .load = 1};
      dock(&self, 512, 0x400000);
      u64i wide;
      memcpy(&wide, data->head, 8);
      check(wide == 0 && alike(text->head + 100, "\x48\xc7\xc1\0\0\0\0", 7));
    }
    it("fails on a reference too far") {
      start("main.c");
      LAID(16);
      self.hooks = malloc(sizeof(hook));
      self.hooks[self.hookn++] = (hook){.site = {text->head + 70, 0}, .aim = {NULL, 0}};
      if(!setjmp(bail)) dock(&self, (u64i)1 << 32, 0);
      check(says("object reference too far"));
    }
  }

  context("library") {
    it("signs the functions with a body and the globals, defined or weak") {
      start("main.c");
      nest(&self, (unit){.path = "b.c"});
      meta f = DEF, w = WEAK, g = {.kind = glob}, p = PEND, d = DECL, st = DEF, shadow = DEF;
      st.own = 1;
      say(1, "f", &f);
      say(2, "f", &shadow); // f means main.c's
      say(1, "w", &w);
      say(2, "g", &g);
      say(1, "p", &p);
      say(1, "d", &d);
      say(2, "st", &st);
      meta** all;
      u32i   n = sign(&self, &all), hit = 0;
      for(u32i i = 0; i < n; i++) hit |= (all[i] == &f) | (all[i] == &w) << 1 | (all[i] == &g) << 2;
      free(all);
      check(n == 3 && hit == 7);
    }
    it("knits the references of object code, named while nothing defines them") {
      start("main.c");
      meta f = DEF, g = {.kind = glob, .slot = 8};
      say(1, "f", &f);
      say(1, "g", &g);
      self.hooks = malloc(4 * sizeof(hook));
      self.hooks[self.hookn++] = (hook){.site = {text->head + 70, 0}, .name = say(1, "f", NULL), .unit = 1, .add = -4};
      self.hooks[self.hookn++] = (hook){.site = {NULL, 0}, .name = say(1, "g", NULL), .unit = 1, .wide = 1};
      self.hooks[self.hookn++] = (hook){.site = {text->head + 80, 0}, .name = say(1, "u", NULL), .unit = 1, .weak = 1, .got = 1};
      self.hooks[self.hookn++] = (hook){.site = {NULL, 8}, .aim = {text->head + 3, 0}, .wide = 1};
      cord* c;
      u32i  n = knit(&self, &c);
      check(n == 4 && c[0].kind == cd_rel && !c[0].name && c[0].aim.at == text->head + 1 && c[0].add == -4);
      check(c[1].kind == cd_wide && !c[1].name && !c[1].aim.at && c[1].aim.off == 8);
      check(c[2].name == say(1, "u", NULL) && c[2].weak && c[2].got);
      check(!c[3].name && c[3].aim.at == text->head + 3 && c[3].site.off == 8);
      free(c);
    }
    it("knits a global of the source asked from an archive to it once pulled, else by name") {
      start("main.c");
      meta g = {.kind = glob, .slot = 24}, h = PEND;
      say(1, "g", &g);
      say(1, "h", &h);
      ask(&self, &g);
      ask(&self, &h);
      self.aims[self.seats++] = (seat){text->head + 70, WANT | 0};
      self.aims[self.seats++] = (seat){text->head + 80, WANT | 1};
      self.aims[self.seats++] = (seat){text->head + 90, 4};
      cord* c;
      u32i  n = knit(&self, &c);
      check(n == 3 && c[0].kind == cd_glob && !c[0].name && c[0].aim.off == 24);
      check(c[1].name == say(1, "h", NULL) && c[1].aim.off == (WANT | 1));
      check(!c[2].name && c[2].aim.off == 4 && c[2].site.at == text->head + 90);
      free(c);
    }
  }
}
