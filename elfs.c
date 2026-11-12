// vim: ts=2:sw=2
#include "core.h"

enum { eb_none, eb_cl32, eb_cl64 };
enum { ed_none, ed_2lsb, ed_2msb };
enum { ev_none, ev_curr };
enum { et_none, et_relo, et_exec, et_dyno, et_core };
enum { em_ia64 = 62, em_arm8 = 183 };
static const u16i elfs_mach[] = { [cpu_ia64] = em_ia64, [cpu_arm8] = em_arm8 };
// the loader of a program, glibc's, musl's or bionic's: it relocates the image
// and binds its imports
static char* const elfs_loader[][3] = {
  [cpu_ia64] = {"/lib64/ld-linux-x86-64.so.2", "/lib/ld-musl-x86_64.so.1", "/system/bin/linker64"},
  [cpu_arm8] = {"/lib/ld-linux-aarch64.so.1", "/lib/ld-musl-aarch64.so.1", "/system/bin/linker64"},
};

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

enum { SHT_INIT_ARRAY = 14, SHT_FINI_ARRAY, SHT_PREINIT_ARRAY, SHT_X86_64_UNWIND = 0x70000001 };
enum { SHF_WRITE = 0x1, SHF_ALLOC = 0x2, SHF_EXECINSTR = 0x4, SHF_INFO_LINK = 0x40, SHF_TLS = 0x400 };
enum { STB_LOCAL, STB_GLOBAL, STB_WEAK };
enum { SHN_UNDEF, SHN_ABS = 0xfff1, SHN_COMMON };
enum {
  R_X86_64_NONE, R_X86_64_64, R_X86_64_PC32, R_X86_64_PLT32 = 4, R_X86_64_GLOB_DAT = 6,
  R_X86_64_RELATIVE = 8, R_X86_64_GOTPCREL = 9,
  R_X86_64_GOTPCRELX = 41, R_X86_64_REX_GOTPCRELX,
};

typedef struct { u64i offset; u32i type, sym; s64i addend; } rela64;

#define GONE ((seat){NULL, ~0u}) // a section left out
#define flop(fmt, ...) fail("%s: " fmt, home(self)->path __VA_OPT__(,) __VA_ARGS__)

// string at in table tab, "" past its end; the object is NUL terminated
static char* elfs_str(byte* o, const sh64* tab, u64i at) {
  return tab && at < tab->size? (char*)o + tab->offset + at: "";
}

// the name of symbol s, string table str, in the table of the unit being
// joined: NULL when it has none
static item* elfs_name(mode* self, byte* o, const sh64* str, const sym64* s) {
  char* at = elfs_str(o, str, s->name);
  u64i  len = slen(at);
  return len? self->core.name(self, self->unit, at, len): NULL;
}

