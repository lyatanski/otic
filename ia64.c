#include "core.h"

enum {
  eax, ecx, edx,  ebx,  esp,  ebp,  esi,  edi,
  ir8, ir9, ir10, ir11, ir12, ir13, ir14, ir15,          // 64 bit only
  xmm0, xmm1, xmm2,  xmm3,  xmm4,  xmm5,  xmm6,  xmm7,
  xmm8, xmm9, xmm10, xmm11, xmm12, xmm13, xmm14, xmm15,  // 64 bit only
  rax=eax, rcx=ecx, rdx=edx, rbx=ebx, rsp=esp, rbp=ebp, rsi=esi, rdi=edi,
};

enum {
  mov  = 0xb8,
  ret  = 0xc3,
  cmp  = 0x83,
  call = 0xe8,
  jmp  = 0xeb,
  jcs  = 0x70,
  jcl  = 0x0f80
};

enum { jo, jno, jb, jae, je, jne, jbe, ja, js, jns, jp, jnp, jl, jge, jle, jg };

#define rex(...)   (*(uint8_t*)&(struct { uint8_t b:1, x:1, r:1, w:1, _:4; }){._=4, __VA_ARGS__})
#define modrm(...) (*(uint8_t*)&(struct { uint8_t rm:3, reg:3, mod:2; }){__VA_ARGS__})
#define sib(...)   (*(uint8_t*)&(struct { uint8_t base:3, idx:3, ss:2; }){__VA_ARGS__})

uint8_t mspe[] = {           rcx, rdx, ir8, ir9 };
uint8_t elfs[] = { rdi, rsi, rdx, rcx, ir8, ir9 };

#define BODY { *(typeof(val)*)data->tail = val; data->tail += sizeof(val); }
void ib(buff* data, uint8_t  val) BODY
void iw(buff* data, uint16_t val) BODY
void id(buff* data, uint32_t val) BODY
void io(buff* data, uint64_t val) BODY
#undef  BODY

void ia64_move(buff* data, uint32_t num) {
  ib(data, mov + eax);
  id(data, num);
}

void ia64_mend(buff* data) { *data->back = data->tail - data->back - 1; }
void ia64_done(buff* data) { ib(data, ret); }

void ia64_jump(buff* data, uint32_t fix) {
  ib(data, jmp);
  if(fix) *data->back = data->tail - data->back;
  data->back = data->tail++;
}

void ia64_when(buff* data, uint32_t cond) {
  ib(data, cmp);
  ib(data, modrm(.mod=3, .reg=7, .rm=eax));
  ib(data, 0);

  ib(data, jcs + je);
  data->back = data->tail++;
}

void ia64_args(buff* data, uint32_t ptr) {
}

void ia64_call(buff* data, uint32_t disp) {
  ib(data, mov+rax); id(data, 1);
  ib(data, mov+rdi); id(data, 1);
  ib(data, mov+rsi); id(data, disp);
  ib(data, mov+rdx); id(data, 7);
  iw(data, 0x050f);
}

void ia64_push(buff* data, uint32_t addr) {
}

void ia64_pull(buff* data, uint32_t addr) {
}

void ia64_math(buff* data, uint32_t oper) {
  switch(oper) {
    case '+': break;
    case '-': break;
    case '*': break;
    case '/': break;
    case '%': break;
  }
}

void ia64_bits(buff* data, uint32_t oper) {
  switch(oper) {
    case '&': break;
    case '|': break;
    case '^': break;
    case '~': break;
    case '<': break;
    case '>': break;
  }
}

void ia64_diff(buff* data, uint32_t oper) {
  switch(oper) {
    case '=': break;
    case '!': break;
    case '<': break;
    case '>': break;
    //case '>=': break;
    //case '<=': break;
  }
}

emit ia64 = {
  .move = ia64_move,
  .when = ia64_when,
  .jump = ia64_jump,
  .done = ia64_done,
  .back = ia64_mend,
  .args = ia64_args,
  .call = ia64_call,
};

