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

#define BODY {                                                               \
  if(text->body + sizeof(val) > text->tail) fail("text section overflow\n"); \
  *(typeof(val)*)text->body = val; text->body += sizeof(val); }
void ib(file* text, byte val) BODY
void iw(file* text, u16i val) BODY
void id(file* text, u32i val) BODY
void io(file* text, u64i val) BODY
#undef  BODY

void ia64_push(file* text, para src) {
  //if(src.reg.x64) ib(text, rex(.b=1));
  ib(text, 0x50 + src.reg.enc);
}

void ia64_pop(file* text, para dst) {
  //if(src.reg.x64) ib(text, rex(.b=1));
  ib(text, 0x58 + dst.reg.enc);
}

void ia64_mov(file* text, para dst, para src) {
  int size = 1;
  if(size > 4) ib(text, rex(.w=1));

  bool swap = 0;
  if(mem == src.type) {
    swap = 1;
    para swp = dst; dst = src; src = swp;
  }
  byte mod = (mem == dst.type)?
    (0 == dst.mem.off4)? 0: (dst.mem.off1 == dst.mem.off4)? 1: 2: 3;

  if(imm == src.type) {
    if(dst.reg.x64 && src.imm8 == src.imm4) {
      ib(text, 0xc6 | bits(.size=size > 1));
      ib(text, modrm(.mod=mod, .reg=0, .rm=dst.reg.enc));
      if(src.imm4 == src.imm1) ib(text, src.imm1);
      else                     id(text, src.imm4);
    }
    else {
      // b8 does not support reg64 from imm32
      ib(text, 0xb8 + dst.reg.enc);
      if(src.imm8 == src.imm4) id(text, src.imm4);
      else                     io(text, src.imm8);
    }
  }
  else
  if(reg == src.type) {
    ib(text, 0x88 | bits(.rm=swap, .size=(size > 1)));
    ib(text, modrm(.mod=mod, .reg=src.reg.enc, .rm=dst.reg.enc));
    if(mem == dst.type) {
      // sib.idx == rsp (aka. 0b100) is used for no index register
      if(rsp.reg.enc == dst.mem.base.enc || rsp.reg.enc != dst.mem.indx.enc)
        ib(text, sib(.base=dst.mem.base.enc, .idx=dst.mem.indx.enc, .ss=dst.mem.scale));
      if(1 == mod) ib(text, dst.mem.off1);
      if(2 == mod) id(text, dst.mem.off4);
    }
  }
  else fail("mov encoding problem");
}

void ia64_call(file* text, para addr) {
  if(imm == addr.type) {
    ib(text, 0xe8);
    id(text, addr.imm8);
  }
  else {
    ib(text, 0xff);
    ib(text, modrm(.mod=3, .reg=2, .rm=addr.reg.enc));
  }
}

void ia64_ret(file* text) {
  ib(text, 0xc3);
}

// prev is the end of a rel8 branch
void ia64_mend(file* text, byte* prev) {
  s64i dist = text->body - prev;
  if(dist > 127) fail("jump too far: %ld bytes\n", dist); // TODO rel32
  prev[-1] = dist;
}

void ia64_jump(file* text) {
  ib(text, 0xeb);
  ib(text, 0); // reserve space for distance
}

typedef enum { jo, jno, jb, jae, je, jne, jbe, ja, js, jns, jp, jnp, jl, jge, jle, jg } cond;
void ia64_jmpc(file* text, cond oper, s32i offs) {
  if((int8_t)offs == offs) {
    ib(text, 0x70 + oper);
    ib(text, offs - 2);
  }
  else {
    iw(text, 0x0f80 + oper);
    id(text, offs - 6);
  }
}

typedef enum { test, not = 2, neg, umul, smul, udiv, sdiv } aext;
void ia64_aext(file* text, aext op, para src) {
  // dst is ax/eax/rax
  // for div remainder in dx/edx/rdx
  int size = 4;
  if(size > 4) ib(text, rex(.w=1));
  ib(text, 0xf6 | !!(size > 1));
  ib(text, modrm(.mod=3, .reg=op, .rm=src.reg.enc));
}

