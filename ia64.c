#include "core.h"

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

#define rex(...) (*(uint8_t*)&(struct { uint8_t b:1, x:1, r:1, w:1, _:4; }){._=4, __VA_ARGS__})
#define modrm(...) (*(uint8_t*)&(struct { uint8_t rm:3, reg:3, mod:2; }){__VA_ARGS__})

int ib(char* exe, uint8_t  val) { *(typeof(val)*)exe = val; return sizeof(val); }
int iw(char* exe, uint16_t val) { *(typeof(val)*)exe = val; return sizeof(val); }
int id(char* exe, uint32_t val) { *(typeof(val)*)exe = val; return sizeof(val); }
int io(char* exe, uint64_t val) { *(typeof(val)*)exe = val; return sizeof(val); }

void emit_i386() {
  printf("i386\n");
}

void ia64_text(void* data) {
  char* exe = data;
  int size = 0;

  //mov eax, 42
  exe[size++] = mov + eax;
  size += id(exe+size, 42);

  //ret
  exe[size++] = ret;
}

emit ia64 = {
  .show = emit_i386,
  .text = ia64_text
};

