#include "core.h"

enum {
  r0,  r1,  r2,  r3,  r4,  r5,  r6,  r7,
  r8,  r9,  r10, r11, r12, r13, r14, r15,
  r16, r17, r18, r19, r20, r21, r22, r23,
  r24, r25, r26, r27, r28, r29, r30,
};

typedef struct { uint32_t rd: 5, rn: 5, imm: 6, rm: 5, _0: 1, shift: 2, _c: 5, S: 1, op: 1, sf: 1; } datr;
typedef struct { uint32_t rd: 5, rn: 5, imm: 6, rm: 5, _0: 1, shift: 2, _c: 5, opc: 2, sf: 1; } datr1;
typedef union {
  struct { uint32_t rd  : 5, imm :16, hw  : 2, _   : 6, opc : 2, sf  : 1; } dati;
  struct { uint32_t op4 : 5, rn  : 5, op3 : 6, op2 : 5, opc : 4, _   : 7; } brur;
  struct { uint32_t cond: 4, o0  : 1, imm :19, o1  : 1,          _   : 7; } brci;
  //struct { uint32_t op4 : 5, rn  : 5, op3 : 6, op2 : 5, opc : 4, op  : 7; } ldst;
  //struct { uint32_t op4 : 5, rn  : 5, op3 : 6, op2 : 5, opc : 4, op  : 7; } rdat;
  //struct { uint32_t op4 : 5, rn  : 5, op3 : 6, op2 : 5, opc : 4, op  : 7; } sdat;

  uint32_t n;
} arm8_form;

#define X (1u << 31) // sf: the 64-bit form of an instruction
#define form(FMT, OPC, ...) (FMT ## _ ## OPC | (arm8_form){.FMT={__VA_ARGS__}}.n)

typedef enum {
    COND_EQ = 0x0,
    COND_NE = 0x1,
    COND_CS = 0x2,     /* Unsigned greater or equal */
    COND_HS = COND_CS, /* ALIAS greater or equal */
    COND_CC = 0x3,     /* Unsigned less than */
    COND_LO = COND_CC, /* ALIAS Lower */
    COND_MI = 0x4,     /* Negative */
    COND_PL = 0x5,     /* Zero or greater */
    COND_VS = 0x6,     /* Overflow */
    COND_VC = 0x7,     /* No overflow */
    COND_HI = 0x8,     /* Unsigned greater than */
    COND_LS = 0x9,     /* Unsigned less or equal */
    COND_GE = 0xa,
    COND_LT = 0xb,
    COND_GT = 0xc,
    COND_LE = 0xd,
    COND_AL = 0xe,
    COND_NV = 0xf, /* behaves like COND_AL here */
} arm8_cond;

