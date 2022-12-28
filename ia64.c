#include "core.h"

enum {
  eax, ecx, edx,  ebx,  esp,  ebp,  esi,  edi,
  ir8, ir9, ir10, ir11, ir12, ir13, ir14, ir15,          // 64 bit only
  xmm0, xmm1, xmm2,  xmm3,  xmm4,  xmm5,  xmm6,  xmm7,
  xmm8, xmm9, xmm10, xmm11, xmm12, xmm13, xmm14, xmm15,  // 64 bit only
  rax=eax, rcx=ecx, rdx=edx, rbx=ebx, rsp=esp, rbp=ebp, rsi=esi, rdi=edi,
};

typedef enum { reg, mem, imm } kind;
typedef struct {
  kind type;
  union {
    uint64_t val;
    struct { uint8_t name:3, x64:1; } reg;
    struct { uint8_t base:3, idx:3, ss:2; } mem;
  };
} para;

enum { RM, OI, size };
enum { mov };
uint8_t opco[][size] = {
  [mov] = { [RM] = 0x89, [OI] = 0xb8, },
};

enum {
  push = 0x50,
  pop  = 0x58,
  ret  = 0xc3,
};

#define rex(...)   (*(uint8_t*)&(struct { uint8_t b:1, x:1, r:1, w:1, _:4; }){._=4, __VA_ARGS__})
#define modrm(...) (*(uint8_t*)&(struct { uint8_t rm:3, reg:3, mod:2; }){__VA_ARGS__})
#define sib(...)   (*(uint8_t*)&(struct { uint8_t base:3, idx:3, ss:2; }){__VA_ARGS__})

// some instructions have the following bit pattern xxxxxxDS where:
//   S - size of the operand 8/32
//   D - direction RM -> 1 / MR -> 0
#define flag(...)  (*(uint8_t*)&(struct { uint8_t size:1, rm:1, _:6; }){__VA_ARGS__})

uint8_t mspe[] = {           rcx, rdx, ir8, ir9 };
uint8_t elfs[] = { rdi, rsi, rdx, rcx, ir8, ir9 };

#define BODY { *(typeof(val)*)data->tail = val; data->tail += sizeof(val); }
void ib(buff* data, uint8_t  val) BODY
void iw(buff* data, uint16_t val) BODY
void id(buff* data, uint32_t val) BODY
void io(buff* data, uint64_t val) BODY
#undef  BODY

void ia64_mov(buff* data, para src, para dst) {
  int size = 4;

  if(size > 4) ib(data, rex(.w=1));
  if(imm == src.type) {
    if(reg != dst.type) fail("destination should be register");
    if(size > 4 && (uint32_t)src.val == src.val) {
      ib(data, 0xc6 | flag(.size=size > 1));
      ib(data, modrm(.mod=3, .reg=0, .rm=dst.reg.name));
      if(size == 1) ib(data, src.val); else id(data, src.val);
    }
    else {
      // b8 does not support reg64 from imm32
      ib(data, 0xb8 + dst.reg.name);
      if(size > 4) io(data, src.val); else id(data, src.val);
    }
  }
  else {
    int store = (dst.type != reg);
    ib(data, 0x88 | flag(.rm=store, .size=(size > 1)));
    ib(data, modrm(.mod=3, .reg=src.reg.name, .rm=dst.reg.name));
  }
}

// TODO remove
void ia64_move(buff* data, uint32_t val) {
  para src = {imm, .val=val};
  para dst = {reg, .reg = {eax}};
  ia64_mov(data, src, dst);
}

void ia64_mark(buff* data) { *data->back = data->tail - data->back - 1; }
void ia64_mend(buff* data) { *data->back = data->tail - data->back - 1; }
void ia64_done(buff* data) { ib(data, ret); }

// TODO remove
void ia64_args(buff* data, uint32_t ptr) {
}

// TODO remove
void ia64_body(buff* data, uint32_t disp) {
  ib(data, push + rbp);
  ib(data, rex(.w=1)); ib(data, opco[mov][RM]); ib(data, modrm(.mod=3, .reg=rsp, .rm=rbp));
  // funtion body
  ib(data, pop + rbp);
  ib(data, ret);
}

