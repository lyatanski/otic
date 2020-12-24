// vim: ts=2:sw=2
#ifdef _WIN32
#include <memoryapi.h> /* for VirtualAlloc */
#else
#include <sys/mman.h>  /* for mmap         */
#endif
#include "core.h"

void* hold(int size) {
#ifdef _WIN32
  return VirtualAlloc(0, size,
    MEM_COMMIT | MEM_RESERVE,
    PAGE_READWRITE);
#else
  return mmap(/*addr=*/0, size,
    PROT_READ | PROT_WRITE | PROT_EXEC,
    MAP_ANONYMOUS | MAP_PRIVATE, /*fd=*/-1, /*offset=*/0);
#endif
}

void prep(void* memory, int size) {
#ifdef _WIN32
  VirtualProtect(memory, size, PAGE_EXECUTE_READ, 0);
#else
  mprotect(memory, size, PROT_EXEC);
#endif
}

void drope(void* memory, int size) {
#ifdef _WIN32
  VirtualFree(address, 0, MEM_RELEASE);
#else
  munmap(memory, size);
#endif
}

typedef int (*func)();
void exec(void* memory) {
  int return_code = ((func)memory)();
  printf("%d\n", return_code);
}

