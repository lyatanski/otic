// vim: ts=2:sw=2
#include "core.h"

enum class { b32 = 1, b64 };
enum data  { lsb = 1, msb };
enum version { current = 1 };
enum type  { rel = 1, exec, dyn, core };
enum machine {
  ia64 = 62,
  arm8 = 183,
};

#define ehdr(B, P, S, ...) {                                                  \
  typedef u ## B ## i uXXi;                                                   \
  typedef struct {                                                            \
    union {                                                                   \
      byte id[16];                                                            \
      struct { byte magic[4], class, data, version, osabi, abiver; };         \
    } id;                                                                     \
    u16i type, machine;                                                       \
    u32i version;                                                             \
    uXXi entry, phoff, shoff;                                                 \
    u32i flags;                                                               \
    u16i ehsize, phsize, phnum, shsize, shnum, shstr;                         \
  } hdr;                                                                      \
  *(hdr*)(P+S) = (hdr) {                                                      \
    .id.magic   = {0x7F, 'E', 'L', 'F'},                                      \
    .id.version = 1,                                                          \
    .version    = 1,                                                          \
    .entry      = 0x400000+sizeof(hdr)+sizeof(ph ## B),                       \
    .phoff      = sizeof(hdr),                                                \
    .ehsize     = sizeof(hdr),                                                \
    .phsize     = sizeof(ph ## B),                                            \
    __VA_ARGS__ };                                                            \
  S += sizeof(hdr);                                                           \
}

typedef struct {
  u32i type, offset, vaddr, paddr, filesz, memsz, flags, align;
} ph32;
typedef struct {
  u32i type, flags; u64i offset, vaddr, paddr, filesz, memsz, align;
} ph64;
#define phdr(B, P, S, ...) {                                                  \
  *(ph ## B*)(P+S) = (ph ## B){__VA_ARGS__};                                  \
  S += sizeof(ph ## B);                                                       \
}

// name is sring table index

#define shdr(B)                                                               \
typedef struct {                                                              \
  u32i        name, type;                                                     \
  u ## B ## i flags, addr, offset, size;                                      \
  u32i        link, info;                                                     \
  u ## B ## i align, entsize;                                                 \
} sh ## B
shdr(32);
shdr(64);
#undef shdr

typedef struct {
  u32i name; u32i value; u32i size; byte type: 4, bind: 4, other; u16i shndx;
} sym32;
typedef struct {
  u32i name; byte type: 4, bind: 4, other; u16i shndx; u64i value; u64i size;
} sym64;

int example() {
  int  size = 0;
  char file[1024] = {};
  ehdr(64, file, size,
    .id.class = 2,
    .id.data  = 1,
    .type     = 2,
    .machine  = arm8,
    .phnum    = 1,
  );
  phdr(64, file, size,
    .type     = 1,
    .flags    = 5,
    .offset   = 0,
    .vaddr    = 0x400000,
    .paddr    = 0x400000,
    .filesz   = 0xc,
    .memsz    = 0xc,
    .align    = 0x10000,
  );
  //size += texta(file+size);
  FILE* fp = fopen("tiny64", "wb");
  fwrite(file, 1, size, fp);
  fclose(fp);
  return 0;
}

