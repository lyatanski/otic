// vim: ts=2:sw=2
#include "core.h"

#ifndef part
#include "core.c"
#include "hash.c"

#include "ia64.c"
#include "arm8.c"

#include "elfs.c"
//#include "pe32.c"
//#include "mach.c"
#endif

#ifdef _WIN32
#include <memoryapi.h>  /* for VirtualAlloc */
#else
#include <sys/mman.h>   /* for mmap         */
#endif

extern const emit ia64;
extern const emit arm8;
#ifdef __aarch64__
#define base arm8
#else // __x86_64__
#define base ia64
#endif

void* hold(void* memory, size_t size) {
#ifdef _WIN32
  return VirtualAlloc(0, size,
    MEM_COMMIT | MEM_RESERVE,
    PAGE_READWRITE);
#else
  return mmap(/*addr=*/0, size,
    PROT_READ | PROT_WRITE | PROT_EXEC,
    MAP_ANONYMOUS | MAP_PRIVATE, /*fd=*/-1, /*offset=*/0);
#endif
  return NULL;
}

void prep(void* memory, size_t size) {
#ifdef _WIN32
  VirtualProtect(memory, size, PAGE_EXECUTE_READ, 0);
#else
  mprotect(memory, size, PROT_EXEC);
#endif
}

void* drop(void* memory, size_t size) {
#ifdef _WIN32
  VirtualFree(memory, 0, MEM_RELEASE);
#else
  munmap(memory, size);
#endif
  return NULL;
}

typedef int (*func)();
void exec(buff* data) {
  //prep(data->head, 0);
  int return_code = ((func)data->head)();
  printf("%d\n", return_code);
}

void  show(buff* data) {
  for(byte* b = data->head; b < data->tail; b++)
    printf("%.2x", *b);
  printf("\n");
}

int   main(int argc, char** argv) {
  mode self = {};

  if(argc != 3) return 1;
  self.hold = hold;
  self.drop = drop;
  self.exec = show;
  switch(argv[1][0]) {
    case 'a': self.arch = arm8; break;
    case 'i': self.arch = ia64; break;
    case 'e': self.arch = base; self.exec = exec; break;
    default : return 1;
  }

  self.name = argv[2];
  make(&self);

  self.name = "bins/ia64.o";
  elfs(&self);

  return 0;
}

