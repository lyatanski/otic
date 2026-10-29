// vim: ts=2:sw=2
#include "core.h"

// ar archive (static library): "!<arch>\n", then members, each a 60-byte text
// header and its body, padded to an even size. Each member is handed to the
// core as an object to link, as if it was named on the command line.
// - GNU names end in '/'; a long one is "/n", n an offset into the "//"
//   member. Its symbol index is "/" or "/SYM64/".
// - BSD names end in spaces; a long one is "#1/n", its n bytes open the body.
//   Its symbol index is "__.SYMDEF" or "__.SYMDEF_64", maybe " SORTED".
// The index and the long names are not objects.

typedef struct {
  char name[16], date[12], user[6], group[6], mode[8], size[10], seal[2];
} ario_head;

static bool ario_same(char* x, char* y, u64i size) {
  for(; size && *x == *y; size--, x++, y++);
  return !size;
}

static u64i ario_num(char* at, u32i size) {
  u64i n = 0;
  for(; size && *at >= '0' && *at <= '9'; size--) n = n * 10 + *at++ - '0';
  return n;
}

// the members of the archive at path, read whole in f, as objects named
// path(member), each a copy aligned for its headers
void ario_load(mode* self, char* path, file* f) {
  if(f->tail - f->head < 8 || !ario_same((char*)f->head, "!<arch>\n", 8))
    fail("not an archive: %s\n", path);
  u64i  plen = 0, tlen = 0;
  char* tags = NULL;
  while(path[plen]) plen++;
  for(byte* at = f->head + 8; at < f->tail; ) {
    ario_head* h = (ario_head*)at;
    byte* body = at + sizeof(*h);
    if((u64i)(f->tail - at) < sizeof(*h) || h->seal[0] != '`' || h->seal[1] != '\n')
      fail("bad archive header: %s\n", path);
    u64i  size = ario_num(h->size, sizeof(h->size));
    if(size > (u64i)(f->tail - body)) fail("bad archive member: %s\n", path);
    at = size + (size & 1) < (u64i)(f->tail - body)? body + size + (size & 1): f->tail;

    char* name = h->name;
    u64i  nlen = 0;
    if(ario_same(name, "// ", 3)) { tags = (char*)body; tlen = size; continue; }
    if(ario_same(name, "/ ", 2) || ario_same(name, "/SYM64/ ", 8)) continue;
    if(ario_same(name, "#1/", 3)) {
      nlen = ario_num(name + 3, sizeof(h->name) - 3);
      if(nlen > size) fail("bad archive name: %s\n", path);
      name = (char*)body;
      body += nlen;
      size -= nlen;
      while(nlen && !name[nlen - 1]) nlen--;
    } else if(name[0] == '/' && name[1] >= '0' && name[1] <= '9') {
      u64i off = ario_num(name + 1, sizeof(h->name) - 1);
      if(off >= tlen) fail("bad archive name: %s\n", path);
      name = tags + off;
      while(off + nlen < tlen && name[nlen] != '/' && name[nlen] != '\n') nlen++;
    } else {
      while(nlen < sizeof(h->name) && name[nlen] != '/' && name[nlen] != ' ') nlen++;
    }
    if(nlen >= 9 && ario_same(name, "__.SYMDEF", 9)) continue;

    file* o = malloc(sizeof(file) + size);
    char* n = malloc(plen + nlen + 3);
    if(!o || !n) fail("out of memory\n");
    o->body = o->head;
    o->tail = copy(o->head, body, size);
    *o->tail = 0;
    copy(copy(copy(copy((byte*)n, path, plen), "(", 1), name, nlen), ")", 2);
    bolt(self, n, o);
  }
}
