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

#define r(name, val) const para name = {reg, .reg = {val, .x64=0}}
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
typedef struct { byte b:1, x:1, r:1, w:1, _:4; } rexs;
#define rex(...)   (*(byte*)&(rexs){._=4, __VA_ARGS__})

/* ModR/M:
 *   mod:
 *     0 - Register/Memory with no offset
 *     1 - Register/Memory with 1 byte offset
 *     2 - Register/Memory with 4 byte offset
 *     3 - Register/Register
 */
typedef struct { byte rm:3, reg:3, mod:2; } modrms;
#define modrm(...) (*(byte*)&(modrms){__VA_ARGS__})

typedef struct { byte base:3, idx:3, ss:2; } sibs;
#define sib(...)   (*(byte*)&(sibs){__VA_ARGS__})

// some instructions have the following bit pattern xxxxxxDS where:
//   S - size of the operand 8/32
//   D - direction RM -> 1 / MR -> 0
typedef struct { byte size:1, rm:1, _:6; } bitss;
#define bits(...)  (*(byte*)&(bitss){__VA_ARGS__})

//byte mspe[] = {           rcx, rdx, ir8, ir9 };
//byte elfs[] = { rdi, rsi, rdx, rcx, ir8, ir9 };

// the low bytes of v: an immediate or displacement is emitted truncated
#define BODY(type) { type val = (type)v;                                     \
  if(text->body + sizeof(val) > text->tail) fail("text section overflow\n"); \
  text->body = copy(text->body, &val, sizeof(val)); }
void ib(file* text, u64i v) BODY(byte)
void iw(file* text, u64i v) BODY(u16i)
void id(file* text, u64i v) BODY(u32i)
void io(file* text, u64i v) BODY(u64i)
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
void ia64_mend(const file* text, byte* prev) {
  s32i dist = (s32i)(text->body - prev);
  copy(prev - 4, &dist, sizeof(dist));
}

void ia64_jump(file* text) {
  ib(text, 0xe9);
  id(text, 0); // reserve space for distance
}

