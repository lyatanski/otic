// vim: ts=2:sw=2
// ario's spec: archives built in memory, loaded into a front end whose form
// is a mock that records the members it takes. fail longjmps back here, so
// errors are checked in process
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
#include "../ario.c"
#include "test.h"

// the archive being built
static byte  arch[4096];
static u32i  archn;

// a number of w bytes at at, big endian (GNU) or little endian (BSD)
static void  put(byte* at, u32i w, u64i v, bool big) {
  for(u32i i = 0; i < w; i++) at[big? w - 1 - i: i] = (byte)(v >> 8 * i);
}

static void  begin(void) {
  memcpy(arch, "!<arch>\n", 8);
  archn = 8;
}

// a member: its header name, as written, and size bytes of body, padded to an
// even size. Where its header is
static u32i  member(const char* name, const void* body, u32i size) {
  char h[64];
  u32i at = archn;
  snprintf(h, sizeof h, "%-16s%-12s%-6s%-6s%-8s%-10u`\n", name, "0", "0", "0", "644", size);
  memcpy(arch + archn, h, 60);
  memcpy(arch + archn + 60, body, size);
  archn += 60 + size;
  if(archn & 1) arch[archn++] = '\n';
  return at;
}

// a BSD member with a long name, "#1/n": the name, NUL padded to n bytes,
// opens the body
static u32i  longer(const char* name, u32i n, const void* body, u32i size) {
  char h[16];
  byte b[1024] = {0};
  snprintf(h, sizeof h, "#1/%u", n);
  memcpy(b, name, strlen(name));
  memcpy(b + n, body, size);
  return member(h, b, n + size);
}

// the body of a GNU index of w-byte numbers: count, n offsets (0 until
// aimed), the n names. Where its offsets are
static byte* gnu(const char* head, u32i w, u32i n, char* names) {
  byte b[1024] = {0};
  u32i size = w + n * w, len;
  put(b, w, n, 1);
  for(u32i i = 0; i < n; i++, names += len + 1, size += len + 1)
    memcpy(b + size, names, (len = (u32i)strlen(names)) + 1);
  return arch + member(head, b, size) + 60 + w;
}

// the body of a BSD index of w-byte numbers: bytes of the pairs, the (name,
// offset) pairs, bytes of the names, the names. With a long header name when
// head is "#1/". Where its pairs are
static byte* bsd(const char* head, u32i w, u32i n, const char* names) {
  byte b[1024] = {0};
  u32i strs = 0, len;
  put(b, w, 2 * w * n, 0);
  for(u32i i = 0; i < n; i++, strs += len + 1) {
    put(b + w + 2 * w * i, w, strs, 0);
    memcpy(b + 2 * w + 2 * w * n + strs, names + strs, (len = (u32i)strlen(names + strs)) + 1);
  }
  put(b + w + 2 * w * n, w, strs, 0);
  u32i size = 2 * w + 2 * w * n + strs;
  if(!alike(head, "#1/", 3)) return arch + member(head, b, size) + 60 + w;
  return arch + longer(head + 3, 20, b, size) + 60 + 20 + w;
}

// the offsets of an index: name i is defined by the member at at[i]
static void  point(byte* offs, u32i w, u32i stride, bool big, u32i n, const u32i* at) {
  for(u32i i = 0; i < n; i++) put(offs + i * stride + (big? 0: w), w, at[i], big);
}

// the front end: the root and the archive, its members taken by the mock form
static mode  self;
// a member taken: its path, kind and body, whether it was NUL ended, at the
// start of its file and apart from the archive (unit 1)
typedef struct { const char* path; byte kind; char body[64]; u64i size; bool nul, fresh; } took_t;
static took_t taken[8];
static u32i  takes;

static void* mock_heap(void* p, size_t size) { if(size) return realloc(p, size); free(p); return NULL; }
// a copy of the member: the core frees it once taken
static void  mock_take(mode* m, file* f) {
  took_t* t = taken + takes++;
  const file* a = m->units[1].code;
  *t = (took_t){home(m)->path, home(m)->kind, .size = f->tail - f->head, .nul = !*f->tail,
                .fresh = f->body == f->head && (f->head < a->head || f->head >= a->tail)};
  memcpy(t->body, f->head, t->size < sizeof t->body? t->size: sizeof t->body);
}

static void  start(void) {
  free(self.units);
  memset(&self, 0, sizeof self);
  memset(taken, 0, sizeof taken);
  takes = 0;
  said[0] = 0;
  self = (mode){
    .name = "main.c", .heap = mock_heap,
    .form = {NULL, mock_take, os_linux}, .ario = {ario_load, ario_pull},
  };
  nest(&self, (unit){.kind = un_root, .path = "main.c"});
}

// the archive built so far joined as lib.a: 1, else 0 and said tells why
static bool  loads(void) {
  file* f = malloc(sizeof(file) + archn);
  f->body = f->head;
  f->tail = copy(f->head, arch, archn);
  start();
  if(setjmp(bail)) return 0;
  join(&self, "lib.a", 5, f, un_arch);
  return 1;
}
static bool  rejects(const char* why) {
  if(!loads() && strstr(said, why)) return 1;
  printf("    want %s\n    got  %s\n", why, said[0]? said: "no error\n");
  return 0;
}