// a relocatable object, the unit being joined, aligned for its headers and
// NUL ended: it can go once taken, its names have their own bytes. Its code
// goes after text, its data and bss are laid out, its globals defined in its
// table, its undefined names ask for the archive members that define them,
// and its references are hooked for dock. Only x86-64 so far: no
// thread-locals or constructors
void elfs_take(mode* self, file* f) {
  u64i  size = f->tail - f->head;
  byte* o = f->head;
  const elfs_id* id = (const elfs_id*)o;
  const eh64* eh = (const eh64*)(id + 1);
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
  const sh64* sh = (const sh64*)(o + eh->shoff);
  for(u32i i = 0; i < n; i++)
    if((sh[i].type != SHT_NOBITS && (sh[i].offset > size || sh[i].size > size - sh[i].offset))
       || sh[i].size >> 30 || sh[i].link >= n)
      flop("bad section\n");

  // sections: code after text, the rest in data or bss
  seat* put = self->heap(NULL, n * sizeof(seat) + 1);
  for(u32i i = 0; i < n; i++) {
    const sh64* s = sh + i;
    u64i  a = s->align? s->align: 1;
    put[i] = GONE;
    if(!(s->flags & SHF_ALLOC) || s->type == SHT_NOTE || s->type == SHT_X86_64_UNWIND)
      continue; // unwind tables and notes are not used
    if(s->flags & SHF_TLS) flop("thread-local storage is not supported\n");
    if(s->type == SHT_INIT_ARRAY || s->type == SHT_FINI_ARRAY || s->type == SHT_PREINIT_ARRAY)
      flop("constructors are not supported\n");
    if(a > 4096 || a & (a - 1)) flop("alignment over a page\n");
    if(a > self->most) self->most = (u32i)a;
    if(s->flags & SHF_EXECINSTR) {
      file* t = self->text;
      u64i  at = ((t->body - t->head) + a - 1) & ~(a - 1);
      if(at + s->size > (u64i)(t->tail - t->head)) fail("text section overflow\n");
      put[i] = (seat){t->head + at, 0};
      t->body = copy(put[i].at, o + s->offset, s->type == SHT_NOBITS? 0: s->size);
    } else {
      put[i].off = self->core.lay(self, (u32i)s->size, s->type == SHT_NOBITS, (u32i)a);
      if(s->type != SHT_NOBITS) copy(self->data->head + put[i].off, o + s->offset, s->size);
    }
  }

  // globals: functions taking any arguments, or data. A weak or tentative
  // (common) one is left out when another unit defines the name, and a strong
  // one replaces it
  const sym64* sym = NULL;
  const sh64*  str = NULL;
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
  meta* defs = zero(self, (nsym + 1) * sizeof(meta));
  for(u64i i = 1; i < nsym; i++) {
    const sym64* s = sym + i;
    if(s->bind == STB_LOCAL || s->shidx == SHN_UNDEF || s->shidx == SHN_ABS) continue;
    item*  name = elfs_name(self, o, str, s);
    if(!name) continue;
    const meta*  prev = self->core.find(self, 0, name);
    u32i   held = prev? self->core.heft(prev): 0;
    meta   x = {.name = name, .kind = glob, .type = self->core.find(self, 0, self->core.name(self, 0, "int", 3)),
                .unit = self->unit, .weak = s->bind == STB_WEAK || s->shidx == SHN_COMMON};
    if(held > 2 && x.weak) continue;
    if(held == 4) flop("'%.*s' is defined twice\n", nick(name));
    if(prev && prev->lib) flop("'%.*s' is declared before the object\n", nick(name));
    if(s->shidx == SHN_COMMON) { // value: alignment
      u64i a = s->value? s->value: 1;
      if(a > 4096 || a & (a - 1) || s->size >> 30) flop("bad common symbol\n");
      if(a > self->most) self->most = (u32i)a;
      x.slot = self->core.lay(self, (u32i)s->size, 1, (u32i)a);
    } else {
      if(s->shidx >= n || put[s->shidx].off == GONE.off)
        flop("'%.*s' is in a section left out\n", nick(name));
      seat p = put[s->shidx];
      if(p.at) { x.kind = call; x.addr = p.at + s->value; x.size = VARY; x.ext = 1; }
      else x.slot = (s32i)(p.off + s->value);
    }
    defs[i] = x;
    name->data = defs + i;
  }
  for(u64i i = 1; i < nsym; i++) { // a weak undefined name does not pull
    const sym64* s = sym + i;
    if(s->bind != STB_GLOBAL || s->shidx != SHN_UNDEF) continue;
    item*  name = elfs_name(self, o, str, s);
    meta*  m = name && !name->data? self->core.find(self, self->unit, name): NULL;
    if(m && m->kind == pend) self->core.ask(self, m);
  }

  // references: by name to globals, which a later definition may replace.
  // Room for all of them first, so that hooks fits a large object
  u64i  refs = self->hookn;
  for(u32i i = 0; i < n; i++) if(sh[i].type == SHT_RELA) refs += sh[i].size / sizeof(rela64);
  if(refs >> 31) flop("too many relocations\n");
  if(refs > self->hookc) {
    self->hookc = refs > 2 * (u64i)self->hookc? (u32i)refs: 2 * self->hookc;
    self->hooks = self->heap(self->hooks, self->hookc * sizeof(hook));
  }
  for(u32i i = 0; i < n; i++) {
    if(sh[i].type != SHT_RELA || sh[i].info >= n || put[sh[i].info].off == GONE.off) continue;
    const sh64*   t = sh + sh[i].info;
    seat    p = put[sh[i].info];
    const rela64* r = (const rela64*)(o + sh[i].offset);
    if(sh[i].entsize != sizeof(rela64) || sh[i].link != syms || t->type == SHT_NOBITS)
      flop("bad relocations\n");
    for(const rela64* e = r + sh[i].size / sizeof(rela64); r < e; r++) {
      if(r->type == R_X86_64_NONE) continue;
      u64i  w = r->type == R_X86_64_64? 8: 4;
      if(r->offset > t->size || t->size - r->offset < w || r->sym >= nsym)
        flop("bad relocation\n");
      byte* at = p.at? p.at + r->offset: self->data->head + p.off + r->offset;
      bool  load = 0, got = 0;
      switch(r->type) {
      case R_X86_64_GOTPCREL: case R_X86_64_GOTPCRELX: case R_X86_64_REX_GOTPCRELX:
        // no GOT: its loads become the address itself
        if(r->offset < 2) flop("bad relocation\n");
        if(at[-2] == 0x8b) { // mov reg -> lea reg
          at[-2] = 0x8d;
          got = 1;
          load = r->type == R_X86_64_REX_GOTPCRELX && r->offset > 2;
        }
        else if(at[-2] == 0xff && at[-1] == 0x15) at[-2] = 0x67, at[-1] = 0xe8; // call * -> addr32 call
        else if(at[-2] == 0xff && at[-1] == 0x25) at[-2] = 0x90, at[-1] = 0xe9; // jmp * -> nop; jmp
        else flop("cannot drop the GOT load at %#llx\n", (unsigned long long)r->offset);
        break;
      case R_X86_64_PC32: case R_X86_64_PLT32: case R_X86_64_64: break;
      default: flop("relocation type %u is not supported\n", r->type);
      }
      const sym64* s = sym + r->sym;
      hook   h = {.site = p.at? (seat){at, 0}: (seat){NULL, p.off + (u32i)r->offset},
                  .unit = self->unit, .add = r->addend, .wide = w == 8,
                  .weak = s->bind == STB_WEAK && s->shidx == SHN_UNDEF, .load = load, .got = got};
      if(s->bind != STB_LOCAL) {
        if(!(h.name = elfs_name(self, o, str, s))) flop("a reference to a symbol without a name\n");
      } else if(s->shidx >= n || put[s->shidx].off == GONE.off) {
        flop("a reference into a section left out\n");
      } else {
        seat q = put[s->shidx];
        h.aim = q.at? (seat){q.at + s->value, 0}: (seat){NULL, q.off + (u32i)s->value};
      }
      self->hooks = swell(self, self->hooks, self->hookn, &self->hookc, sizeof(hook), 256);
      self->hooks[self->hookn++] = h;
    }
  }
  self->heap(put, 0);
}
#undef flop

