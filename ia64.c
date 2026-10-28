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
  text->body = copy(text->body, &val, sizeof(val)); }
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
  int size = 4;
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

// prev is the end of a rel32 branch
void ia64_mend(file* text, byte* prev) {
  s32i dist = text->body - prev;
  copy(prev - 4, &dist, sizeof(dist));
}

void ia64_jump(file* text) {
  ib(text, 0xe9);
  id(text, 0); // reserve space for distance
}

void ia64_loop(file* text, byte* dest) {
  s64i dist = dest - (text->body + 2);
  if((int8_t)dist == dist) { ib(text, 0xeb); ib(text, dist); }
  else                     { ib(text, 0xe9); id(text, dist - 3); }
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
  bool imm1 = (int8_t)src.imm4 == src.imm4;
  ib(text, imm1? 0x83: 0x81); // sign-extended imm8 or imm32
  ib(text, modrm(.mod=3, .reg=op, .rm=dst.reg.enc));
  if(imm1) ib(text, src.imm4);
  else     id(text, src.imm4);
}

#define calc(op, code)                                                        \
void ia64_##op(file* text, para dst, para src) {                              \
  int size = 4;                                                               \
  if(size > 4) ib(text, rex(.w=1));                                           \
  if(imm == src.type) {                                                       \
    ia64_ical(text, op, dst, src);                                            \
  }                                                                           \
  else {                                                                      \
    ib(text, code | bits(.rm=0, .size=(size > 1)));   /* dst op= src */     \
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
    ib(text, (imm == src.type? 0xc0: 0xd2) | bits(.size=(size > 1))); // d3: by cl
    ib(text, modrm(.mod=3, .reg=op, .rm=dst.reg.enc));
    if(imm == src.type) ib(text, src.imm8); // the cpu takes it mod 32, or 64
  }
}

// eax = eax op (cnst? val: ecx), comparisons give 0 or 1; rax, rcx when WIDE.
// A constant keeps ecx unless op divides.
void ia64_calc(file* text, u32i op, bool cnst, s32i val) {
  bool w = op & WIDE;
  para src = cnst? (para){imm, .imm8 = val}: ecx;
#define W if(w) ib(text, rex(.w=1))
  switch(op & ~WIDE) {
    case '+': W; ia64_add(text, eax, src); break;
    case '-': W; ia64_sub(text, eax, src); break;
    case '&': W; ia64_and(text, eax, src); break;
    case '|': W; ia64_or (text, eax, src); break;
    case '^': W; ia64_xor(text, eax, src); break;
    case '*': W; // imul eax, eax, imm32 or imul eax, ecx
      if(cnst) { ib(text, 0x69); ib(text, modrm(.mod=3)); id(text, val); }
      else { iw(text, 0xaf0f); ib(text, modrm(.mod=3, .reg=eax.reg.enc, .rm=ecx.reg.enc)); }
      break;
    case '/': case '%':
      if(cnst) { W; ib(text, 0xc7); ib(text, modrm(.mod=3, .rm=ecx.reg.enc)); id(text, val); }
      W; ib(text, 0x99);             // cdq, cqo
      W; ia64_aext(text, sdiv, ecx); // idiv ecx
      if((op & ~WIDE) == '%') { W; ia64_mov(text, eax, edx); }
      break;
    case tokd('<', '<'): W; ia64_shft(text, shl, eax, src); break;
    case tokd('>', '>'): W; ia64_shft(text, sar, eax, src); break;
    default: { // comparison: cmp; setcc al; movzx eax, al
      op &= ~WIDE;
      cond cc = op == tokd('=', '=')? je: op == tokd('!', '=')? jne:
                op == '<'? jl: op == tokd('<', '=')? jle: op == '>'? jg: jge;
      W; ia64_cmp(text, eax, src);
      iw(text, 0x900f + (cc << 8)); ib(text, modrm(.mod=3, .rm=eax.reg.enc));
      iw(text, 0xb60f); ib(text, modrm(.mod=3, .reg=eax.reg.enc, .rm=eax.reg.enc));
    }
  }
#undef  W
}

void ia64_cast(file* text, bool wide) {
  if(wide) ib(text, rex(.w=1)), ib(text, 0x63);    // movsxd rax, eax
  else     ib(text, 0x89);                         // mov eax, eax
  ib(text, modrm(.mod=3));
}

void ia64_save(file* text) {
  ia64_push(text, rax);
}

void ia64_swap(file* text) {
  ib(text, rex(.w=1)); ia64_mov(text, ecx, eax); // mov rcx, rax
}

void ia64_pull(file* text) {
  ia64_swap(text);
  ia64_pop(text, rax);
}

void ia64_when(file* text, bool nz) {
  ib(text, rex(.w=1)); iw(text, 0xc085);      // test rax, rax
  iw(text, 0x840f + (nz << 8));               // je/jne rel32, patched by mend
  id(text, 0);
}

void ia64_move(file* text, s32i val) {
  ia64_mov(text, eax, (para){imm, .imm8=val});
}

