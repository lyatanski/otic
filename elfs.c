// vim: ts=2:sw=2
#include "core.h"

enum { eb_none, eb_cl32, eb_cl64 };
enum { ed_none, ed_2lsb, ed_2msb };
enum { ev_none, ev_curr };
enum { et_none, et_relo, et_exec, et_dyno, et_core };
enum { em_ia64 = 62, em_arm8 = 183 };
static const u16i elfs_mach[] = { [cpu_ia64] = em_ia64, [cpu_arm8] = em_arm8 };

typedef union {
  byte _[16];
  struct { byte seal[4], bits, data, issue, osabi, abiver; };
} elfs_id;

#define T(B) u##B##i

#define ehdr(B)                                                               \
typedef struct {                                                              \
  u16i type, arch;                                                            \
  u32i issue;                                                                 \
  T(B) entry, phoff, shoff;                                                   \
  u32i flags;                                                                 \
  u16i ehsize, phsize, phnum, shsize, shnum, shstr;                           \
} eh##B
ehdr(32);
ehdr(64);
#undef ehdr

enum {
  SHT_PROGBITS = 1, SHT_SYMTAB, SHT_STRTAB, SHT_RELA, SHT_HASH, SHT_DYNAMIC,
  SHT_NOTE, SHT_NOBITS, SHT_REL, SHT_SHLIB, SHT_DYNSYM,
};
#define shdr(B)                                                               \
typedef struct {                                                              \
  u32i name, type;                                                            \
  T(B) flags, addr, offset, size;                                             \
  u32i link, info;                                                            \
  T(B) align, entsize;                                                        \
} sh##B
shdr(32);
shdr(64);
#undef shdr

typedef struct {
  u32i name; u32i value; u32i size; byte type: 4, bind: 4, other; u16i shidx;
} sym32;
typedef struct {
  u32i name; byte type: 4, bind: 4, other; u16i shidx; u64i value; u64i size;
} sym64;

typedef struct {
  u32i type, offset, vaddr, paddr, filesz, memsz, flags, align;
} ph32;
typedef struct {
  u32i type, flags; u64i offset, vaddr, paddr, filesz, memsz, align;
} ph64;

// text, then data and bss in a second, writable segment: 64 KB apart in
// memory so both segments start on their own page with any page size
file* elfs_pack(mode* self) {
  const u64i org  = 0x400000;
  const u64i code = self->text->body - self->text->head;
  const u64i size = self->data->body - self->data->head;
  const u64i data = (code + 15) & ~15; // data after text in the file
  const u64i nph  = size || self->zero? 2: 1;
  const u64i top  = sizeof(elfs_id) + sizeof(eh64) + nph * sizeof(ph64);
  const u64i all  = top + (nph > 1? data + size: code);
  dock(self, data + 0x10000);

  elfs_id id = {
    .seal   = {0x7f, 'E', 'L', 'F'},
    .bits   = eb_cl64,
    .data   = ed_2lsb,
    .issue  = ev_curr,
  };
  eh64 eh = {
    .type   = et_exec,
    .arch   = elfs_mach[self->arch.cpu],
    .issue  = ev_curr,
    .entry  = org + top,
    .phoff  = sizeof(id) + sizeof(eh),
    .ehsize = sizeof(id) + sizeof(eh),
    .phsize = sizeof(ph64),
    .phnum  = nph,
  };
  ph64 ph[] = {{
    .type   = 1, // PT_LOAD
    .flags  = 5, // PF_R | PF_X
    .vaddr  = org,
    .paddr  = org,
    .filesz = top + code,
    .memsz  = top + code,
    .align  = 0x1000,
  }, {
    .type   = 1,
    .flags  = 6, // PF_R | PF_W
    .offset = top + data,
    .vaddr  = org + top + data + 0x10000,
    .paddr  = org + top + data + 0x10000,
    .filesz = size,
    .memsz  = ((size + 15) & ~15) + self->zero,
    .align  = 0x10000,
  }};

  file* out = calloc(1, sizeof(file) + all);
  if(!out) fail("out of memory\n");
  out->body = out->head + all;
  out->tail = out->head + all;
  byte* at = copy(copy(out->head, &id, sizeof(id)), &eh, sizeof(eh));
  copy(copy(at, ph, nph * sizeof(ph64)), self->text->head, code);
  copy(out->head + top + data, self->data->head, size);
  return out;
}