typedef enum {
  // Compare and branch (immediate)
  I3201_CBZ       = 0x34000000,
  I3201_CBNZ      = 0x35000000,

  // Conditional branch (immediate)
  I3202_B_C       = 0x54000000,

  // Unconditional branch (immediate)
  I3206_B         = 0x14000000,
  I3206_BL        = 0x94000000,

  // Unconditional branch (register)
  brur_br        = 0xd61f0000,
  brur_blr       = 0xd63f0000,
  brur_ret       = 0xd65f0000,

  // AdvSIMD load/store single structure
  I3303_LD1R      = 0x0d40c000,

  // Load literal for loading the address at pc-relative offset
  I3305_LDR       = 0x58000000,
  I3305_LDR_v64   = 0x5c000000,
  I3305_LDR_v128  = 0x9c000000,

  // Load/store register
  I3312_STRB      = 0x38000000, // 8 << 30,
  I3312_STRH      = 0x38000000, // 16 << 30,
  I3312_STRW      = 0x38000000, // 32 << 30,
  I3312_STRX      = 0x38000000, // 64 << 30,

  I3312_LDRB      = 0x38400000, // 8 << 30,
  I3312_LDRH      = 0x38400000, // 16 << 30,
  I3312_LDRW      = 0x38400000, // 32 << 30,
  I3312_LDRX      = 0x38400000, // 64 << 30,

  I3312_LDRSBW    = 0x38c00000, // 8 << 30,
  I3312_LDRSHW    = 0x38c00000, // 16 << 30,

  I3312_LDRSBX    = 0x38800000, // 8 << 30,
  I3312_LDRSHX    = 0x38800000, // 16 << 30,
  I3312_LDRSWX    = 0x38800000, // 32 << 30,

  I3312_LDRVS     = 0x3c400000, // 32 << 30,
  I3312_STRVS     = 0x3c000000, // 32 << 30,

  I3312_LDRVD     = 0x3c400000, // 64 << 30,
  I3312_STRVD     = 0x3c000000, // 64 << 30,

  I3312_LDRVQ     = 0x3cc00000,
  I3312_STRVQ     = 0x3c800000,

  I3312_TO_I3310  = 0x00200800,
  I3312_TO_I3313  = 0x01000000,

  // Load/store register pair instructions
  I3314_LDP       = 0x28400000,
  I3314_STP       = 0x28000000,

  // Add/subtract immediate instructions
  I3401_ADDI      = 0x11000000,
  I3401_ADDSI     = 0x31000000,
  I3401_SUBI      = 0x51000000,
  I3401_SUBSI     = 0x71000000,

  // Bitfield instructions
  I3402_BFM       = 0x33000000,
  I3402_SBFM      = 0x13000000,
  I3402_UBFM      = 0x53000000,

  // Extract instruction
  I3403_EXTR      = 0x13800000,

  // Logical immediate instructions
  I3404_ANDI      = 0x12000000,
  I3404_ORRI      = 0x32000000,
  I3404_EORI      = 0x52000000,

  // Move wide immediate instructions
  dati_movn      = 0x12800000,
  dati_movz      = 0x52800000,
  dati_movk      = 0x72800000,

  // PC relative addressing instructions
  I3406_ADR       = 0x10000000,
  I3406_ADRP      = 0x90000000,

  // Add/subtract shifted register instructions (without a shift)
  I3502_ADD       = 0x0b000000,
  I3502_ADDS      = 0x2b000000,
  I3502_SUB       = 0x4b000000,
  I3502_SUBS      = 0x6b000000,

  // Add/subtract shifted register instructions (with a shift)
  I3502S_ADD_LSL  = 0x0b000000,

  // Add/subtract with carry instructions
  I3503_ADC       = 0x1a000000,
  I3503_SBC       = 0x5a000000,

  // Conditional select instructions
  I3506_CSEL      = 0x1a800000,
  I3506_CSINC     = 0x1a800400,
  I3506_CSINV     = 0x5a800000,
  I3506_CSNEG     = 0x5a800400,

  // Data-processing (1 source) instructions
  I3507_CLZ       = 0x5ac01000,
  I3507_RBIT      = 0x5ac00000,
  I3507_REV16     = 0x5ac00400,
  I3507_REV32     = 0x5ac00800,
  I3507_REV64     = 0x5ac00c00,

  // Data-processing (2 source) instructions
  I3508_LSLV      = 0x1ac02000,
  I3508_LSRV      = 0x1ac02400,
  I3508_ASRV      = 0x1ac02800,
  I3508_RORV      = 0x1ac02c00,
  I3508_SMULH     = 0x9b407c00,
  I3508_UMULH     = 0x9bc07c00,
  I3508_UDIV      = 0x1ac00800,
  I3508_SDIV      = 0x1ac00c00,

  // Data-processing (3 source) instructions
  I3509_MADD      = 0x1b000000,
  I3509_MSUB      = 0x1b008000,

  // Logical shifted register instructions (without a shift)
  I3510_AND       = 0x0a000000,
  I3510_BIC       = 0x0a200000,
  I3510_ORR       = 0x2a000000,
  I3510_ORN       = 0x2a200000,
  I3510_EOR       = 0x4a000000,
  I3510_EON       = 0x4a200000,
  I3510_ANDS      = 0x6a000000,

  // Logical shifted register instructions (with a shift)
  I3502S_AND_LSR  = 0x0a400000,

  // AdvSIMD copy
  I3605_DUP       = 0x0e000400,
  I3605_INS       = 0x4e001c00,
  I3605_UMOV      = 0x0e003c00,

  // AdvSIMD modified immediate
  I3606_MOVI      = 0x0f000400,
  I3606_MVNI      = 0x2f000400,
  I3606_BIC       = 0x2f001400,
  I3606_ORR       = 0x0f001400,

  // AdvSIMD shift by immediate
  I3614_SSHR      = 0x0f000400,
  I3614_SSRA      = 0x0f001400,
  I3614_SHL       = 0x0f005400,
  I3614_SLI       = 0x2f005400,
  I3614_USHR      = 0x2f000400,
  I3614_USRA      = 0x2f001400,

  // AdvSIMD three same
  I3616_ADD       = 0x0e208400,
  I3616_AND       = 0x0e201c00,
  I3616_BIC       = 0x0e601c00,
  I3616_BIF       = 0x2ee01c00,
  I3616_BIT       = 0x2ea01c00,
  I3616_BSL       = 0x2e601c00,
  I3616_EOR       = 0x2e201c00,
  I3616_MUL       = 0x0e209c00,
  I3616_ORR       = 0x0ea01c00,
  I3616_ORN       = 0x0ee01c00,
  I3616_SUB       = 0x2e208400,
  I3616_CMGT      = 0x0e203400,
  I3616_CMGE      = 0x0e203c00,
  I3616_CMTST     = 0x0e208c00,
  I3616_CMHI      = 0x2e203400,
  I3616_CMHS      = 0x2e203c00,
  I3616_CMEQ      = 0x2e208c00,
  I3616_SMAX      = 0x0e206400,
  I3616_SMIN      = 0x0e206c00,
  I3616_SSHL      = 0x0e204400,
  I3616_SQADD     = 0x0e200c00,
  I3616_SQSUB     = 0x0e202c00,
  I3616_UMAX      = 0x2e206400,
  I3616_UMIN      = 0x2e206c00,
  I3616_UQADD     = 0x2e200c00,
  I3616_UQSUB     = 0x2e202c00,
  I3616_USHL      = 0x2e204400,

  // AdvSIMD two-reg misc
  I3617_CMGT0     = 0x0e208800,
  I3617_CMEQ0     = 0x0e209800,
  I3617_CMLT0     = 0x0e20a800,
  I3617_CMGE0     = 0x2e208800,
  I3617_CMLE0     = 0x2e20a800,
  I3617_NOT       = 0x2e205800,
  I3617_ABS       = 0x0e20b800,
  I3617_NEG       = 0x2e20b800,

  // System instructions
  NOP             = 0xd503201f,
  DMB_ISH         = 0xd50338bf,
  DMB_LD          = 0x00000100,
  DMB_ST          = 0x00000200,
} arm8_inst;

