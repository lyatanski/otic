// vim: ts=2:sw=2
#include "core.h"

#define ehdr(B, P, S, ...) {                                                  \
  typedef uint ## B ## _t uintXX_t;                                           \
  typedef struct {                                                            \
    union {                                                                   \
      struct { uint8_t magic[4], class, data, ver, osabi, abiver; };          \
      uint8_t id[16];                                                         \
    };                                                                        \
    uint16_t type, machine;                                                   \
    uint32_t version;                                                         \
    uintXX_t entry, phoff, shoff;                                             \
    uint32_t flags;                                                           \
    uint16_t ehsize, phsize, phnum, shsize, shnum, shstr;                     \
  } hdr;                                                                      \
  *(hdr*)(P+S) = (hdr){{0x7F, 'E', 'L', 'F'},                                 \
  .entry    = 0x400000+sizeof(hdr)+sizeof(phdr ## B),                         \
  .phoff    = sizeof(hdr),                                                    \
  .ehsize   = sizeof(hdr),                                                    \
  .phsize   = sizeof(phdr ## B),                                              \
  __VA_ARGS__ };                                                              \
  S += sizeof(hdr);                                                           \
}

typedef struct {
  uint32_t type, offset, vaddr, paddr, filesz, memsz, flags, align;
} phdr32;
typedef struct {
  uint32_t type, flags; uint64_t offset, vaddr, paddr, filesz, memsz, align;
} phdr64;
#define phdr(B, P, S, ...) {                                                  \
  *(phdr ## B*)(P+S) = (phdr ## B){__VA_ARGS__};                              \
  S += sizeof(phdr ## B);                                                     \
}

int text(char* exe) {
  //mov rax, 231 ; sys_exit_group
  exe[ 0] = 0xb8;
  exe[ 1] = 0xe7;
  exe[ 2] = 0x00;
  exe[ 3] = 0x00;
  exe[ 4] = 0x00;

  //mov rdi, 42  ; int status
  exe[ 5] = 0xbf;
  exe[ 6] = 0x2a;
  exe[ 7] = 0x00;
  exe[ 8] = 0x00;
  exe[ 9] = 0x00;

  //syscall
  exe[10] = 0x0f;
  exe[11] = 0x05;
  return 12;
}

int main() {
  int  size = 0;
  char file[1024] = {};
  ehdr(64, file, size,
  .class    = 2,
  .data     = 1,
  .ver      = 1,
  .type     = 2,
  .machine  = 0x3E,
  .version  = 1,
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
  .align    = 0x1000,
  );
  size += text(file+size);
  FILE* fp = fopen("tiny64", "wb");
  fwrite(file, 1, size, fp);
  fclose(fp);
  return 0;
}
