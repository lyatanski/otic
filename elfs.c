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

void elfs_sh64(mode* self) {
  eh64* ehdr = (eh64*)(self->code->head + sizeof(elfs_id));
  sh64* sect = (sh64*)(self->code->head + ehdr->shoff);
  for(u16i i = 0; i < ehdr->shnum; i++) {
    printf("[%2d] %4ld %s\n", i, sect[i].size, self->code->head + sect[ehdr->shstr].offset + sect[i].name);
    if(sect[i].type == SHT_SYMTAB) {
      sym64* sym = (sym64*)(self->code->head + sect[i].offset);
      for(u64i j = 0, all = sect[i].size / sect[i].entsize; j < all; j++) {
        printf("  [%2ld] %4ld %s\n", j, sym[j].size, self->code->head + sect[sect[i].link].offset + sym[j].name);
      }
    }
  }
}

file* elfs_pack(mode* self) {
  const u64i org = 0x400000;
  const u64i top = sizeof(elfs_id) + sizeof(eh64) + sizeof(ph64);
  const u64i all = top + (self->text->body - self->text->head);

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
    .phnum  = 1,
  };
  ph64 ph = {
    .type   = 1, // PT_LOAD
    .flags  = 5, // PF_R | PF_X
    .vaddr  = org,
    .paddr  = org,
    .filesz = all,
    .memsz  = all,
    .align  = 0x1000,
  };

  file* out = malloc(sizeof(file) + all);
  if(!out) fail("out of memory\n");
  out->body = out->head;
  out->tail = out->head + all;
#define put(src, size) \
  { out->body = copy(out->body, src, size); }
  put(&id, sizeof(id));
  put(&eh, sizeof(eh));
  put(&ph, sizeof(ph));
  put(self->text->head, all - top);
#undef put
  return out;
}

const form elfs = {
  .pack = elfs_pack,
};