void arm8_put(file* data, uint32_t inst) {
  if(data->body + 4 > data->tail) fail("text section overflow\n");
  *(uint32_t*)data->body = inst; data->body += 4;
}

void arm8_imm(file* data, int rd, uint32_t num) {
  arm8_put(data, form(dati, movz, .imm=num & 0xffff, .rd=rd));      // movz wd, #lo
  if(num >> 16)
    arm8_put(data, 0x72a00000 | (num >> 16) << 5 | rd);             // movk wd, #hi, lsl 16
}

void arm8_move(file* data, s32i num) {
  arm8_imm(data, r0, num);
}

void arm8_save(file* data) {
  arm8_put(data, 0xf81f0fe0);                                       // str x0, [sp, #-16]!
}

void arm8_swap(file* data) {
  arm8_put(data, X | I3510_ORR | r0 << 16 | 31 << 5 | r1);          // mov x1, x0
}

void arm8_pull(file* data) {
  arm8_swap(data);
  arm8_put(data, 0xf84107e0);                                       // ldr x0, [sp], #16
}

void arm8_cast(file* data, bool wide) {
  arm8_put(data, wide? 0x93407c00: I3510_ORR | r0 << 16 | 31 << 5); // sxtw x0, w0 or mov w0, w0
}

// ldur/stur rt, [rn, #off] of size bytes, a narrow load zero extends
void arm8_disp(file* data, uint32_t inst, u32i size, int rt, int rn, s32i off) {
  arm8_put(data, (size == 8? 3u: size >> 1) << 30 | inst | (off & 0x1ff) << 12 | rn << 5 | rt);
}