enum { STT_NOTYPE, STT_OBJECT, STT_FUNC, STT_SECTION };
enum { STV_PROTECTED = 3 };
enum {
  R_AARCH64_ABS64 = 257, R_AARCH64_MOVW_PREL_G0_NC = 288, R_AARCH64_MOVW_PREL_G1_NC = 290,
  R_AARCH64_GLOB_DAT = 1025, R_AARCH64_RELATIVE = 1027,
};
enum { DT_NULL, DT_NEEDED, DT_HASH = 4, DT_STRTAB, DT_SYMTAB, DT_RELA, DT_RELASZ, DT_RELAENT, DT_STRSZ,
       DT_SYMENT, DT_SONAME = 14 };
enum { PT_LOAD = 1, PT_DYNAMIC, PT_INTERP, PT_NOTE, PT_SHLIB, PT_PHDR, PT_GNU_STACK = 0x6474e551 };

typedef struct { s64i tag; u64i val; } dyn64;

// a name a library leaves undefined: weak when every reference to it is
typedef struct { item* name; bool weak; byte type; } elfs_und;

// the symbols of a library: its public definitions, then the names it leaves
// undefined, each once (the imports first, in slot order), and the bytes of
// their names with a NUL after each
typedef struct {
  meta**    defs;
  elfs_und* unds;
  u32i      ndef, nund, cund;
  dex       undx; // the undefined ones, by spelling
  u64i      strs;
} elfs_syms;

// the index of name among the undefined ones, added when new
// whether undefined name i of the syms ctx[0] is spelled as the item ctx[1]
static bool  elfs_same(const void* ctx, u32i i) {
  const elfs_syms* t = ((const void**)ctx)[0];
  const item* name = ((const void**)ctx)[1];
  return t->unds[i].name->size == name->size && alike(t->unds[i].name->name, name->name, name->size);
}

static u32i  elfs_need(const mode* self, elfs_syms* t, item* name, bool weak, byte type) {
  const void* ctx[] = {t, name};
  u32i* at = dsee(self, &t->undx, clue(name->name, name->size), elfs_same, ctx);
  if(*at) { t->unds[*at - 1].weak &= weak; return *at - 1; }
  *at = t->nund + 1;
  t->unds = swell(self, t->unds, t->nund, &t->cund, sizeof(elfs_und), 16);
  t->unds[t->nund++] = (elfs_und){name, weak, type};
  t->strs += name->size + 1;
  return t->nund - 1;
}

