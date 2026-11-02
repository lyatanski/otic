// vim: ts=2:sw=2
// the form specs: a form alone, the front end it asks (mode.core) a mock. A
// case lays out text and data, packs them and reads the image back, or builds
// an object and takes it. fail longjmps back, so errors are checked in process
#include <setjmp.h>
#include <stdio.h>
#include <string.h>
#include <time.h> // test.h's
#include "../core.h"

static jmp_buf bail;
static char    said[256]; // what fail said
#undef  fail
#define fail(...) (snprintf(said, sizeof said, __VA_ARGS__), longjmp(bail, 1))

#include "../util.c"

static mode  self;
static unit  units[2]; // the root and the object being joined
static byte  texts[1 << 14], datas[1 << 12];

// what the form asked of the mock core
static struct { u32i calls; u64i at, org; } docked;
static u32i  laid, asked;
static meta* asks[8];

static void  mock_dock(mode*, u64i at, u64i org) {
  docked.calls++;
  docked.at = at;
  docked.org = org;
}
static u64i  mock_gap(mode* self) { return self->most > 16? self->most: 16; }
static u32i  mock_lay(mode* self, u32i size, bool zero, u32i align) {
  laid++;
  if(zero) {
    self->zero = ((self->zero + align - 1) & -align) + size;
    return (self->zero - size) | ZERO;
  }
  u32i off = ((self->data->body - self->data->head) + align - 1) & -align;
  self->data->body = self->data->head + off + size;
  return off;
}

// the names: one table for every unit, in item.data what the name means in
// the unit being joined; elsewhere, what another unit defines
static item  items[32];
static meta* elsewhere[32];
static u32i  itemn;

static item* mock_name(mode*, u32i at, char* mark, u64i size) {
  for(u32i i = 0; i < itemn; i++)
    if(items[i].size == size && !memcmp(items[i].name, mark, size)) return items + i;
  items[itemn] = (item){.size = size, .name = mark, .unit = at};
  return items + itemn++;
}
static item* named(char* name) { return mock_name(&self, 1, name, strlen(name)); }
static meta* mock_find(mode*, u32i at, item* name) {
  if(at && name->data) return name->data;
  return elsewhere[name - items];
}
static u32i  mock_heft(meta* x) { return x->kind == pend? 2: x->lib? 1: x->weak? 3: 4; }
static u32i  mock_ask(mode*, meta* x) { asks[asked] = x; return asked++; }

// another unit defines name as x
static meta  defs[8];
static u32i  defn;
static inline void define(char* name, meta x) {
  item* s = named(name);
  defs[defn] = x;
  defs[defn].name = s;
  elsewhere[s - items] = defs + defn++;
}

// a fresh mode for cpu: code and data as given, main at main bytes into code
static void  start(chip cpu, char* code, u32i size, char* data, u32i dsize, u32i main) {
  static file* text = (file*)texts;
  static file* data_ = (file*)datas;
  free(self.hooks);
  memset(&self, 0, sizeof self);
  memset(units, 0, sizeof units);
  memset(items, 0, sizeof items);
  memset(elsewhere, 0, sizeof elsewhere);
  memset(&docked, 0, sizeof docked);
  laid = asked = itemn = defn = 0;
  said[0] = 0;
  text->body = copy(text->head, code, size);
  text->tail = texts + sizeof texts;
  data_->body = copy(data_->head, data, dsize);
  data_->tail = datas + sizeof datas;
  units[1].path = "obj.o";
  self = (mode){
    .text = text, .data = data_, .main = text->head + main,
    .units = units, .unit = 1, .unitn = 2,
    .arch = {.cpu = cpu},
    .core = {mock_dock, mock_gap, mock_lay, mock_name, mock_find, mock_heft, mock_ask},
  };
}

// the image, or NULL and said tells why
static file* packed(form f) {
  static file* out;
  free(out);
  out = NULL;
  if(setjmp(bail)) return NULL;
  return out = f.pack(&self);
}

// the bytes at p are want, in hex, else both are shown
static bool  bytes(byte* p, char* want) {
  char got[256] = "";
  u32i n = 0;
  for(u32i i = 0; i < (strlen(want) + 1) / 3 && n < sizeof got - 4; i++)
    n += sprintf(got + n, n? " %02x": "%02x", p[i]);
  if(!strcmp(got, want)) return 1;
  printf("    want %s\n    got  %s\n", want, got);
  return 0;
}
