// vim: ts=2:sw=2
#include "core.h"

typedef struct {
  union {
    struct {
      uint8_t  ident[4];
      uint8_t  class;
      uint8_t  data;
      uint8_t  ver;
      uint8_t  osabi;
      uint8_t  abiver;
    };
    uint8_t id[16];
  };
  uint16_t type;
  uint16_t machine;
  uint32_t version;
  uint64_t entry;
  uint64_t phoff;
  uint64_t shoff;
  uint32_t flags;
  uint16_t ehsize;
  uint16_t phsize;
  uint16_t phnum;
  uint16_t shsize;
  uint16_t shnum;
  uint16_t shstr;
} ehdr;

typedef struct {
  uint32_t type;
  uint32_t flags;
  uint64_t offset;
  uint64_t vaddr;
  uint64_t paddr;
  uint64_t filesz;
  uint64_t memsz;
  uint64_t align;
} phdr;

int ehdr64(char* exe) {
  ehdr* h;
  h = (ehdr*)exe;
  for(int i = 0; i < 16; i++) h->id[i] = 0;
  h->ident[0] = 0x7F;
  h->ident[1] = 'E';
  h->ident[2] = 'L';
  h->ident[3] = 'F';
  h->class    = 2;
  h->data     = 1;
  h->ver      = 1;
  h->osabi    = 0;
  h->abiver   = 0;
  h->type     = 2;
  h->machine  = 0x3E;
  h->version  = 1;
  h->entry    = 0x400000+sizeof(ehdr)+sizeof(phdr);
  h->phoff    = sizeof(ehdr);
  h->shoff    = 0;
  h->flags    = 0;
  h->ehsize   = sizeof(ehdr);
  h->phsize   = sizeof(phdr);
  h->phnum    = 1;
  h->shsize   = 0;
  h->shnum    = 0;
  h->shstr    = 0;
  return sizeof(ehdr);
}

int phdr64(char* exe) {
  phdr* h;
  h = (phdr*)exe;
  h->type   = 1;
  h->flags  = 5;
  h->offset = 0;
  h->vaddr  = 0x400000;
  h->paddr  = 0x400000;
  h->filesz = 0xc;
  h->memsz  = 0xc;
  h->align  = 0x1000;
  return sizeof(phdr);
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
  char file[1024];
  size += ehdr64(file+size);
  size += phdr64(file+size);
  size += text(file+size);
  FILE* fp = fopen("tiny64", "wb");
  fwrite(file, 1, size, fp);
  fclose(fp);
  return 0;
}
