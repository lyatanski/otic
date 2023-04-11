// vim: ts=2:sw=2
#include "core.h"

static const u32i bits[] = {0, 32, 64};
enum { bits_none, bits_32 = 1, bits_64 };
static const char* elf_data[] = {"", "lsb", "msb"};
enum { data_none, data_2lsb, data_2msb };
enum { issue_current = 1 };
enum { type_none, type_rel, type_exe, type_dyn, type_core };
enum { arch_ia64 = 62, arch_arm8 = 183, };

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

//*(hdr*)(P+S) = (hdr) {
//    .id.seal    = {0x7F, 'E', 'L', 'F'},
//    .id.issue   = issue_current,
//    .issue      = issue_current,
//    .entry      = 0x400000+sizeof(hdr)+sizeof(ph##B),
//    .phoff      = sizeof(hdr),
//    .ehsize     = sizeof(hdr),
//    .phsize     = sizeof(ph##B),
//    __VA_ARGS__ };
//  S += sizeof(hdr);

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

void elfs(mode* self) {
  self->code = scan(self->name);
  elfs_id* id = (elfs_id*)self->code->head;
  printf("bits    %d\n", bits[id->bits]);
  printf("data    %s\n", elf_data[id->data]);

  if(id->bits == bits_64) elfs_sh64(self);
}