typedef enum { add, or, adc, sbb, and, sub, xor, cmp } calc;
void ia64_ical(file* text, calc op, para dst, para src) {
  int size = 4;
  ib(text, 0x80 | bits(.rm=1, .size=(size > 1)));
  ib(text, modrm(.mod=3, .reg=op, .rm=dst.reg.enc));
  ib(text, src.imm8);
}

#define calc(op, code)                                                        \
void ia64_##op(file* text, para dst, para src) {                              \
  int size = 4;                                                               \
  if(size > 4) ib(text, rex(.w=1));                                           \
  if(imm == src.type) {                                                       \
    ia64_ical(text, op, dst, src);                                            \
  }                                                                           \
  else {                                                                      \
    ib(text, code | bits(.rm=1, .size=(size > 1)));                           \
    ib(text, modrm(.mod=3, .reg=src.reg.enc, .rm=dst.reg.enc));               \
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
void ia64_shft(file* text, shft op, para dst, para src) {
  // src can only be reg CL or imm
  int size = 4;
  if(size > 4) ib(text, rex(.w=1));
  if(imm == src.type && src.imm8 == 1) {
    ib(text, 0xd0 | bits(.size=(size > 1)));
    ib(text, modrm(.mod=3, .reg=op, .rm=dst.reg.enc));
  }
  else {
    ib(text, 0xc0 | bits(.size=(size > 1)));
    ib(text, modrm(.mod=3, .reg=op, .rm=dst.reg.enc));
    if(imm == src.type) ib(text, src.imm8);
  }
  // d3 for cl?
}

void ia64_calc(file* text, u32i oper) {
  switch(oper) {
    case '+': ia64_add (text, eax, edx);  break;
    case '-': ia64_sub (text, eax, edx);  break;
    case '*': ia64_aext(text, smul, edx); break;
    case '/': ia64_aext(text, sdiv, edx); break;
    case '%': ia64_aext(text, sdiv, edx); ia64_mov(text, eax, edx); break;
    case '&': ia64_and (text, eax, edx);  break;
    case '|': ia64_or  (text, eax, edx);  break;
    case '^': ia64_xor (text, eax, edx);  break;
    case '~': ia64_aext(text, not, eax);  break;
    case '<': ia64_shft(text, shl, eax, edx); break; // TODO src is wrong
    case '>': ia64_shft(text, shr, eax, edx); break; // TODO src is wrong
  }
}

void ia64_diff(file* text, char oper, para dst, para src) {
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
void ia64_when(file* text, u32i cond) {
  ia64_cmp(text, eax, (para){imm, .imm8 = 0});
  ia64_jmpc(text, je, 0);
}

// TODO remove
void ia64_move(file* text, u32i val) {
  ia64_mov(text, eax, (para){imm, .imm8=val});
}

// TODO remove
void ia64_body(file* text) {
  ia64_push(text, rbp);
  ia64_mov(text, rbp, rsp);
  // funtion body
  ia64_pop(text, rbp);
  ia64_ret(text);
}

// program entry: exit(main())
void ia64_boot(file* text) {
  ia64_call(text, (para){imm, .imm8 = 0});    // call main, patched by link
  ib(text, 0x89); ib(text, modrm(.mod=3, .reg=eax.reg.enc, .rm=edi.reg.enc)); // mov edi, eax
  ia64_mov(text, eax, (para){imm, .imm8 = 60}); // SYS_exit
  iw(text, 0x050f);                           // syscall
}

void ia64_link(file*, byte* boot, byte* main) {
  *(s32i*)(boot + 1) = main - (boot + 5);     // call rel32 is relative to its end
}

const emit ia64 = {
  .move = (flow)ia64_move,
  .when = (flow)ia64_when,
  .jump = (flow)ia64_jump,
  .done = (flow)ia64_ret,
  .back = (flow)ia64_mend,
  .calc = (flow)ia64_calc,
  .boot = (flow)ia64_boot,
  .link = (flow)ia64_link,
  .cpu  = cpu_ia64,
};