// SysV argument registers: rdi rsi rdx rcx r8 r9
static const byte ia64_regs[] = { 7, 6, 2, 1, 8, 9 };

// mov reg, [base+disp] (a narrow load zero extends) or mov [base+disp], reg,
// of size bytes; base is rax or rbp
void ia64_disp(file* text, bool store, u32i size, byte reg, byte base, s32i disp) {
  bool d8 = (int8_t)disp == disp;
  byte mod = base == rbp.reg.enc || disp? d8? 1: 2: 0;
  if(store && size == 2) ib(text, 0x66);
  if(size == 8 || reg > 7) ib(text, rex(.w=size == 8, .r=reg > 7));
  if(!store && size < 4) iw(text, size == 1? 0xb60f: 0xb70f); // movzx
  else ib(text, (size == 1? 0x88: 0x89) | !store << 1);
  ib(text, modrm(.mod=mod, .reg=reg & 7, .rm=base));
  if(mod == 1) ib(text, disp);
  if(mod == 2) id(text, disp);
}

void ia64_load(file* text, s32i slot, u32i size) { ia64_disp(text, 0, size, 0, 5, slot); }
void ia64_stor(file* text, s32i slot, u32i size) { ia64_disp(text, 1, size, 0, 5, slot); }
void ia64_peek(file* text, s32i slot, u32i size) { ia64_disp(text, 0, size, 1, 5, slot); }
void ia64_read(file* text, u32i size)            { ia64_disp(text, 0, size, 0, 0, 0); }

void ia64_fill(file* text, u32i size) {
  ia64_disp(text, 1, size, 1, 0, 0);
  ib(text, rex(.w=1)); ia64_mov(text, eax, ecx); // mov rax, rcx
}

void ia64_addr(file* text, s32i slot) {
  bool d8 = (int8_t)slot == slot;
  ib(text, rex(.w=1)); ib(text, 0x8d);          // lea rax, [rbp+slot]
  ib(text, modrm(.mod=d8? 1: 2, .rm=rbp.reg.enc));
  if(d8) ib(text, slot);
  else   id(text, slot);
}

void ia64_glob(file* text) {
  ib(text, rex(.w=1)); ib(text, 0x8d);          // lea rax, [rip+rel32], patched by site
  ib(text, modrm(.rm=rbp.reg.enc));
  id(text, 0);
}

void ia64_site(file*, byte* glob, s64i dist) {
  s32i rel = dist - 7;                          // relative to the end of the lea
  if(rel != dist - 7) fail("data too far\n");
  copy(glob + 3, &rel, sizeof(rel));
}

void ia64_parm(file* text, u32i i, s32i slot) {
  ia64_disp(text, 1, 8, ia64_regs[i], 5, slot);
}

void ia64_args(file* text, u32i i) {
  if(ia64_regs[i] > 7) ib(text, rex(.b=1));
  ib(text, 0x58 + (ia64_regs[i] & 7));        // pop reg
}

void ia64_open(file* text) {
  ia64_push(text, rbp);
  ib(text, 0x48); iw(text, 0xe589);           // mov rbp, rsp
  ib(text, 0x48); iw(text, 0xec81); id(text, 0); // sub rsp, imm32, patched by size
}

void ia64_size(file*, byte* func, s32i size) {
  copy(func + 7, &size, sizeof(size));
}

// Linux syscall: number in rax, arguments in rdi rsi rdx r10 r8 r9
void ia64_trap(file* text, u32i argc) {
  static const byte regs[] = { 7, 6, 2, 10, 8, 9 };
  for(u32i i = argc; --i; ) {
    if(regs[i - 1] > 7) ib(text, rex(.b=1));
    ib(text, 0x58 + (regs[i - 1] & 7));         // pop reg
  }
  ia64_pop(text, rax);
  iw(text, 0x050f);                             // syscall
}

void ia64_dial(file* text) {
  ia64_call(text, (para){imm, .imm8 = 0});    // call rel32, patched by link
}

void ia64_done(file* text) {
  ib(text, 0xc9);                             // leave
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
  s32i rel = main - (boot + 5);               // call rel32 is relative to its end
  copy(boot + 1, &rel, sizeof(rel));
}

const emit ia64 = {
  .move = ia64_move,
  .load = ia64_load,
  .stor = ia64_stor,
  .peek = ia64_peek,
  .addr = ia64_addr,
  .glob = ia64_glob,
  .site = ia64_site,
  .read = ia64_read,
  .fill = ia64_fill,
  .push = ia64_save,
  .pull = ia64_pull,
  .swap = ia64_swap,
  .cast = ia64_cast,
  .calc = ia64_calc,
  .when = ia64_when,
  .jump = ia64_jump,
  .back = ia64_mend,
  .loop = ia64_loop,
  .open = ia64_open,
  .size = ia64_size,
  .parm = ia64_parm,
  .args = ia64_args,
  .dial = ia64_dial,
  .trap = ia64_trap,
  .done = ia64_done,
  .boot = ia64_boot,
  .link = ia64_link,
  .cpu  = cpu_ia64,
};

