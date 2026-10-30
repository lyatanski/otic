// vim: ts=2:sw=2
#include "core.h"

// ar archive (static library): "!<arch>\n", then members, each a 60-byte text
// header and its body, padded to an even size. An archive is one unit whose
// table is its symbol index, the first member: each name maps to the member
// that defines it, which is taken (pulled) as a unit of its own only once one
// of its names is used.
// - GNU names end in '/'; a long one is "/n", n an offset into the "//"
//   member. Its index is "/" or "/SYM64/": a big endian count of 4 or 8
//   bytes, as many offsets of member headers, then as many names, NUL ended.
// - BSD names end in spaces; a long one is "#1/n", its n bytes open the body.
//   Its index is "__.SYMDEF" or "__.SYMDEF_64", maybe " SORTED": the bytes of
//   the (name, offset) pairs, the pairs, the bytes of the names, the names;
//   little endian numbers of 4 or 8 bytes.

typedef struct {
  char name[16], date[12], user[6], group[6], mode[8], size[10], seal[2];
} ario_head;

// a member: its name (GNU long ones from "//"), its body and the next header
typedef struct {
  char* name;
  u64i  nlen;
  byte* body;
  u64i  size;
  byte* next;
} ario_part;

static u64i ario_num(char* at, u32i size) {
  u64i n = 0;
  for(; size && *at >= '0' && *at <= '9'; size--) n = n * 10 + *at++ - '0';
  return n;
}

// a number of size bytes at at, big endian (GNU) or little endian (BSD)
static u64i ario_word(byte* at, u32i size, bool big) {
  u64i n = 0;
  for(u32i i = 0; i < size; i++) n = n << 8 | at[big? i: size - 1 - i];
  return n;
}

// the member whose header is at at, in the archive f at path
static ario_part ario_read(char* path, file* f, byte* at) {
  ario_head* h = (ario_head*)at;
  ario_part  p = {.name = h->name, .body = at + sizeof(*h)};
  if((u64i)(f->tail - at) < sizeof(*h) || h->seal[0] != '`' || h->seal[1] != '\n')
    fail("bad archive header: %s\n", path);
  p.size = ario_num(h->size, sizeof(h->size));
  if(p.size > (u64i)(f->tail - p.body)) fail("bad archive member: %s\n", path);
  p.next = p.size + (p.size & 1) < (u64i)(f->tail - p.body)? p.body + p.size + (p.size & 1): f->tail;
  if(alike(p.name, "#1/", 3)) {
    p.nlen = ario_num(p.name + 3, sizeof(h->name) - 3);
    if(p.nlen > p.size) fail("bad archive name: %s\n", path);
    p.name = (char*)p.body;
    p.body += p.nlen;
    p.size -= p.nlen;
    while(p.nlen && !p.name[p.nlen - 1]) p.nlen--;
  } else if(p.name[0] == '/' && p.name[1] >= '0' && p.name[1] <= '9') {
    u64i off = ario_num(p.name + 1, sizeof(h->name) - 1);
    ario_part t = {.size = 0};
    byte* q = f->head + 8;
    for(; q < at && !alike(q, "// ", 3); q = t.next) t = ario_read(path, f, q);
    if(q < at) t = ario_read(path, f, q);
    if(q >= at || off >= t.size) fail("bad archive name: %s\n", path);
    p.name = (char*)t.body + off;
    for(p.nlen = 0; off + p.nlen < t.size && p.name[p.nlen] != '/' && p.name[p.nlen] != '\n'; p.nlen++);
  } else {
    while(p.nlen < sizeof(h->name) && p.name[p.nlen] != '/' && p.name[p.nlen] != ' ') p.nlen++;
  }
  return p;
}

// name, len bytes, defined by the member whose header is off bytes into the
// archive: an archive name x, unless an earlier member defines it
static void ario_name(mode* self, meta* x, char* name, u64i len, u64i off) {
  file* f = home(self)->code;
  if(off < 8 || off >= (u64i)(f->tail - f->head) || off >> 31 || !len)
    fail("bad archive index: %s\n", home(self)->path);
  item* s = hget(home(self), name, len);
  if(s->data) return;
  *x = (meta){.name = s, .kind = pend, .ext = 1, .size = VARY, .slot = off, .unit = self->unit};
  s->data = x;
}