// what name means in the archive's table
static meta* named(const char* name) {
  item* s = hsee(self.units + 1, name, strlen(name), clue(name, strlen(name)));
  return s? s->data: NULL;
}

// the member that defines name taken: 1, else 0 and said tells why
static bool  pulls(const char* name) {
  meta* x = named(name);
  if(!x) { printf("    %s is not in the table\n", name); return 0; }
  if(setjmp(bail)) return 0;
  ario_pull(&self, x);
  return 1;
}

// the member last taken is path with body
static bool  took(const char* path, const char* body) {
  if(takes && !strcmp(taken[takes - 1].path, path) && taken[takes - 1].kind == un_memb) {
    const took_t* t = taken + takes - 1;
    if(t->size == strlen(body) && alike(t->body, body, t->size) && t->nul) return 1;
  }
  printf("    want %s: %s\n", path, body);
  if(takes) printf("    got  %s: %.*s\n", taken[takes - 1].path, (int)taken[takes - 1].size,
                   taken[takes - 1].body);
  else printf("    got  nothing\n");
  return 0;
}

spec("ario") {
  context("load") {
    byte* ix;
    meta* x;
    it("rejects what is not an archive") {
      begin();
      memcpy(arch, "!<arch>", 7);
      archn = 7;
      check(rejects("not an archive: lib.a"));
      arch[0] = 'x';
      archn = 8;
      check(rejects("not an archive: lib.a"));
    }
    it("loads an empty archive as nothing") {
      begin();
      check(loads() && self.unitn == 2 && self.units[1].kind == un_arch && !self.units[1].defs);
    }
    it("needs a symbol index first") {
      begin();
      member("a.o/", "AAAA", 4);
      check(rejects("archive without a symbol index (run ranlib): lib.a"));
    }
    it("maps each name of a GNU index to its member, pending") {
      begin();
      ix = gnu("/", 4, 3, "one\0two\0three");
      u32i a = member("a.o/", "AAAA", 4), b = member("b.o/", "BBB", 3);
      point(ix, 4, 4, 1, 3, (u32i[]){a, b, a});
      check(loads());
      check((x = named("one")) && x->kind == pend && x->slot == (s32i)a);
      check(x->ext && x->size == VARY && x->unit == 1 && x->name->unit == 1);
      check((x = named("two")) && x->slot == (s32i)b && named("three")->slot == (s32i)a);
      check(!named("four"));
    }
    it("reads a GNU index of 64-bit numbers") {
      begin();
      ix = gnu("/SYM64/", 8, 2, "one\0two");
      u32i a = member("a.o/", "AAAA", 4), b = member("b.o/", "BB", 2);
      point(ix, 8, 8, 1, 2, (u32i[]){a, b});
      check(loads() && named("one")->slot == (s32i)a && named("two")->slot == (s32i)b);
    }
    it("reads a BSD index") {
      begin();
      ix = bsd("__.SYMDEF", 4, 2, "one\0two");
      u32i a = member("a.o", "AAAA", 4), b = member("b.o", "BB", 2);
      point(ix, 4, 8, 0, 2, (u32i[]){a, b});
      check(loads() && named("one")->slot == (s32i)a && named("two")->slot == (s32i)b);
    }
    it("reads a sorted BSD index of 64-bit numbers under a long name") {
      begin();
      ix = bsd("#1/__.SYMDEF_64", 8, 2, "one\0two");
      u32i a = member("a.o", "AAAA", 4), b = member("b.o", "BB", 2);
      point(ix, 8, 16, 0, 2, (u32i[]){a, b});
      check(loads() && named("one")->slot == (s32i)a && named("two")->slot == (s32i)b);
      begin();
      ix = bsd("#1/__.SYMDEF SORTED", 4, 1, "one");
      a = member("a.o", "AAAA", 4);
      point(ix, 4, 8, 0, 1, (u32i[]){a});
      check(loads() && named("one")->slot == (s32i)a);
    }
    it("keeps the first member that defines a name") {
      begin();
      ix = gnu("/", 4, 2, "one\0one");
      u32i a = member("a.o/", "AAAA", 4), b = member("b.o/", "BB", 2);
      point(ix, 4, 4, 1, 2, (u32i[]){b, a});
      check(loads() && named("one")->slot == (s32i)b);
    }
    it("rings the names of each member") {
      begin();
      ix = gnu("/", 4, 4, "one\0two\0three\0four");
      u32i a = member("a.o/", "AAAA", 4), b = member("b.o/", "BB", 2);
      point(ix, 4, 4, 1, 4, (u32i[]){a, b, a, a});
      check(loads());
      meta* one = named("one"), *two = named("two");
      u32i  n = 0;
      x = one;
      do n++; while((x = x->hide) != one && n < 8);
      check(n == 3 && named("three")->hide && named("four")->hide);
      check(two->hide == two);
    }
    it("rejects a broken header or member") {
      begin();
      ix = gnu("/", 4, 1, "one");
      arch[8 + 58] = '!';
      check(rejects("bad archive header: lib.a"));
      begin();
      ix = gnu("/", 4, 1, "one");
      archn -= 2;
      check(rejects("bad archive member: lib.a"));
    }
    it("rejects a broken index") {
      begin();
      ix = gnu("/", 4, 1, "one");
      put(ix - 4, 4, 1000, 1); // more offsets than bytes
      check(rejects("bad archive index: lib.a"));
      begin();
      ix = gnu("/", 4, 1, "one");
      member("a.o/", "AAAA", 4);
      point(ix, 4, 4, 1, 1, (u32i[]){4}); // into the magic
      check(rejects("bad archive index: lib.a"));
      begin();
      ix = gnu("/", 4, 1, "one");
      point(ix, 4, 4, 1, 1, (u32i[]){archn}); // past the end
      check(rejects("bad archive index: lib.a"));
      begin();
      ix = gnu("/", 4, 1, "one");
      arch[archn - 1] = 'e'; // the name runs past the index
      member("a.o/", "AAAA", 4);
      point(ix, 4, 4, 1, 1, (u32i[]){archn - 64});
      check(rejects("bad archive index: lib.a"));
      begin();
      ix = bsd("__.SYMDEF", 4, 1, "one");
      put(ix, 4, 99, 0); // a name past the names
      check(rejects("bad archive index: lib.a"));
    }
  }

  context("pull") {
    byte* ix;
    it("takes the member as a unit named after the archive and the member") {
      begin();
      ix = gnu("/", 4, 2, "one\0two");
      u32i a = member("a.o/", "AAAA", 4), b = member("b.o/", "BBB", 3);
      point(ix, 4, 4, 1, 2, (u32i[]){a, b});
      check(loads() && pulls("two") && took("lib.a(b.o)", "BBB"));
      check(self.unitn == 3 && self.units[2].kind == un_memb && self.unit == 0);
      check(pulls("one") && took("lib.a(a.o)", "AAAA") && takes == 2);
    }
    it("takes the names of the member out of the archive's table, not the others") {
      begin();
      ix = gnu("/", 4, 3, "one\0two\0three");
      u32i a = member("a.o/", "AAAA", 4), b = member("b.o/", "BB", 2);
      point(ix, 4, 4, 1, 3, (u32i[]){a, b, a});
      check(loads());
      meta* two = named("two");
      check(pulls("three") && !named("one") && !named("three") && named("two") == two);
    }
    it("copies the body, NUL ended") {
      begin();
      ix = gnu("/", 4, 1, "one");
      u32i a = member("a.o/", "AAAAA", 5);
      point(ix, 4, 4, 1, 1, (u32i[]){a});
      check(loads() && pulls("one") && took("lib.a(a.o)", "AAAAA"));
      check(taken[0].nul && taken[0].fresh); // a copy, not the bytes in the archive
    }
    it("finds a member past an odd one, padded") {
      begin();
      ix = gnu("/", 4, 2, "one\0tw"); // 19 bytes
      u32i t = member("//", "a-very-long-name.o/\n", 20);
      u32i a = member("/0", "A", 1), b = member("b.o/", "BB", 2);
      point(ix, 4, 4, 1, 2, (u32i[]){a, b});
      check(t == 8 + 60 + 20 && b == a + 62 && loads());
      check(pulls("one") && took("lib.a(a-very-long-name.o)", "A"));
      check(pulls("tw") && took("lib.a(b.o)", "BB"));
    }
    it("names a GNU member with a long name from the // member") {
      begin();
      ix = gnu("/", 4, 2, "one\0two");
      member("//", "a-very-long-name.o/\nanother-long-name.o/\n", 41);
      u32i a = member("/0", "AAAA", 4), b = member("/20", "BB", 2);
      point(ix, 4, 4, 1, 2, (u32i[]){a, b});
      check(loads());
      check(pulls("one") && took("lib.a(a-very-long-name.o)", "AAAA"));
      check(pulls("two") && took("lib.a(another-long-name.o)", "BB"));
    }
    it("rejects a GNU long name past the // member") {
      begin();
      ix = gnu("/", 4, 1, "one");
      member("//", "a.o/\n", 5);
      u32i a = member("/40", "AAAA", 4);
      point(ix, 4, 4, 1, 1, (u32i[]){a});
      check(loads() && !pulls("one") && strstr(said, "bad archive name: lib.a"));
    }
    it("names a BSD member with a long name from its body") {
      begin();
      ix = bsd("#1/__.SYMDEF SORTED", 4, 1, "one");
      u32i a = longer("a-very-long-name.o", 20, "AAAA", 4);
      point(ix, 4, 8, 0, 1, (u32i[]){a});
      check(loads() && pulls("one") && took("lib.a(a-very-long-name.o)", "AAAA"));
    }
    it("names a BSD member by its name before the spaces") {
      begin();
      ix = bsd("__.SYMDEF", 4, 1, "one");
      u32i a = member("a.o", "AAAA", 4);
      point(ix, 4, 8, 0, 1, (u32i[]){a});
      check(loads() && pulls("one") && took("lib.a(a.o)", "AAAA"));
    }
  }
}