// the slot reaches 256 bytes below the frame pointer, further through x16
void arm8_slot(file* data, uint32_t inst, u32i size, int rt, s32i slot) {
  if(slot < -0xfff) fail("frame too large: %d bytes\n", -slot);
  if(slot >= -256) { arm8_disp(data, inst, size, rt, r29, slot); return; }
  arm8_put(data, 0xd1000000 | -slot << 10 | r29 << 5 | r16);        // sub x16, x29, #-slot
  arm8_disp(data, inst, size, rt, r16, 0);
}

void arm8_load(file* data, s32i slot, u32i size) { arm8_slot(data, I3312_LDRW, size, r0, slot); }
void arm8_stor(file* data, s32i slot, u32i size) { arm8_slot(data, I3312_STRW, size, r0, slot); }
void arm8_peek(file* data, s32i slot, u32i size) { arm8_slot(data, I3312_LDRW, size, r1, slot); }
void arm8_read(file* data, u32i size)            { arm8_disp(data, I3312_LDRW, size, r0, r0, 0); }

void arm8_fill(file* data, u32i size) {
  arm8_disp(data, I3312_STRW, size, r1, r0, 0);
  arm8_put(data, X | I3510_ORR | r1 << 16 | 31 << 5 | r0);          // mov x0, x1
}

void arm8_addr(file* data, s32i slot) {
  if(slot < -0xfff) fail("frame too large: %d bytes\n", -slot);
  arm8_put(data, 0xd1000000 | -slot << 10 | r29 << 5 | r0);         // sub x0, x29, #-slot
}

// x0 = pc + a 32-bit distance, patched by site
void arm8_glob(file* data) {
  arm8_put(data, I3406_ADR | r0);                                   // adr x0, .
  arm8_put(data, X | dati_movz | r16);                              // movz x16, #lo
  arm8_put(data, X | dati_movk | 1 << 21 | r16);                    // movk x16, #hi, lsl 16
  arm8_put(data, X | I3502_ADD | r16 << 16);                        // add x0, x0, x16
}

void arm8_site(file*, byte* glob, s64i dist) {
  if(dist >> 32) fail("data too far\n");
  ((uint32_t*)glob)[1] |= (dist & 0xffff) << 5;
  ((uint32_t*)glob)[2] |= (dist >> 16 & 0xffff) << 5;
}

void arm8_parm(file* data, u32i i, s32i slot) {
  arm8_slot(data, I3312_STRW, 8, i, slot);                          // arguments in x0-x5
}

void arm8_args(file* data, u32i i) {
  arm8_put(data, 0xf84107e0 | i);                                   // ldr xi, [sp], #16
}

void arm8_open(file* data) {
  arm8_put(data, 0xa9bf7bfd);                                       // stp x29, x30, [sp, #-16]!
  arm8_put(data, 0x910003fd);                                       // mov x29, sp
  arm8_put(data, 0xd10003ff);                                       // sub sp, sp, #0, patched by size
}

