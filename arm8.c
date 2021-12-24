#include "core.h"

enum {
  r0,  r1,  r2,  r3,  r4,  r5,  r6,  r7,
  r8,  r9,  r10, r11, r12, r13, r14, r15,
  r16, r17, r18, r19, r20, r21, r22, r23,
  r24, r25, r26, r27, r28, r29, r30,
};

typedef union {
  struct { uint32_t rd  : 5, imm :16, hw  : 2, _   : 6, opc : 2, sf  : 1; } dati;
  struct { uint32_t op4 : 5, rn  : 5, op3 : 6, op2 : 5, opc : 4, _   : 7; } brur;
  struct { uint32_t cond: 4, o0  : 1, imm :19, o1  : 1,          _   : 7; } brci;
  //struct { uint32_t op4 : 5, rn  : 5, op3 : 6, op2 : 5, opc : 4, op  : 7; } ldst;
  //struct { uint32_t op4 : 5, rn  : 5, op3 : 6, op2 : 5, opc : 4, op  : 7; } rdat;
  //struct { uint32_t op4 : 5, rn  : 5, op3 : 6, op2 : 5, opc : 4, op  : 7; } sdat;

  uint32_t n;
} arm8_form;

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

void arm8_emit() {
  printf("armv8\n");
  printf("%x\n", form(dati, movz, .imm=42, .rd=0));
  printf("%x\n", form(brur, ret, .rn=r30));
  uint32_t r = form(brur, ret, .rn=r30);
  uint32_t e = 0xd65f03c0;
  printf("%x\n%x\n%s\n", r, e, r==e? "true": "false");
}

void arm8_move(buff* data, uint32_t num) {
}

void arm8_when(buff* data, uint32_t num) {
}

void arm8_jump(buff* data, uint32_t num) {
}

void arm8_done(buff* data) {
}
void arm8_mend(buff* data) {
}

void arm8_halt() {
  printf("%x\n", 0xd65f03c0);
}

void arm8_text(void* data) {
  uint32_t* exe = data;
  int size = 0;

  //mov  w0, #42
  exe[size++] = form(dati, movz, .imm=42, .rd=0);

  //ret
  //exe[size++] = form(brur, ret, .rn=r30);
  exe[size++] = 0xd65f03c0;
}

emit arm8 = {
  .move = (flow)arm8_move,
  .when = (flow)arm8_when,
  .jump = (flow)arm8_jump,
  .done = (flow)arm8_done,
  .back = (flow)arm8_mend,
};

