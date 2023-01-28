// vim: ts=2:sw=2
#include "core.h"

enum class { b32 = 1, b64 };
enum data  { lsb = 1, msb };
enum version { current = 1 };
enum type  { rel = 1, exe, dyn, core };
enum machine {
  _ia64 = 62,
  _arm8 = 183,
};

typedef union {
    byte _[16];
    struct { byte magic[4], class, data, version, osabi, abiver; };
} elfs_id;

#define T(B) u##B##i

#define ehdr(B)                                                               \
typedef struct {                                                              \
  u16i type, machine;                                                         \
  u32i version;                                                               \
  T(B) entry, phoff, shoff;                                                   \
  u32i flags;                                                                 \
  u16i ehsize, phsize, phnum, shsize, shnum, shstr;                           \
} eh##B
ehdr(32);
ehdr(64);
#undef ehdr

//*(hdr*)(P+S) = (hdr) {
//    .id.magic   = {0x7F, 'E', 'L', 'F'},
//    .id.version = 1,
//    .version    = 1,
//    .entry      = 0x400000+sizeof(hdr)+sizeof(ph##B),
//    .phoff      = sizeof(hdr),
//    .ehsize     = sizeof(hdr),
//    .phsize     = sizeof(ph##B),
//    __VA_ARGS__ };
//  S += sizeof(hdr);

typedef struct {
  u32i type, offset, vaddr, paddr, filesz, memsz, flags, align;
} ph32;
typedef struct {
  u32i type, flags; u64i offset, vaddr, paddr, filesz, memsz, align;
} ph64;

// name is sring table index

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

void elfs(mode* self) {
  self->code = scan(self->name);
  elfs_id* id = (elfs_id*)self->code->head;
  printf("class   %d\n", id->class);
  printf("data    %d\n", id->data);
  printf("osabi   %d\n", id->osabi);
  printf("abiver  %d\n", id->abiver);
  if(id->class == b64) {
    eh64* ehdr = (eh64*)(self->code->head + sizeof(elfs_id));
    sh64* shdr = (sh64*)(self->code->head + ehdr->shoff);
    for(int i = 0; i < ehdr->shnum; i++) {
      printf("[%2d] %4ld %s\n", i, shdr[i].size, self->code->head + shdr[ehdr->shstr].offset + shdr[i].name);
      if(shdr[i].type == SHT_SYMTAB) {
        sym64* sym = (sym64*)(self->code->head + shdr[i].offset);
        for(size_t j = 0, all = shdr[i].size / shdr[i].entsize; j < all; j++) {
          printf("  [%2ld] %4ld %s\n", j, sym[j].size, self->code->head + shdr[shdr[i].link].offset + sym[j].name);
        }
      }
    }
  }
}