// the definitions of a library (a program has none: nothing links to it), and
// the imports as undefined names
static void  elfs_sign(mode* self, elfs_syms* t) {
  *t = (elfs_syms){.strs = 1};
  if(!self->main) t->ndef = self->core.sign(self, &t->defs);
  for(u32i i = 0; i < t->ndef; i++) t->strs += t->defs[i]->name->size + 1;
  for(u32i i = 0; i < self->impn; i++)
    elfs_need(self, t, self->imps[i]->name, 0, self->imps[i]->kind == call? STT_FUNC: STT_OBJECT);
}

// the name of s at str + *at, NUL ended: its offset
static u32i  elfs_word(byte* str, u64i* at, const item* s) {
  u32i o = (u32i)*at;
  str[o + s->size] = 0;
  copy(str + o, s->name, s->size);
  *at += s->size + 1;
  return o;
}

// sym, the symbols of t from first on, their names in str past its NUL, a
// definition in text (a function at its entry for C code), data or bss at its
// address there and in section tsec, dsec or bsec. A shared library (dyno)
// reaches its globals directly: they are protected, so that no program copies
// them away from it
static void  elfs_syms_put(const mode* self, const elfs_syms* t, sym64* sym, byte* str, bool dyno,
                           u64i text, u64i data, u64i bss, u16i tsec, u16i dsec, u16i bsec) {
  u64i at = 1;
  for(u32i i = 0; i < t->ndef; i++, sym++) {
    const meta* m = t->defs[i];
    bool  f = m->kind == call, z = !f && m->slot & ZERO;
    *sym = (sym64){
      .name  = elfs_word(str, &at, m->name),
      .type  = f? STT_FUNC: STT_OBJECT,
      .bind  = m->weak? STB_WEAK: STB_GLOBAL,
      .other = dyno && !f? STV_PROTECTED: 0,
      .shidx = f? tsec: z? bsec: dsec,
      .value = f? text + ((m->hail? m->hail: m->addr) - self->text->head): (z? bss: data) + (m->slot & ~ZERO),
      .size  = !f && m->type? m->type->size: 0,
    };
  }
  for(u32i i = 0; i < t->nund; i++, sym++)
    *sym = (sym64){.name = elfs_word(str, &at, t->unds[i].name), .type = t->unds[i].type,
                   .bind = t->unds[i].weak? STB_WEAK: STB_GLOBAL};
}

// the offset of the k-th name in a string table of NUL separated names
static u32i  elfs_nth(const char* s, u32i k) {
  u32i i = 1;
  while(k--) i += (u32i)slen(s + i) + 1;
  return i;
}

// the section of seat s in a library object, 1 text, 2 data or 3 bss, and
// its offset there. The import slots follow data at slots
static u16i  elfs_part(const mode* self, seat s, u64i slots, u64i* off) {
  if(s.at) { *off = s.at - self->text->head; return 1; }
  if(s.off & ZERO) { *off = s.off & ~ZERO; return 3; }
  *off = s.off & SLOT? slots + 8 * (s.off & ~SLOT): s.off;
  return 2;
}

