#include "core.h"
//prologue
//epilogue

typedef struct { byte enc:3, x64:1; } regs;
typedef struct {
  regs base;
  regs indx;
  byte scale;
  //byte sibb;
  union {
    s32i off4;
    char off1;
  };
} mems;
typedef struct {
  enum { nil, reg, mem, imm } type;
  union {
    regs reg;
    mems mem;
    s64i imm8;
    s32i imm4;
    s16i imm2;
    char imm1;
  };
} para;

#define r(name, val) const para name = {reg, .reg = {val}}
#define R(name, val) const para name = {reg, .reg = {val, .x64=1}}
r(eax, 0); r(ecx, 1); r(edx, 2); r(ebx, 3); r(esp, 4); r(ebp, 5); r(esi, 6); r(edi, 7);
R(rax, 0); R(rcx, 1); R(rdx, 2); R(rbx, 3); R(rsp, 4); R(rbp, 5); R(rsi, 6); R(rdi, 7);
R(ir8, 0); R(ir9, 1); R(ir10, 2); R(ir11, 3); R(ir12, 4); R(ir13, 5); R(ir14, 6); R(ir15, 7);
#undef  R
#undef  r

/* REX:
 *   W - 64 Bit Operand Size
 *   R - extension of the ModR/M reg field
 *   X - extension of the SIB index field
 *   B - extension of the ModR/M rm field, SIB base field or Opcode reg field
 */
#define rex(...)   (*(byte*)&(struct { byte b:1, x:1, r:1, w:1, _:4; }){._=4, __VA_ARGS__})

/* ModR/M:
 *   mod:
 *     0 - Register/Memory with no offset
 *     1 - Register/Memory with 1 byte offset
 *     2 - Register/Memory with 4 byte offset
 *     3 - Register/Register
 */
#define modrm(...) (*(byte*)&(struct { byte rm:3, reg:3, mod:2; }){__VA_ARGS__})

#define sib(...)   (*(byte*)&(struct { byte base:3, idx:3, ss:2; }){__VA_ARGS__})

// some instructions have the following bit pattern xxxxxxDS where:
//   S - size of the operand 8/32
//   D - direction RM -> 1 / MR -> 0
#define bits(...)  (*(byte*)&(struct { byte size:1, rm:1, _:6; }){__VA_ARGS__})

//byte mspe[] = {           rcx, rdx, ir8, ir9 };
//byte elfs[] = { rdi, rsi, rdx, rcx, ir8, ir9 };

// TODO remove
void debug(buff* data) {
  for(byte* b = data->head; b < data->tail; b++)
    printf("%.2x", *b);
  printf("\n");
}

#define BODY { *(typeof(val)*)data->tail = val; data->tail += sizeof(val); }
void ib(buff* data, byte val) BODY
void iw(buff* data, u16i val) BODY
void id(buff* data, u32i val) BODY
void io(buff* data, u64i val) BODY
#undef  BODY

void ia64_push(buff* data, para src) {
  //if(src.reg.x64) ib(data, rex(.b=1));
  ib(data, 0x50 + src.reg.enc);
}

void ia64_pop(buff* data, para dst) {
  //if(src.reg.x64) ib(data, rex(.b=1));
  ib(data, 0x58 + dst.reg.enc);
}

void ia64_mov(buff* data, para dst, para src) {
  int size = 1;
  if(size > 4) ib(data, rex(.w=1));

  bool swap = 0;
  if(mem == src.type) {
    swap = 1;
    para swp = dst; dst = src; src = swp;
  }
  byte mod = (mem == dst.type)?
    (0 == dst.mem.off4)? 0: (dst.mem.off1 == dst.mem.off4)? 1: 2: 3;

  if(imm == src.type) {
    if(dst.reg.x64 && src.imm8 == src.imm4) {
      ib(data, 0xc6 | bits(.size=size > 1));
      ib(data, modrm(.mod=mod, .reg=0, .rm=dst.reg.enc));
      if(src.imm4 == src.imm1) ib(data, src.imm1);
      else                     id(data, src.imm4);
    }
    else {
      // b8 does not support reg64 from imm32
      ib(data, 0xb8 + dst.reg.enc);
      if(src.imm8 == src.imm4) id(data, src.imm4);
      else                     io(data, src.imm8);
    }
  }
  else
  if(reg == src.type) {
    ib(data, 0x88 | bits(.rm=swap, .size=(size > 1)));
    ib(data, modrm(.mod=mod, .reg=src.reg.enc, .rm=dst.reg.enc));
    if(mem == dst.type) {
      // sib.idx == rsp (aka. 0b100) is used for no index register
      if(rsp.reg.enc == dst.mem.base.enc || rsp.reg.enc != dst.mem.indx.enc)
        ib(data, sib(.base=dst.mem.base.enc, .idx=dst.mem.indx.enc, .ss=dst.mem.scale));
      if(1 == mod) ib(data, dst.mem.off1);
      if(2 == mod) id(data, dst.mem.off4);
    }
  }
  else fail("mov encoding problem");
}

void ia64_call(buff* data, para addr) {
  if(imm == addr.type) {
    ib(data, 0xe8);
    id(data, addr.imm8);
  }
  else {
    ib(data, 0xff);
    ib(data, modrm(.mod=3, .reg=2, .rm=addr.reg.enc));
  }
}

void ia64_ret(buff* data) {
  ib(data, 0xc3);
}