void arm8_size(file*, byte* func, s32i size) {
  if(size > 0xfff) fail("frame too large: %d bytes\n", size);
  ((uint32_t*)func)[2] |= size << 10;
}

// Linux syscall: number in x8, arguments in x0-x5
void arm8_trap(file* data, u32i argc) {
  for(u32i i = argc; --i; ) arm8_put(data, 0xf84107e0 | (i - 1));  // ldr xi, [sp], #16
  arm8_put(data, 0xf84107e0 | r8);                                  // ldr x8, [sp], #16
  arm8_put(data, 0xd4000001);                                       // svc #0
}

void arm8_dial(file* data) {
  arm8_put(data, I3206_BL);                                         // bl, patched by link
}

// w0 = w0 op (cnst? num: w1), comparisons give 0 or 1; x0, x1 when WIDE.
// A constant goes through x16 and keeps x1.
void arm8_calc(file* data, u32i op, bool cnst, s32i num) {
  int rm = cnst? r16: r1;
  uint32_t w1 = (op & WIDE? X: 0) | rm << 16;                      // rd = rn = w0
  if(cnst) arm8_imm(data, rm, num);
  if(cnst && (op & WIDE)) arm8_put(data, 0x93407c00 | rm << 5 | rm); // sxtw x16, w16
  switch(op & ~WIDE) {
    case '+': arm8_put(data, I3502_ADD  | w1); break;
    case '-': arm8_put(data, I3502_SUB  | w1); break;
    case '&': arm8_put(data, I3510_AND  | w1); break;
    case '|': arm8_put(data, I3510_ORR  | w1); break;
    case '^': arm8_put(data, I3510_EOR  | w1); break;
    case '*': arm8_put(data, I3509_MADD | w1 | 31 << 10); break;   // mul w0, w0, w1
    case '/': arm8_put(data, I3508_SDIV | w1); break;
    case '%':
      arm8_put(data, I3508_SDIV | w1 | r2);                         // sdiv w2, w0, w1
      arm8_put(data, I3509_MSUB | w1 | r2 << 5);                    // msub w0, w2, w1, w0
      break;
    case tokd('<', '<'): arm8_put(data, I3508_LSLV | w1); break;
    case tokd('>', '>'): arm8_put(data, I3508_ASRV | w1); break;
    default: { // comparison: cmp w0, w1; cset w0, cc
      op &= ~WIDE;
      arm8_cond cc = op == tokd('=', '=')? COND_EQ: op == tokd('!', '=')? COND_NE:
                     op == '<'? COND_LT: op == tokd('<', '=')? COND_LE: op == '>'? COND_GT: COND_GE;
      arm8_put(data, I3502_SUBS | w1 | 31);
      arm8_put(data, I3506_CSINC | 31 << 16 | (cc ^ 1) << 12 | 31 << 5 | r0);
    }
  }
}

void arm8_when(file* data, bool nz) {
  arm8_put(data, X | (nz? I3201_CBNZ: I3201_CBZ) | r0);             // cb(n)z x0, patched by mend
}

void arm8_jump(file* data) {
  arm8_put(data, I3206_B);                                          // b, patched by mend
}

void arm8_loop(file* data, byte* dest) {
  arm8_put(data, I3206_B | (((dest - data->body) >> 2) & 0x3ffffff)); // b dest
}

void arm8_done(file* data) {
  arm8_put(data, 0x910003bf);                                       // mov sp, x29
  arm8_put(data, 0xa8c17bfd);                                       // ldp x29, x30, [sp], #16
  arm8_put(data, form(brur, ret, .rn=r30));                         // ret
}
// prev is the end of a cbz or b
void arm8_mend(file* data, byte* prev) {
  uint32_t* inst = (uint32_t*)prev - 1;
  s64i dist = (data->body - (byte*)inst) >> 2;
  if((*inst & 0xfc000000) == I3206_B) {
    if(dist >= 1 << 25) fail("jump too far: %ld words\n", dist);
    *inst |= dist & 0x3ffffff;
  }
  else {
    if(dist >= 1 << 18) fail("jump too far: %ld words\n", dist);
    *inst |= (dist & 0x7ffff) << 5;
  }
}