// a static library: the image as one relocatable object, text, data (then the
// import slots) and bss its sections. Symbols: the three sections, the public
// definitions, then the names left undefined. A reference within a section is
// patched, one across sections or to a name nothing defines is relocated
static file* elfs_relo(mode* self) {
  static char names[] = "\0.text\0.data\0.bss\0.symtab\0.strtab\0.rela.text\0.rela.data\0.shstrtab";
  enum { s_text = 1, s_data, s_bss, s_sym, s_str, s_rtext, s_rdata, s_names, s_all };
  const bool arm  = self->arch.cpu == cpu_arm8;
  const u64i step = self->core.gap(self);
  const u64i code = self->text->body - self->text->head;
  const u64i size = self->data->body - self->data->head;
  const u64i slots = (size + 7) & -8;
  elfs_syms t;
  elfs_sign(self, &t);
  cord* cords;
  u32i  ncord = self->core.knit(self, &cords);
  for(cord* c = cords; c < cords + ncord; c++)
    if(c->name) elfs_need(self, &t, c->name, c->weak, STT_NOTYPE);
  const u32i first = 1 + s_bss; // past the null symbol and those of the sections
  const u32i nsym = first + t.ndef + t.nund;

  // relocations, of text and of data: two per glob on arm8, one per slot
  u32i    nrel[2] = {0};
  rela64* rel[2];
  for(u32i k = 0; k < 2; k++) rel[k] = self->heap(NULL, (2 * ncord + self->impn) * sizeof(rela64) + 1);
#define RELA(sec, ...) (rel[(sec) - s_text][nrel[(sec) - s_text]++] = (rela64){__VA_ARGS__})
  for(cord* c = cords; c < cords + ncord; c++) {
    u64i so, to = 0;
    u16i ss = elfs_part(self, c->site, slots, &so), ts = 0;
    u32i sym = c->name? first + t.ndef + elfs_need(self, &t, c->name, c->weak, 0)
                      : (ts = elfs_part(self, c->aim, slots, &to));
    byte* at = c->site.at? c->site.at: self->data->head + c->site.off;
    if(c->kind == cd_glob && arm) {
      RELA(ss, so + 4, R_AARCH64_MOVW_PREL_G0_NC, sym, to + 4);
      RELA(ss, so + 8, R_AARCH64_MOVW_PREL_G1_NC, sym, to + 8);
    }
    else if(c->kind == cd_glob) RELA(ss, so + 3, R_X86_64_PC32, sym, to - 4);
    else if(c->kind == cd_wide) RELA(ss, so, R_X86_64_64, sym, to + c->add);
    else if(ts == ss) { // within a section: the distance is known
      s64i d = (s64i)to + c->add - (s64i)so;
      s32i r = (s32i)d;
      if(r != d) fail("object reference too far\n");
      copy(at, &r, sizeof(r));
    }
    else if(c->name && c->got) { // the lea a GOT load: back to the load
      at[-2] = 0x8b;
      RELA(ss, so, R_X86_64_GOTPCREL, sym, c->add);
    }
    else RELA(ss, so, c->name && ss == s_text && (at[-1] == 0xe8 || at[-1] == 0xe9)?
                      R_X86_64_PLT32: R_X86_64_PC32, sym, to + c->add);
  }
  for(u32i i = 0; i < self->impn; i++)
    RELA(s_data, slots + 8 * i, arm? R_AARCH64_ABS64: R_X86_64_64,
         first + t.ndef + elfs_need(self, &t, self->imps[i]->name, 0, 0), 0);
#undef RELA
  self->heap(cords, 0);

  // the file: header, text, data, symbols, their names, relocations, the
  // section names and headers
  sh64 sh[s_all] = {{0},
    {.type = SHT_PROGBITS, .flags = SHF_ALLOC | SHF_EXECINSTR, .size = code, .align = step},
    {.type = SHT_PROGBITS, .flags = SHF_ALLOC | SHF_WRITE, .size = slots + 8 * self->impn, .align = step},
    {.type = SHT_NOBITS, .flags = SHF_ALLOC | SHF_WRITE, .size = self->zero, .align = step},
    {.type = SHT_SYMTAB, .size = nsym * sizeof(sym64), .link = s_str, .info = first,
     .align = 8, .entsize = sizeof(sym64)},
    {.type = SHT_STRTAB, .size = t.strs, .align = 1},
    {.type = SHT_RELA, .flags = SHF_INFO_LINK, .size = nrel[0] * sizeof(rela64), .link = s_sym,
     .info = s_text, .align = 8, .entsize = sizeof(rela64)},
    {.type = SHT_RELA, .flags = SHF_INFO_LINK, .size = nrel[1] * sizeof(rela64), .link = s_sym,
     .info = s_data, .align = 8, .entsize = sizeof(rela64)},
    {.type = SHT_STRTAB, .size = sizeof names, .align = 1},
  };
  u64i at = sizeof(elfs_id) + sizeof(eh64);
  for(u32i i = 1; i < s_all; i++) {
    sh[i].name = elfs_nth(names, i - 1);
    at = (at + sh[i].align - 1) & ~(sh[i].align - 1);
    sh[i].offset = at;
    if(sh[i].type != SHT_NOBITS) at += sh[i].size;
  }
  const u64i shoff = (at + 7) & -8, all = shoff + sizeof sh;

  elfs_id id = {.seal = {0x7f, 'E', 'L', 'F'}, .bits = eb_cl64, .data = ed_2lsb, .issue = ev_curr};
  eh64 eh = {
    .type   = et_relo,
    .arch   = elfs_mach[self->arch.cpu],
    .issue  = ev_curr,
    .shoff  = shoff,
    .ehsize = sizeof(id) + sizeof(eh),
    .shsize = sizeof(sh64),
    .shnum  = s_all,
    .shstr  = s_names,
  };
  file* out = zero(self, sizeof(file) + all);
  byte* o = out->head;
  out->body = out->tail = o + all;
  copy(copy(o, &id, sizeof(id)), &eh, sizeof(eh));
  copy(o + sh[s_text].offset, self->text->head, code);
  copy(o + sh[s_data].offset, self->data->head, size);
  sym64* sym = (sym64*)(o + sh[s_sym].offset);
  for(u16i i = s_text; i <= s_bss; i++) sym[i] = (sym64){.type = STT_SECTION, .shidx = i};
  elfs_syms_put(self, &t, sym + first, o + sh[s_str].offset, 0, 0, 0, 0, s_text, s_data, s_bss);
  copy(o + sh[s_rtext].offset, rel[0], sh[s_rtext].size);
  copy(o + sh[s_rdata].offset, rel[1], sh[s_rdata].size);
  copy(o + sh[s_names].offset, names, sizeof names);
  copy(o + shoff, sh, sizeof sh);
  for(u32i k = 0; k < 2; k++) self->heap(rel[k], 0);
  self->heap(t.defs, 0);
  self->heap(t.unds, 0);
  self->heap(t.undx.slot, 0);
  return out;
}