void ia64_loop(file* text, const byte* dest) {
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

// the condition code of comparison op
cond ia64_cc(u32i op) {
  op &= ~(WIDE | TEST);
  return op == tokd('=', '=')? je: op == tokd('!', '=')? jne:
         op == '<'? jl: op == tokd('<', '=')? jle: op == '>'? jg: jge;
}

void ia64_flag(file* text, u32i op) {
  iw(text, 0x900f + (ia64_cc(op) << 8)); ib(text, modrm(.mod=3, .rm=eax.reg.enc)); // setcc al
  iw(text, 0xb60f); ib(text, modrm(.mod=3, .reg=eax.reg.enc, .rm=eax.reg.enc));     // movzx eax, al
}

void ia64_cond(file* text, u32i op, bool hold) {
  iw(text, 0x800f + ((ia64_cc(op) ^ !hold) << 8)); // jcc rel32, patched by mend
  id(text, 0);
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
    default: // comparison: cmp, then setcc al; movzx eax, al unless TEST
      W; ia64_cmp(text, eax, src);
      if(!(op & TEST)) ia64_flag(text, op);
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

// scratch k is r8 + k: SysV argument and C call registers, free between calls
void ia64_keep(file* text) {
  ia64_push(text, rax);
  iw(text, 0x9066);                             // xchg ax, ax: room for lift
}

void ia64_lift(const file* text, byte* keep, u32i k) {
  (void)text;
  keep[0] = 0x49; keep[1] = 0x89; keep[2] = modrm(.mod=3, .reg=eax.reg.enc, .rm=k); // mov r8+k, rax
}

void ia64_take(file* text, u32i k) {
  ia64_swap(text);
  ib(text, 0x4c); ib(text, 0x89); ib(text, modrm(.mod=3, .reg=k, .rm=eax.reg.enc)); // mov rax, r8+k
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

void ia64_site(const file* text, byte* glob, s64i dist) {
  (void)text;
  s32i rel = (s32i)(dist - 7);                  // relative to the end of the lea
  if(rel != dist - 7) fail("data too far\n");
  copy(glob + 3, &rel, sizeof(rel));
}

void ia64_parm(file* text, u32i i, s32i slot) {
  ia64_disp(text, 1, 8, ia64_regs[i], 5, slot);
}

// pop reg, r8-r15 too
void ia64_drop(file* text, byte reg) {
  if(reg > 7) ib(text, rex(.b=1));
  ib(text, 0x58 + (reg & 7));
}

void ia64_args(file* text, u32i i) {
  ia64_drop(text, ia64_regs[i]);
}

void ia64_pass(file* text, u32i i) {
  byte reg = ia64_regs[i];
  ib(text, rex(.w=1, .b=reg > 7)); ib(text, 0x89); ib(text, modrm(.mod=3, .reg=eax.reg.enc, .rm=reg & 7));
}

// the registers of locals: kept by a function called, in SysV and Win64
static const byte ia64_kept[] = { 3, 12, 13, 14, 15 }; // rbx r12 r13 r14 r15

// the machine register of an operand of rop
static byte ia64_reg(s32i x) {
  return x >= 0? ia64_kept[x]: x == ACC? 0: x == TMP? 1: ia64_regs[ARG - x];
}

// to op= by: mov, the group add or and sub xor cmp by its /digit, or their
// forms with a constant. A constant not below 0 is moved on 32 bits, which
// clears the rest
void ia64_rop(file* text, u32i op, s32i to, s32i by, s32i n) {
  u32i o = op & ~(WIDE | TEST);
  byte d = ia64_reg(to), s = by == NUM? 0: ia64_reg(by);
  byte k = o == '='? 8: o == '+'? add: o == '|'? or: o == '&'? and: o == '-'? sub: o == '^'? xor: cmp;
  bool w = op & WIDE && !(by == NUM && k == 8 && n >= 0), i8 = (int8_t)n == n;
  if(w || d > 7 || s > 7) ib(text, rex(.w=w, .r=s > 7, .b=d > 7));
  if(by != NUM) {
    ib(text, k == 8? 0x89: k << 3 | 1);           // op r/m, reg
    ib(text, modrm(.mod=3, .reg=s & 7, .rm=d & 7));
  }
  else if(k == 8 && !w) { ib(text, 0xb8 + (d & 7)); id(text, n); } // mov reg, imm32
  else {
    ib(text, k == 8? 0xc7: i8? 0x83: 0x81);       // mov r/m, imm32 or op r/m, imm8 or imm32
    ib(text, modrm(.mod=3, .reg=k & 7, .rm=d & 7));
    if(k != 8 && i8) ib(text, n);
    else             id(text, n);
  }
}

void ia64_stow(file* text, u32i r, s32i slot, bool back) {
  ia64_disp(text, !back, 8, ia64_kept[r], 5, slot);
}

void ia64_open(file* text) {
  ia64_push(text, rbp);
  ib(text, 0x48); iw(text, 0xe589);           // mov rbp, rsp
  ib(text, 0x48); iw(text, 0xec81); id(text, 0); // sub rsp, imm32, patched by size
}

void ia64_size(file* text, byte* func, s32i size) {
  (void)text;
  copy(func + 7, &size, sizeof(size));
}

// Linux syscall: number in rax, arguments in rdi rsi rdx r10 r8 r9
void ia64_trap(file* text, u32i argc) {
  static const byte regs[] = { 7, 6, 2, 10, 8, 9 };
  for(u32i i = argc; --i; ) ia64_drop(text, regs[i - 1]);
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

void ia64_link(const file* text, byte* boot, const byte* main) {
  (void)text;
  s32i rel = (s32i)(main - (boot + 5));       // call rel32 is relative to its end
  copy(boot + 1, &rel, sizeof(rel));
}

// called by boot: [rsp+8] is argc, argv follows it. start NULL: through the
// import slot whose address is in acc
void ia64_crt(file* text, const byte* main, const byte* start, clib c) {
  if(c == cl_bionic) { // __libc_init(rsp+8, 0, main, structors), the structors 48 0 bytes on the stack
    id(text, 0x247c8d48); ib(text, 0x08);         // lea rdi, [rsp+8]
    iw(text, 0xf631);                             // xor esi, esi
    ib(text, 0x48); iw(text, 0x158d);             // lea rdx, [rip+main]
    id(text, main - (text->body + 4));
    id(text, 0xf0e48348);                         // and rsp, -16
    iw(text, 0xc931);                             // xor ecx, ecx
    for(u32i k = 0; k < 6; k++) ib(text, 0x51);   // push rcx
    ib(text, 0x48); iw(text, 0xe189);             // mov rcx, rsp
  }
  else {
    id(text, 0x0824748b);                         // mov esi, [rsp+8]
    id(text, 0x24548d48); ib(text, 0x10);         // lea rdx, [rsp+16]
    ib(text, 0x48); iw(text, 0x3d8d);             // lea rdi, [rip+main]
    id(text, main - (text->body + 4));
    iw(text, 0xc931);                             // xor ecx, ecx
    ib(text, 0x45); iw(text, 0xc031);             // xor r8d, r8d
    ib(text, 0x45); iw(text, 0xc931);             // xor r9d, r9d
    id(text, 0xf0e48348);                         // and rsp, -16
  }
  if(start) { byte* at = text->body; ia64_dial(text); ia64_link(text, at, start); }
  else iw(text, 0x10ff);                          // call [rax]
  ib(text, 0xf4);                                 // hlt: start never returns
}

// SysV (Linux, macOS): arguments in rdi rsi rdx rcx r8 r9, al = 0 vector
// registers for a variadic callee. Win64: in rcx rdx r8 r9 and on the stack
// past 32 bytes of shadow space. Both want rsp 16-byte aligned, which pushed
// operands may break: the old rsp is pushed twice, then rsp rounded down, so
// one copy is at [rsp+8] either way
byte* ia64_ffi(file* text, const byte* func, u32i argc, u32i fixed, osys os) {
  (void)fixed;
  static const byte win[] = { 1, 2, 8, 9, 10, 0 }; // r10 and rax go on the stack
  byte room = os == os_win? argc > 4? 48: 32: 0;
  if(!func) { ib(text, 0x4c); iw(text, 0x188b); } // mov r11, [rax]
  for(u32i i = argc; i--; ) ia64_drop(text, os == os_win? win[i]: ia64_regs[i]);
  ib(text, 0x54);                                 // push rsp
  ib(text, 0xff); iw(text, 0x2434);               // push [rsp]
  id(text, 0xf0e48348);                           // and rsp, -16
  if(room) { id(text, 0xec8348 | room << 24); }   // sub rsp, room
  else iw(text, 0xc031);                          // xor eax, eax
  if(argc > 4 && room) { id(text, 0x2454894c); ib(text, 32); } // mov [rsp+32], r10
  if(argc > 5 && room) { id(text, 0x24448948); ib(text, 40); } // mov [rsp+40], rax
  byte* at = func? text->body: NULL;
  if(func) { ia64_dial(text); ia64_link(text, at, func); }
  else { ib(text, 0x41); iw(text, 0xd3ff); }      // call r11
  id(text, 0x24648b48); ib(text, room + 8);       // mov rsp, [rsp+room+8]
  return at;
}

// Win64 callers pass the arguments in rcx rdx r8 r9 and past 32 bytes of
// shadow space, and keep rdi and rsi: the SysV registers otic takes them in
void ia64_hail(file* text, osys os) {
  if(os != os_win) return;
  iw(text, 0x5657);                               // push rdi; push rsi
  ib(text, 0x48); iw(text, 0xcf89);               // mov rdi, rcx
  ib(text, 0x48); iw(text, 0xd689);               // mov rsi, rdx
  ib(text, 0x4c); iw(text, 0xc289);               // mov rdx, r8
  ib(text, 0x4c); iw(text, 0xc989);               // mov rcx, r9
  id(text, 0x24448b4c); ib(text, 56);             // mov r8, [rsp+56]
  id(text, 0x244c8b4c); ib(text, 64);             // mov r9, [rsp+64]
  ib(text, 0xe8); id(text, 3);                    // call the function, past what follows
  iw(text, 0x5f5e); ib(text, 0xc3);               // pop rsi; pop rdi; ret
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
  .ffi  = ia64_ffi,
  .crt  = ia64_crt,
  .flag = ia64_flag,
  .cond = ia64_cond,
  .keep = ia64_keep,
  .lift = ia64_lift,
  .take = ia64_take,
  .pass = ia64_pass,
  .hail = ia64_hail,
  .rop  = ia64_rop,
  .stow = ia64_stow,
  .cpu  = cpu_ia64,
  .regs = sizeof ia64_kept,
};