void ia64_jump(buff* data, u32i fix) {
  ib(data, 0xeb);
  if(fix) *data->back = data->tail - data->back;
  data->back = data->tail++;
}

typedef enum { jo, jno, jb, jae, je, jne, jbe, ja, js, jns, jp, jnp, jl, jge, jle, jg } cond;
void ia64_jmpc(buff* data, cond oper, s32i offs) {
  if((int8_t)offs == offs) {
    ib(data, 0x70 + oper);
    ib(data, offs - 2);
  }
  else {
    iw(data, 0x0f80 + oper);
    id(data, offs - 6);
  }
}

typedef enum { test, not = 2, neg, umul, smul, udiv, sdiv } aext;
void ia64_aext(buff* data, aext op, para src) {
  // dst is ax/eax/rax
  // for div remainder in dx/edx/rdx
  int size = 4;
  if(size > 4) ib(data, rex(.w=1));
  ib(data, 0xf6 | !!(size > 1));
  ib(data, modrm(.mod=3, .reg=op, .rm=src.reg.enc));
}

typedef enum { add, or, adc, sbb, and, sub, xor, cmp } calc;
void ia64_ical(buff* data, calc op, para dst, para src) {
  int size = 4;
  ib(data, 0x80 | bits(.rm=1, .size=(size > 1)));
  ib(data, modrm(.mod=3, .reg=op, .rm=dst.reg.enc));
  ib(data, src.imm8);
}

#define calc(op, code)                                                        \
void ia64_##op(buff* data, para dst, para src) {                              \
  int size = 4;                                                               \
  if(size > 4) ib(data, rex(.w=1));                                           \
  if(imm == src.type) {                                                       \
    ia64_ical(data, op, dst, src);                                            \
  }                                                                           \
  else {                                                                      \
    ib(data, code | bits(.rm=1, .size=(size > 1)));                           \
    ib(data, modrm(.mod=3, .reg=src.reg.enc, .rm=dst.reg.enc));               \
  }                                                                           \
}
calc(add, 0x00)
calc(or,  0x08)
calc(adc, 0x10)
calc(sbb, 0x18)
calc(and, 0x20)
calc(sub, 0x28)
calc(xor, 0x30)
calc(cmp, 0x38)
#undef  calc

typedef enum { shl = 4, shr = 5, sar = 7 } shft;
void ia64_shft(buff* data, shft op, para dst, para src) {
  // src can only be reg CL or imm
  int size = 4;
  if(size > 4) ib(data, rex(.w=1));
  if(imm == src.type && src.imm8 == 1) {
    ib(data, 0xd0 | bits(.size=(size > 1)));
    ib(data, modrm(.mod=3, .reg=op, .rm=dst.reg.enc));
  }
  else {
    ib(data, 0xc0 | bits(.size=(size > 1)));
    ib(data, modrm(.mod=3, .reg=op, .rm=dst.reg.enc));
    if(imm == src.type) ib(data, src.imm8);
  }
  // d3 for cl?
}

void ia64_calc(buff* data, u32i oper) {
  switch(oper) {
    case '+': ia64_add (data, eax, edx);  break;
    case '-': ia64_sub (data, eax, edx);  break;
    case '*': ia64_aext(data, smul, edx); break;
    case '/': ia64_aext(data, sdiv, edx); break;
    case '%': ia64_aext(data, sdiv, edx); ia64_mov(data, eax, edx); break;
    case '&': ia64_and (data, eax, edx);  break;
    case '|': ia64_or  (data, eax, edx);  break;
    case '^': ia64_xor (data, eax, edx);  break;
    case '~': ia64_aext(data, not, edx);  break;     // TODO singe operand
    case '<': ia64_shft(data, shl, eax, edx); break; // TODO src is wrong
    case '>': ia64_shft(data, shr, eax, edx); break; // TODO src is wrong
  }
}

void ia64_diff(buff* data, char oper, para dst, para src) {
  switch(oper) {
    case '=': break;
    case '!': break;
    case '<': break;
    case '>': break;
    case 'g': break;
    case 'l': break;
  }
}

// TODO remove
void ia64_when(buff* data, u32i cond) {
  ia64_cmp(data, eax, (para){imm, .imm8 = 0});

  // XXX ia64_jmpc is the same
  ib(data, 0x70 + je);
  data->back = data->tail++;
}

// TODO remove
void ia64_move(buff* data, u32i val) {
  ia64_mov(data, eax, (para){imm, .imm8=val});
}

void ia64_mark(buff* data) {  }
void ia64_mend(buff* data) { *data->back = data->tail - data->back - 1; }

// TODO remove
void ia64_body(buff* data) {
  ia64_push(data, rbp);
  ia64_mov(data, rbp, rsp);
  // funtion body
  ia64_pop(data, rbp);
  ia64_ret(data);
}

// TODO remove
void ia64_echo2(buff* data, u32i func, u32i addr) {
  ib(data, 0xb8 + eax.reg.enc); id(data, func); // mov
  ib(data, 0xb8 + edi.reg.enc); id(data, addr); // mov
  ib(data, 0xff);
  ib(data, modrm(.mod=3, .reg=2, .rm=eax.reg.enc));
}

const emit ia64 = {
  .move = (flow)ia64_move,
  .when = (flow)ia64_when,
  .jump = (flow)ia64_jump,
  .done = (flow)ia64_ret,
  .back = (flow)ia64_mend,
  .echo = (flow)ia64_echo2,
};