// the file the loader opens for library l of a load, into out when given: its
// length. As written when it has a dot ("libz.so.1"), glibc's soname for c
// and m, else lib<name>.so: musl's loader takes libc.so and libm.so as itself,
// and bionic's libraries are named so
static u32i  elfs_file(const meta* l, clib c, char* out) {
  static const char* const glibc[][2] = {{"c", "libc.so.6"}, {"m", "libm.so.6"}};
  const char* s = l->name->name + 1; // without the quotes
  u32i n = l->name->size - 2;
  bool as_is = 0;
  for(u32i i = 0; i < n; i++) as_is |= s[i] == '.';
  for(u32i i = 0; !as_is && c == cl_glibc && i < sizeof glibc / sizeof *glibc; i++)
    if(slen(glibc[i][0]) == n && alike(glibc[i][0], s, n)) s = glibc[i][1], n = slen(s), as_is = 1;
  if(as_is) { if(out) copy((byte*)out, s, n); return n; }
  if(out) copy(copy(copy((byte*)out, "lib", 3), s, n), ".so", 3);
  return n + 6;
}

// an image, position independent: its dynamic tables, then text, in the first
// segment, the dynamic section, data, bss and the import slots in a second,
// writable one, 64 KB further in memory. A shared library (without main)
// exports its public definitions, has its imports bound by the loader through
// their slots, and its addresses in data relocated by the load address. A
// program is the same image entered at the start of text, with no soname and
// no exports, and names its loader (PT_INTERP), after the table describing itself
// (PT_PHDR). Both name the libraries of their imports (DT_NEEDED). Neither can
// have an address in code
static file* elfs_dyno(mode* self) {
  static char names[] = "\0.hash\0.dynsym\0.dynstr\0.rela.dyn\0.text\0.dynamic\0.data\0.bss\0.shstrtab";
  enum { s_hash = 1, s_sym, s_str, s_rel, s_text, s_dyn, s_data, s_bss, s_names, s_all };
  const bool arm  = self->arch.cpu == cpu_arm8;
  const u64i step = self->core.gap(self);
  const u64i code = self->text->body - self->text->head;
  const u64i size = self->data->body - self->data->head;
  const bool prog = self->main != NULL;
  const u64i nph  = prog? 6: 4;
  char*      ldso = elfs_loader[self->arch.cpu][self->clib];
  const u64i ldsn = prog? slen(ldso) + 1: 0;
  elfs_syms t;
  elfs_sign(self, &t);
  cord* cords;
  u32i  ncord = self->core.knit(self, &cords), nrel = self->impn;
  for(const cord* c = cords; c < cords + ncord; c++) {
    if(c->kind != cd_wide || c->name) continue; // a weak name nothing defines is 0
    if(c->site.at) fail("an address in code: %s cannot have it\n", prog? "a program": "a shared library");
    nrel++;
  }
  const char* soname = self->outp && !prog? self->outp: "";
  for(const char* c = soname; *c; c++) if(*c == '/') soname = c + 1;
  const u64i nsym = 1 + t.ndef + t.nund, sonamen = slen(soname);
  const meta** need = self->heap(NULL, (self->impn + 1) * sizeof(meta*)); // each library once
  u32i  nneed = 0;
  u64i  needs = 0;
  for(u32i i = 0; i < self->impn; i++) {
    u32i k = 0;
    while(k < nneed && need[k] != self->imps[i]->lib) k++;
    if(k == nneed) needs += elfs_file(need[nneed++] = self->imps[i]->lib, self->clib, NULL) + 1;
  }
  const u64i strs = t.strs + (prog? 0: sonamen + 1) + needs, ndyn = (prog? 9: 10) + nneed;

  // the first segment, then the second at rw in the file, data at dat
  const u64i tabs = sizeof(elfs_id) + sizeof(eh64);
  const u64i hash = (tabs + nph * sizeof(ph64) + ldsn + 7) & -8; // the loader's path before it
  const u64i dsym = (hash + 4 * (3 + nsym) + 7) & -8;
  const u64i dstr = dsym + nsym * sizeof(sym64);
  const u64i drel = (dstr + strs + 7) & -8;
  const u64i top  = (drel + nrel * sizeof(rela64) + step - 1) & ~(step - 1);
  const u64i rw   = (top + code + 7) & -8;
  const u64i dat  = (rw + ndyn * sizeof(dyn64) + step - 1) & ~(step - 1);
  const u64i bss  = ((size + step - 1) & ~(step - 1)), slots = bss + ((self->zero + 7) & -8);
  const u64i skew = 0x10000;
  const u64i shoff = (dat + size + sizeof names + 7) & -8, all = shoff + s_all * sizeof(sh64);
  self->core.dock(self, dat + skew - top, top);

  elfs_id id = {.seal = {0x7f, 'E', 'L', 'F'}, .bits = eb_cl64, .data = ed_2lsb, .issue = ev_curr};
  eh64 eh = {
    .type   = et_dyno,
    .arch   = elfs_mach[self->arch.cpu],
    .issue  = ev_curr,
    .entry  = prog? top: 0, // the boot stub starts text
    .phoff  = sizeof(id) + sizeof(eh),
    .shoff  = shoff,
    .ehsize = sizeof(id) + sizeof(eh),
    .phsize = sizeof(ph64),
    .phnum  = (u16i)nph,
    .shsize = sizeof(sh64),
    .shnum  = s_all,
    .shstr  = s_names,
  };
  const u64i intr = tabs + nph * sizeof(ph64);
  ph64 ph[] = { // a library has the last four
    {.type = PT_PHDR, .flags = 4, .offset = tabs, .vaddr = tabs, .paddr = tabs,
     .filesz = nph * sizeof(ph64), .memsz = nph * sizeof(ph64), .align = 8},
    {.type = PT_INTERP, .flags = 4, .offset = intr, .vaddr = intr, .paddr = intr,
     .filesz = ldsn, .memsz = ldsn, .align = 1},
    {.type = PT_LOAD, .flags = 5, .filesz = top + code, .memsz = top + code, .align = 0x1000},
    {.type = PT_LOAD, .flags = 6, .offset = rw, .vaddr = rw + skew, .paddr = rw + skew,
     .filesz = dat + size - rw, .memsz = dat - rw + slots + 8 * self->impn, .align = skew},
    {.type = PT_DYNAMIC, .flags = 6, .offset = rw, .vaddr = rw + skew, .paddr = rw + skew,
     .filesz = ndyn * sizeof(dyn64), .memsz = ndyn * sizeof(dyn64), .align = 8},
    {.type = PT_GNU_STACK, .flags = 6, .align = 16},
  };
  const dyn64 rest[] = {
    {DT_HASH, hash}, {DT_STRTAB, dstr}, {DT_SYMTAB, dsym}, {DT_STRSZ, strs},
    {DT_SYMENT, sizeof(sym64)}, {DT_RELA, drel}, {DT_RELASZ, nrel * sizeof(rela64)},
    {DT_RELAENT, sizeof(rela64)}, {DT_NULL, 0},
  };
  sh64 sh[s_all] = {{0},
    {.type = SHT_HASH, .flags = SHF_ALLOC, .addr = hash, .offset = hash, .size = 4 * (2 + nsym + 1),
     .link = s_sym, .align = 8, .entsize = 4},
    {.type = SHT_DYNSYM, .flags = SHF_ALLOC, .addr = dsym, .offset = dsym, .size = nsym * sizeof(sym64),
     .link = s_str, .info = 1, .align = 8, .entsize = sizeof(sym64)},
    {.type = SHT_STRTAB, .flags = SHF_ALLOC, .addr = dstr, .offset = dstr, .size = strs, .align = 1},
    {.type = SHT_RELA, .flags = SHF_ALLOC, .addr = drel, .offset = drel, .size = nrel * sizeof(rela64),
     .link = s_sym, .align = 8, .entsize = sizeof(rela64)},
    {.type = SHT_PROGBITS, .flags = SHF_ALLOC | SHF_EXECINSTR, .addr = top, .offset = top, .size = code,
     .align = step},
    {.type = SHT_DYNAMIC, .flags = SHF_ALLOC | SHF_WRITE, .addr = rw + skew, .offset = rw,
     .size = ndyn * sizeof(dyn64), .link = s_str, .align = 8, .entsize = sizeof(dyn64)},
    {.type = SHT_PROGBITS, .flags = SHF_ALLOC | SHF_WRITE, .addr = dat + skew, .offset = dat, .size = size,
     .align = step},
    {.type = SHT_NOBITS, .flags = SHF_ALLOC | SHF_WRITE, .addr = dat + skew + bss, .offset = dat + size,
     .size = slots - bss + 8 * self->impn, .align = step},
    {.type = SHT_STRTAB, .offset = dat + size, .size = sizeof names, .align = 1},
  };
  for(u32i i = 1; i < s_all; i++) sh[i].name = elfs_nth(names, i - 1);

  file* out = zero(self, sizeof(file) + all);
  byte* o = out->head;
  out->body = out->tail = o + all;
  copy(copy(copy(o, &id, sizeof(id)), &eh, sizeof(eh)), ph + (6 - nph), nph * sizeof(ph64));
  copy(o + intr, (byte*)ldso, ldsn);
  u32i* h = (u32i*)(o + hash); // one bucket: a chain through every symbol
  h[0] = 1;
  h[1] = (u32i)nsym;
  h[2] = nsym > 1;
  for(u32i i = 1; i + 1 < nsym; i++) h[3 + i] = i + 1;
  elfs_syms_put(self, &t, (sym64*)(o + dsym) + 1, o + dstr, 1, top, dat + skew, dat + skew + bss,
                s_text, s_data, s_bss);
  copy(o + dstr + t.strs, soname, sonamen);
  dyn64* d = (dyn64*)(o + rw); // a program has no soname
  if(!prog) *d++ = (dyn64){DT_SONAME, t.strs};
  for(u64i k = 0, at = t.strs + (prog? 0: sonamen + 1); k < nneed; k++) {
    *d++ = (dyn64){DT_NEEDED, at};
    at += elfs_file(need[k], self->clib, (char*)o + dstr + at) + 1;
  }
  copy((byte*)d, rest, sizeof rest);
  rela64* r = (rela64*)(o + drel);
  for(const cord* c = cords; c < cords + ncord; c++) { // dock put the address at base 0 there
    if(c->kind != cd_wide || c->name) continue;
    u64i a;
    copy((byte*)&a, self->data->head + c->site.off, sizeof(a));
    *r++ = (rela64){dat + skew + c->site.off, arm? R_AARCH64_RELATIVE: R_X86_64_RELATIVE, 0, a};
  }
  for(u32i i = 0; i < self->impn; i++)
    *r++ = (rela64){dat + skew + slots + 8 * i, arm? R_AARCH64_GLOB_DAT: R_X86_64_GLOB_DAT,
                    1 + t.ndef + elfs_need(self, &t, self->imps[i]->name, 0, 0), 0};
  copy(o + top, self->text->head, code);
  copy(o + dat, self->data->head, size);
  copy(o + dat + size, names, sizeof names);
  copy(o + shoff, sh, sizeof sh);
  self->heap(cords, 0);
  self->heap(need, 0);
  self->heap(t.defs, 0);
  self->heap(t.unds, 0);
  self->heap(t.undx.slot, 0);
  return out;
}

// a program or a shared library as one image, or without main a static
// library, an object
file* elfs_pack(mode* self) {
  return !self->main && self->libs == lb_relo? elfs_relo(self): elfs_dyno(self);
}

const form elfs = {
  .pack = elfs_pack,
  .take = elfs_take,
  .os   = os_linux,
};