// the symbol index of the archive f, the unit being joined, into its table.
// Nothing is copied into text or data
void ario_load(mode* self, file* f) {
  char* path = home(self)->path;
  if(f->tail - f->head < 8 || !alike(f->head, "!<arch>\n", 8))
    fail("not an archive: %s\n", path);
  if(f->tail == f->head + 8) return;
  ario_part p = ario_read(path, f, f->head + 8);
  char* h = (char*)f->head + 8;
  bool  bsd = p.nlen >= 9 && alike(p.name, "__.SYMDEF", 9);
  u32i  w = alike(h, "/ ", 2)? 4: alike(h, "/SYM64/ ", 8)? 8:
            bsd? p.nlen >= 12 && alike(p.name + 9, "_64", 3)? 8: 4: 0;
  if(!w) fail("archive without a symbol index (run ranlib): %s\n", path);
  byte* b = p.body;
  u64i  n, size = p.size;
  if(size < w) fail("bad archive index: %s\n", path);
  if(!bsd) { // count, offsets, names
    n = ario_word(b, w, 1);
    if(n > (size - w) / (w + 1)) fail("bad archive index: %s\n", path);
    char* name = (char*)b + w + n * w;
    char* end  = (char*)b + size;
    meta* defs = calloc(n + 1, sizeof(meta));
    if(!defs) fail("out of memory\n");
    for(u64i i = 0; i < n; i++) {
      u64i len = 0;
      while(name + len < end && name[len]) len++;
      if(name + len == end) fail("bad archive index: %s\n", path);
      ario_name(self, defs + i, name, len, ario_word(b + w + i * w, w, 1));
      name += len + 1;
    }
    return;
  }
  u64i pairs = ario_word(b, w, 0); // bytes of (name, offset) pairs, names
  if(size < 2 * w || pairs > size - 2 * w) fail("bad archive index: %s\n", path);
  u64i strs = ario_word(b + w + pairs, w, 0);
  if(strs > size - 2 * w - pairs) fail("bad archive index: %s\n", path);
  char* name = (char*)b + 2 * w + pairs;
  n = pairs / (2 * w);
  meta* defs = calloc(n + 1, sizeof(meta));
  if(!defs) fail("out of memory\n");
  for(u64i i = 0; i < n; i++) {
    u64i at = ario_word(b + w + 2 * w * i, w, 0), len = 0;
    if(at >= strs) fail("bad archive index: %s\n", path);
    while(at + len < strs && name[at + len]) len++;
    ario_name(self, defs + i, name + at, len, ario_word(b + 2 * w + 2 * w * i, w, 0));
  }
}

// take the member that defines archive name x, as a unit named
// path(member), its body copied to be aligned for its headers. The names the
// index maps to it leave the archive's table: the member's own table has them
void ario_pull(mode* self, meta* x) {
  unit*     a = self->units + x->unit;
  ario_part p = ario_read(a->path, a->code, a->code->head + x->slot);
  for(u32i i = 0; i < a->size; i++) {
    meta* m = a->pool[i]? a->pool[i]->data: NULL;
    if(m && m->kind == pend && m->slot == x->slot) a->pool[i]->data = NULL;
  }
  u64i  plen = 0;
  while(a->path[plen]) plen++;
  file* o = malloc(sizeof(file) + p.size);
  char* n = malloc(plen + p.nlen + 3);
  if(!o || !n) fail("out of memory\n");
  o->body = o->head;
  o->tail = copy(o->head, p.body, p.size);
  *o->tail = 0;
  copy(copy(copy(copy((byte*)n, a->path, plen), "(", 1), p.name, p.nlen), ")", 2);
  join(self, n, plen + p.nlen + 2, o, un_memb);
}