enum { SHT_INIT_ARRAY = 14, SHT_FINI_ARRAY, SHT_PREINIT_ARRAY, SHT_X86_64_UNWIND = 0x70000001 };
enum { SHF_ALLOC = 0x2, SHF_EXECINSTR = 0x4, SHF_TLS = 0x400 };
enum { STB_LOCAL, STB_GLOBAL, STB_WEAK };
enum { SHN_UNDEF, SHN_ABS = 0xfff1, SHN_COMMON };
enum {
  R_X86_64_NONE, R_X86_64_PC32 = 2, R_X86_64_PLT32 = 4, R_X86_64_GOTPCREL = 9,
  R_X86_64_GOTPCRELX = 41, R_X86_64_REX_GOTPCRELX,
};

typedef struct { u64i offset; u32i type, sym; s64i addend; } rela64;

#define GONE ((seat){NULL, ~0u}) // a section left out
#define flop(fmt, ...) fail("%s: " fmt, self->name __VA_OPT__(,) __VA_ARGS__)

// string at in table tab, "" past its end; the object is NUL terminated
static char* elfs_str(byte* o, sh64* tab, u64i at) {
  return tab && at < tab->size? (char*)o + tab->offset + at: "";
}

// a relocatable object, named by mode.name, aligned for its headers, NUL ended
// and kept: the pool points at its names. Its code goes after text, its data
// and bss are laid out, its globals defined, and its references hooked for
// dock. Only x86-64 and pc-relative references so far: no absolute pointers,
// thread-locals or constructors
void elfs_take(mode* self, file* f) {
  u64i  size = f->tail - f->head;
  byte* o = f->head;
  elfs_id* id = (elfs_id*)o;
  eh64* eh = (eh64*)(id + 1);
  if(size < sizeof(*id) + sizeof(*eh) || id->seal[0] != 0x7f || id->seal[1] != 'E'
     || id->seal[2] != 'L' || id->seal[3] != 'F' || id->bits != eb_cl64
     || id->data != ed_2lsb || eh->type != et_relo)
    flop("not a 64-bit object\n");
  if(eh->arch != elfs_mach[self->arch.cpu]) flop("an object for another target\n");
  if(self->arch.cpu != cpu_ia64) flop("only x86-64 objects so far\n");
  if(eh->shsize != sizeof(sh64) || eh->shoff > size
     || eh->shnum > (size - eh->shoff) / sizeof(sh64))
    flop("bad section headers\n");
  u32i  n  = eh->shnum;
  sh64* sh = (sh64*)(o + eh->shoff);
  for(u32i i = 0; i < n; i++)
    if((sh[i].type != SHT_NOBITS && (sh[i].offset > size || sh[i].size > size - sh[i].offset))
       || sh[i].size >> 30 || sh[i].link >= n)
      flop("bad section\n");

  // sections: code after text, the rest in data or bss
  seat* put = malloc(n * sizeof(seat) + 1);
  if(!put) fail("out of memory\n");
  for(u32i i = 0; i < n; i++) {
    sh64* s = sh + i;
    u64i  a = s->align? s->align: 1;
    put[i] = GONE;
    if(!(s->flags & SHF_ALLOC) || s->type == SHT_NOTE || s->type == SHT_X86_64_UNWIND)
      continue; // unwind tables and notes are not used
    if(s->flags & SHF_TLS) flop("thread-local storage is not supported\n");
    if(s->type == SHT_INIT_ARRAY || s->type == SHT_FINI_ARRAY || s->type == SHT_PREINIT_ARRAY)
      flop("constructors are not supported\n");
    if(a > 16 || a & (a - 1)) flop("alignment over 16\n");
    if(s->flags & SHF_EXECINSTR) {
      file* t = self->text;
      u64i  at = ((t->body - t->head) + a - 1) & -a;
      if(at + s->size > (u64i)(t->tail - t->head)) fail("text section overflow\n");
      put[i] = (seat){t->head + at, 0};
      t->body = copy(put[i].at, o + s->offset, s->type == SHT_NOBITS? 0: s->size);
    } else if(s->type == SHT_NOBITS) {
      self->zero = (self->zero + a - 1) & -a;
      put[i].off = lay(self, s->size, 1);
    } else {
      file* d = self->data;
      u64i  at = ((d->body - d->head) + a - 1) & -a;
      if(at > (u64i)(d->tail - d->head)) fail("data section overflow\n");
      d->body = d->head + at;
      put[i].off = lay(self, s->size, 0);
      copy(d->head + put[i].off, o + s->offset, s->size);
    }
  }

  // globals: defined in the pool, as functions taking any arguments or data
  sym64* sym = NULL;
  sh64*  str = NULL;
  u64i   nsym = 0;
  u32i   syms = 0;
  for(u32i i = 0; i < n; i++) {
    if(sh[i].type != SHT_SYMTAB) continue;
    if(sh[i].entsize != sizeof(sym64)) flop("bad symbol table\n");
    sym  = (sym64*)(o + sh[i].offset);
    nsym = sh[i].size / sizeof(sym64);
    str  = sh + sh[i].link;
    syms = i;
  }
  meta* defs = calloc(nsym + 1, sizeof(meta));
  if(!defs) fail("out of memory\n");
  for(u64i i = 1; i < nsym; i++) {
    sym64* s = sym + i;
    if(s->bind == STB_LOCAL || s->shidx == SHN_UNDEF || s->shidx == SHN_ABS) continue;
    char*  at = elfs_str(o, str, s->name);
    u64i   len = 0;
    while(at[len]) len++;
    if(!len) continue;
    item*  name = hget(self->pool, at, len);
    meta*  prev = name->data;
    meta   x = {.name = name, .kind = glob, .weak = s->bind == STB_WEAK, .path = self->name};
    if(prev && x.weak) continue; // the first or the strong definition stays
    if(prev && !prev->weak) {
      if(s->shidx == SHN_COMMON) continue; // a tentative definition
      if(prev->ext && !prev->addr) flop("'%.*s' is declared before the object\n", (int)len, at);
      flop("'%.*s' is defined twice\n", (int)len, at);
    }
    if(s->shidx == SHN_COMMON) { // value: alignment
      u64i a = s->value? s->value: 1;
      if(a > 16 || a & (a - 1) || s->size >> 30) flop("bad common symbol\n");
      self->zero = (self->zero + a - 1) & -a;
      x.slot = lay(self, s->size, 1);
    } else {
      if(s->shidx >= n || put[s->shidx].off == GONE.off)
        flop("'%.*s' is in a section left out\n", (int)len, at);
      seat p = put[s->shidx];
      if(p.at) { x.kind = call; x.addr = p.at + s->value; x.size = VARY; x.ext = 1; }
      else x.slot = p.off + s->value;
    }
    defs[i] = x;
    name->data = defs + i;
  }

  // references: by name to globals, which a later definition may replace
  for(u32i i = 0; i < n; i++) {
    if(sh[i].type != SHT_RELA || sh[i].info >= n || put[sh[i].info].off == GONE.off) continue;
    sh64*   t = sh + sh[i].info;
    seat    p = put[sh[i].info];
    rela64* r = (rela64*)(o + sh[i].offset);
    if(sh[i].entsize != sizeof(rela64) || sh[i].link != syms || t->type == SHT_NOBITS)
      flop("bad relocations\n");
    for(rela64* e = r + sh[i].size / sizeof(rela64); r < e; r++) {
      if(r->type == R_X86_64_NONE) continue;
      if(r->offset < 2 || r->offset > t->size - 4 || t->size < 4 || r->sym >= nsym)
        flop("bad relocation\n");
      byte* at = p.at? p.at + r->offset: self->data->head + p.off + r->offset;
      switch(r->type) {
      case R_X86_64_GOTPCREL: case R_X86_64_GOTPCRELX: case R_X86_64_REX_GOTPCRELX:
        // no GOT: its loads become the address itself
        if(at[-2] == 0x8b) at[-2] = 0x8d;                                // mov reg -> lea reg
        else if(at[-2] == 0xff && at[-1] == 0x15) at[-2] = 0x67, at[-1] = 0xe8; // call * -> addr32 call
        else if(at[-2] == 0xff && at[-1] == 0x25) at[-2] = 0x90, at[-1] = 0xe9; // jmp * -> nop; jmp
        else flop("cannot drop the GOT load at %#llx\n", (unsigned long long)r->offset);
        break;
      case R_X86_64_PC32: case R_X86_64_PLT32: break;
      default: flop("relocation type %u is not supported\n", r->type);
      }
      sym64* s = sym + r->sym;
      hook   h = {.site = p.at? (seat){at, 0}: (seat){NULL, p.off + (u32i)r->offset},
                  .add  = r->addend};
      if(s->bind != STB_LOCAL) {
        char* name = elfs_str(o, str, s->name);
        u64i  len = 0;
        while(name[len]) len++;
        h.name = hget(self->pool, name, len);
      } else if(s->shidx >= n || put[s->shidx].off == GONE.off) {
        flop("a reference into a section left out\n");
      } else {
        seat q = put[s->shidx];
        h.aim = q.at? (seat){q.at + s->value, 0}: (seat){NULL, q.off + (u32i)s->value};
      }
      if(self->hookn == self->hookc) {
        self->hookc = self->hookc * 2 + 256;
        self->hooks = realloc(self->hooks, self->hookc * sizeof(hook));
        if(!self->hooks) fail("out of memory\n");
      }
      self->hooks[self->hookn++] = h;
    }
  }
  free(put);
}
#undef flop

const form elfs = {
  .pack = elfs_pack,
  .take = elfs_take,
  .os   = os_linux,
};
