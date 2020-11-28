// vim: ts=2:sw=2
#ifdef _WIN32
#include <memoryapi.h> /* for VirtualAlloc */
#else
#include <sys/mman.h>  /* for mmap         */
#endif
#include <stdint.h>
#include <stdio.h>

typedef int (*func)();

#define rex(...) (*(uint8_t*)&(struct { uint8_t b:1, x:1, r:1, w:1, _:4; }){._=4, __VA_ARGS__})
#define modrm(...) (*(uint8_t*)&(struct { uint8_t rm:3, reg:3, mod:2; }){__VA_ARGS__})

int ib(char* exe, uint8_t  val) { *(typeof(val)*)exe = val; return sizeof(val); }
int iw(char* exe, uint16_t val) { *(typeof(val)*)exe = val; return sizeof(val); }
int id(char* exe, uint32_t val) { *(typeof(val)*)exe = val; return sizeof(val); }
int io(char* exe, uint64_t val) { *(typeof(val)*)exe = val; return sizeof(val); }

//int mov(char* exe, uint32_t val) {
//  int size = 0;
//  exe[size++] = rex(.w=1);
//  exe[size++] = 0xc7;
//  exe[size++] = (modrm){.mod=3}.a;
//  return size;
//}

int pick(char* exe, char* _when, char* _then, char* _else) {
  return 0;
}

//int seek(char* exe, char* _when, char* _then, char* _else) {
//}

int loop(char* exe, char* _when, char* _then             ) {
  return 0;
}

int call(char* exe                                       ) {
  return 0;
}

#define eax 0

#define mov    0xb8
#define ret    0xc3
#define jcs    0x70
#define jcl    0x0f80
#define    jo  0x0
#define    jno 0x1
#define    jb  0x2
#define    jae 0x3
#define    je  0x4
#define    jne 0x5
#define    jbe 0x6
#define    ja  0x7
#define    js  0x8
#define    jns 0x9
#define    jp  0xa
#define    jnp 0xb
#define    jl  0xc
#define    jge 0xd
#define    jle 0xe
#define    jg  0xf

const int size = 60;
int text(char* exe) {
  int size = 0;

  //mov eax, 42
  exe[size++] = mov + eax;
  size += id(exe+size, 42);

  //ret
  exe[size++] = ret;

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

  return 0;
}