// program entry: exit(main())
void arm8_boot(file* data) {
  arm8_put(data, I3206_BL);   // bl main, patched by link
  arm8_put(data, 0xd2800ba8); // mov x8, #93 (SYS_exit)
  arm8_put(data, 0xd4000001); // svc #0
}

void arm8_link(file*, byte* boot, byte* main) {
  *(uint32_t*)boot = I3206_BL | (((main - boot) >> 2) & 0x3ffffff);
}

// called by boot, which leaves sp as it was: [sp] is argc, argv follows it
void arm8_crt(file* data, byte* main, byte* start) {
  s64i d = main - data->body - 8;
  if(d < -(1 << 20)) fail("main too far: %ld bytes\n", d);
  arm8_put(data, 0xf94003e1);                                       // ldr x1, [sp]
  arm8_put(data, 0x910023e2);                                       // add x2, sp, #8
  arm8_put(data, I3406_ADR | (d & 3) << 29 | ((d >> 2) & 0x7ffff) << 5 | r0); // adr x0, main
  for(u32i r = 3; r < 6; r++) arm8_put(data, X | dati_movz | r);    // mov xr, #0
  byte* at = data->body;
  arm8_dial(data);
  arm8_link(data, at, start);
  arm8_put(data, 0xd4200000);                                       // brk #0: start never returns
}

// AAPCS64: arguments in x0-x5, sp is always 16-byte aligned here. Apple passes
// the variadic ones on the stack instead, 8 bytes each, through x9-x14
void arm8_ffi(file* data, byte* func, u32i argc, u32i fixed, osys os) {
  u32i vary = os == os_mac && fixed < argc? argc - fixed: 0, room = (vary * 8 + 15) & -16;
  if(!func) arm8_put(data, 0xf9400010);                             // ldr x16, [x0]
  for(u32i i = argc; i--; )
    arm8_put(data, 0xf84107e0 | (vary && i >= fixed? 9 + i - fixed: i)); // ldr xi, [sp], #16
  if(room) arm8_put(data, 0xd10003ff | room << 10);                 // sub sp, sp, #room
  for(u32i j = 0; j < vary; j++)
    arm8_put(data, 0xf90003e0 | j << 10 | (9 + j));                 // str x(9+j), [sp, #8j]
  if(func) { byte* at = data->body; arm8_dial(data); arm8_link(data, at, func); }
  else arm8_put(data, 0xd63f0200);                                  // blr x16
  if(room) arm8_put(data, 0x910003ff | room << 10);                 // add sp, sp, #room
}

const emit arm8 = {
  .move = arm8_move,
  .load = arm8_load,
  .stor = arm8_stor,
  .peek = arm8_peek,
  .addr = arm8_addr,
  .glob = arm8_glob,
  .site = arm8_site,
  .read = arm8_read,
  .fill = arm8_fill,
  .push = arm8_save,
  .pull = arm8_pull,
  .swap = arm8_swap,
  .cast = arm8_cast,
  .calc = arm8_calc,
  .when = arm8_when,
  .jump = arm8_jump,
  .back = arm8_mend,
  .loop = arm8_loop,
  .open = arm8_open,
  .size = arm8_size,
  .parm = arm8_parm,
  .args = arm8_args,
  .dial = arm8_dial,
  .trap = arm8_trap,
  .done = arm8_done,
  .boot = arm8_boot,
  .link = arm8_link,
  .ffi  = arm8_ffi,
  .crt  = arm8_crt,
  .cpu  = cpu_arm8,
};

