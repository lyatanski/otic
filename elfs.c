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

const form elfs = {
  .pack = elfs_pack,
};
