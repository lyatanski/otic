// vim: ts=2:sw=2
#ifdef _WIN32
#include <memoryapi.h> /* for VirtualAlloc */
#else
#include <sys/mman.h>  /* for mmap         */
#endif
#include <stdint.h>
#include <stdio.h>

typedef int (*func)();

typedef union {
  struct { uint32_t rd :5, i16:16, hw :2, op :6, opc:2, sf :1; };
  struct { uint32_t op4:5, rn : 5, op3:6, op2:5, opc:4, op :7; } br;
  uint32_t n;
} i;

const int size = 60;
int text(uint32_t* exe) {
  int size = 0;

  //mov  w0, #0x2a
  exe[size++] = (i){.op=0b100101, .opc=2, .i16=42}.n;
  //ret
  //exe[size++] = (i){.br={.op=0b1101011, .opc=0b0010, .op2=0b11111}}.n;
  exe[size++] = 0xd65f03c0;

  return size;
}

void* hold(int size) {
#ifdef _WIN32
  return VirtualAlloc(0, size, MEM_COMMIT | MEM_RESERVE, PAGE_READWRITE);
#else
  return mmap(
    /*addr=*/0, size,
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

void drop(void* memory, int size) {
#ifdef _WIN32
  VirtualFree(address, 0, MEM_RELEASE);
#else
  munmap(memory, size);
#endif
}

void exec(void* memory) {
  //func function = *(func *)&memory;
  //int return_code = function();
  int return_code = ((func)memory)();
  printf("%d\n", return_code);
}

int main() {

  void* memory = hold(size);
  text(memory);
  exec(memory);
  drop(memory, size);

	//i x = {.br={.op=0b1101011, .opc=0b0010, .op2=0b11111}};
	i x = {.br={.op=0b1101011, .opc=0b0010, .op2=0b11111, .rn=0b11110}};
  printf("%x\n", 0xd65f03c0);
  printf("%x\n", x.n);

  return 0;
}