// TODO remove
void ia64_echo2(buff* data, uint32_t func, uint32_t addr) {
  ib(data, opco[mov][OI] + eax); id(data, func);
  ib(data, opco[mov][OI] + rdi); id(data, addr);
  ib(data, 0xff);
  ib(data, modrm(.mod=3, .reg=2, .rm=eax));
}

// TODO remove
void ia64_echo(buff* data, uint32_t addr) {
  ib(data, opco[mov][OI]+rax); id(data, 1);
  ib(data, opco[mov][OI]+rdi); id(data, 1);
  ib(data, opco[mov][OI]+rsi); id(data, addr);
  ib(data, opco[mov][OI]+rdx); id(data, 7);
  iw(data, 0x050f);
}

void ia64_jump(buff* data, uint32_t fix) {
  ib(data, 0xeb);
  if(fix) *data->back = data->tail - data->back;
  data->back = data->tail++;
}

typedef enum { jo, jno, jb, jae, je, jne, jbe, ja, js, jns, jp, jnp, jl, jge, jle, jg } cond;
void ia64_jmpc(buff* data, int32_t offs, cond oper) {
  if((int8_t)offs == offs) {
    ib(data, 0x70 + oper);
    ib(data, offs - 2);
  }
  else {
    iw(data, 0x0f80 + oper);
    id(data, offs - 6);
  }
}

// TODO remove
// XXX ia64_jmpc is the same
void ia64_when(buff* data, uint32_t cond) {
  ib(data, 0x83); ib(data, modrm(.mod=3, .reg=7, .rm=eax)); ib(data, 0); // CMP

  ib(data, 0x70 + je);
  data->back = data->tail++;
}

typedef enum { test, not = 2, neg, umul, smul, udiv, sdiv } aext;
void ia64_aext(buff* data, aext op, para src) {
  // dst is ax/eax/rax  with remainder in dx/edx/rdx
  int size = 4;
  if(size > 4) ib(data, rex(.w=1));
  ib(data, 0xf6 | !!(size > 1));
  ib(data, modrm(.mod=3, .reg=op, .rm=src.reg.name));
}

typedef enum { add, or, adc, sbb, and, sub, xor, cmp } calc;
void ia64_ical(buff* data, calc op, para dst) {
  int size = 4;
  ib(data, 0x80 | flag(.rm=1, .size=(size > 1)));
  ib(data, modrm(.mod=3, .reg=op, .rm=dst.reg.name));
}

#define arith(op, code)                                                       \
void ia64_##op(buff* data, para src, para dst) {                              \
  int size = 4;                                                               \
  if(size > 4) ib(data, rex(.w=1));                                           \
                                                                              \
  if(imm == dst.type) {                                                       \
    ia64_ical(data, op, dst);                                                 \
  }                                                                           \
  else {                                                                      \
    ib(data, code | flag(.rm=1, .size=(size > 1)));                           \
    ib(data, modrm(.mod=3, .reg=src.reg.name, .rm=dst.reg.name));             \
  }                                                                           \
}
arith(add, 0x00)
arith(or,  0x08)
arith(adc, 0x10)
arith(sbb, 0x18)
arith(and, 0x20)
arith(sub, 0x28)
arith(xor, 0x30)
arith(cmp, 0x38)
#undef  arith

void ia64_calc(buff* data, uint32_t oper) {
  para src = {reg, .reg = {edx}};
  para dst = {reg, .reg = {eax}};
  switch(oper) {
    case '+': ia64_add (data, src, dst);  break;
    case '-': ia64_sub (data, src, dst);  break;
    case '*': ia64_aext(data, smul, src); break;
    case '/': ia64_aext(data, sdiv, src); break;
    case '%': ia64_aext(data, sdiv, src); ia64_mov(data, src, dst); break;
    case '&': ia64_and (data, src, dst);  break;
    case '|': ia64_or  (data, src, dst);  break;
    case '^': ia64_xor (data, src, dst);  break;
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
  .move = (flow)ia64_move,
  .when = (flow)ia64_when,
  .jump = (flow)ia64_jump,
  .done = (flow)ia64_done,
  .back = (flow)ia64_mend,
  .args = (flow)ia64_args,
  .echo = (flow)ia64_echo2,
};

