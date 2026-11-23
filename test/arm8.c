// vim: ts=2:sw=2
// arm8's spec: AArch64 encodings of each emit call, acc in w0/x0, tmp in
// w1/x1, little endian words
#include "emit.h"
#include "../arm8.c"
#include "test.h"

#define e arm8

// past the reach of a conditional branch
static byte  farb[sizeof(file) + (1 << 21)];
static file* far = (file*)farb;
static u32i  at32(const byte* p) { u32i w; memcpy(&w, p, 4); return w; }

spec("arm8") {
  context("registers of locals") {
    it("moves between a register and acc, tmp or an argument register") {
      check(emits("e0 03 13 2a", e.rop(t, '=', ACC, 0, 0)));              // mov w0, w19
      check(emits("e1 03 14 aa", e.rop(t, '=' | WIDE, TMP, 1, 0)));       // mov x1, x20
      check(emits("f3 03 00 2a", e.rop(t, '=', 0, ACC, 0)));              // mov w19, w0
      check(emits("fc 03 03 aa", e.rop(t, '=' | WIDE, 9, ARG - 3, 0)));   // mov x28, x3
      check(emits("e1 03 15 aa", e.rop(t, '=' | WIDE, ARG - 1, 2, 0)));   // mov x1, x21
    }
    it("has the spares x2-x7 past them, and moves none to itself on 64 bits") {
      check(emits("e3 03 00 2a", e.rop(t, '=', 11, ARG - 0, 0)));         // mov w3, w0
      check(emits("e7 00 02 8b", e.rop(t, '+' | WIDE, 15, 10, 0)));       // add x7, x7, x2
      check(emits("e4 03 04 2a", e.rop(t, '=', 12, ARG - 4, 0)));         // mov w4, w4: clears the top half
      check(emits("", e.rop(t, '=' | WIDE, 13, ARG - 5, 0)));             // mov x5, x5
    }
    it("moves a constant, sign extended on 64 bits when below 0") {
      check(emits("b3 00 80 52", e.rop(t, '=', 0, NUM, 5)));              // mov w19, #5
      // mov w20, #0x2345; movk w20, #1, lsl 16
      check(emits("b4 68 84 52 34 00 a0 72", e.rop(t, '=' | WIDE, 1, NUM, 0x12345)));
      // mov w19, #0xffff; movk w19, #0xffff, lsl 16; sxtw x19, w19
      check(emits("f3 ff 9f 52 f3 ff bf 72 73 7e 40 93", e.rop(t, '=' | WIDE, 0, NUM, -1)));
      // mov w2, #0xfffe; movk w2, #0xffff, lsl 16; sxtw x2, w2
      check(emits("c2 ff 9f 52 e2 ff bf 72 42 7c 40 93", e.rop(t, '=' | WIDE, ARG - 2, NUM, -2)));
    }
    it("adds, subtracts and compares a constant below 4K as it is") {
      check(emits("73 06 00 11", e.rop(t, '+', 0, NUM, 1)));                               // add w19, w19, #1
      check(emits("b5 fe 3f d1", e.rop(t, '-' | WIDE, 2, NUM, 4095)));                     // sub x21, x21, #4095
      check(emits("7f 02 00 f1", e.rop(t, tokd('!', '=') | TEST | WIDE, 0, NUM, 0)));      // cmp x19, #0
    }
    it("takes another constant through x16") {
      check(emits("10 00 82 52 b5 02 10 8b", e.rop(t, '+' | WIDE, 2, NUM, 4096)));        // mov w16, #4096; add x21, x21, x16
      // mov w16, #0xffff; movk w16, #0xffff, lsl 16; add w22, w22, w16
      check(emits("f0 ff 9f 52 f0 ff bf 72 d6 02 10 0b", e.rop(t, '+', 3, NUM, -1)));
      check(emits("f0 1f 80 52 73 02 10 0a", e.rop(t, '&', 0, NUM, 255)));                // mov w16, #255; and w19, w19, w16
      // mov w16, #0x900; movk w16, #0x3d, lsl 16; cmp w23, w16
      check(emits("10 20 81 52 b0 07 a0 72 ff 02 10 6b", e.rop(t, tokd('<', '=') | TEST, 4, NUM, 4000000)));
    }
    it("computes and compares between registers and acc") {
      check(emits("94 02 13 4b", e.rop(t, '-', 1, 0, 0)));                    // sub w20, w20, w19
      check(emits("00 00 16 2a", e.rop(t, '|', ACC, 3, 0)));                  // orr w0, w0, w22
      check(emits("d6 02 00 ca", e.rop(t, '^' | WIDE, 3, ACC, 0)));           // eor x22, x22, x0
      check(emits("00 00 14 0b", e.rop(t, '+', ACC, 1, 0)));                  // add w0, w0, w20
      check(emits("7f 02 14 6b", e.rop(t, '<' | TEST, 0, 1, 0)));             // cmp w19, w20
    }
    it("adds an index to acc, sign extended and scaled") {
      check(emits("00 c8 33 8b", e.step(t, 0, 4, 0)));                // add x0, x0, w19, sxtw #2
      check(emits("00 6c 34 8b", e.step(t, 1, 8, 1)));                // add x0, x0, x20, uxtx #3
      check(emits("00 c0 21 8b", e.step(t, TMP, 1, 0)));              // add x0, x0, w1, sxtw
      check(emits("00 64 3c 8b", e.step(t, 9, 2, 1)));                // add x0, x0, x28, uxtx #1
    }
  }

  context("values") {
    it("moves an immediate, in halves past 16 bits") {
      check(emits("40 05 80 52", e.move(t, 42)));                  // mov w0, #42
      check(emits("a0 68 84 52 20 00 a0 72", e.move(t, 0x12345))); // mov w0, #0x2345; movk w0, #1, lsl 16
      check(emits("e0 ff 9f 52 e0 ff bf 72", e.move(t, -1)));      // mov w0, #0xffff; movk w0, #0xffff, lsl 16
    }
    it("loads a slot by size, zero extending") {
      check(emits("a0 83 5f 38", e.load(t, -8, 1)));   // ldurb w0, [x29, #-8]
      check(emits("a0 83 5f b8", e.load(t, -8, 4)));   // ldur w0, [x29, #-8]
      check(emits("a0 03 5f f8", e.load(t, -16, 8)));  // ldur x0, [x29, #-16]
    }
    it("stores a slot by size") {
      check(emits("a0 83 1f 78", e.stor(t, -8, 2)));   // sturh w0, [x29, #-8]
      check(emits("a0 83 1f b8", e.stor(t, -8, 4)));   // stur w0, [x29, #-8]
    }
    it("peeks into tmp") {
      check(emits("a1 03 5f b8", e.peek(t, -16, 4)));  // ldur w1, [x29, #-16]
    }
    it("takes the address of a slot") {
      check(emits("a0 23 00 d1", e.addr(t, -8)));      // sub x0, x29, #8
    }
    it("reaches a slot past 4K in two subs, past 16M through x16") {
      // sub x16, x29, #0x1, lsl #12; sub x16, x16, #0x238; ldur w0, [x16]
      check(emits("b0 07 40 d1 10 e2 08 d1 00 02 40 b8", e.load(t, -0x1238, 4)));
      check(emits("a0 13 40 d1", e.addr(t, -0x4000))); // sub x0, x29, #0x4, lsl #12
      // mov x16, #0x5678; movk x16, #0x1234, lsl #16; sub x0, x29, x16
      check(emits("10 cf 8a d2 90 46 a2 f2 a0 63 30 cb", e.addr(t, -0x12345678)));
    }
    it("sign extends to 64 bits") {
      check(emits("00 7c 40 93", e.cast(t, 1)));       // sxtw x0, w0
    }
  }

  context("stack") {
    it("pushes acc, a 16-byte slot") {
      check(emits("e0 0f 1f f8", e.push(t)));          // str x0, [sp, #-16]!
    }
    it("pulls into acc, its value to tmp") {
      check(emits("e1 03 00 aa e0 07 41 f8", e.pull(t))); // mov x1, x0; ldr x0, [sp], #16
    }
    it("swaps acc to tmp") {
      check(emits("e1 03 00 aa", e.swap(t)));          // mov x1, x0
    }
    it("keeps acc on the stack") {
      check(emits("e0 0f 1f f8", e.keep(t)));          // str x0, [sp, #-16]!
    }
    it("lifts a keep into a move to scratch x9 + k") {
      byte* k;
      check(emits("e9 03 00 aa", k = here, e.keep(t), e.lift(t, k, 0))); // mov x9, x0
      check(emits("ec 03 00 aa", k = here, e.keep(t), e.lift(t, k, 3))); // mov x12, x0
    }
    it("lifts a keep into a move to a register of locals x19 + k - HELD from HELD up") {
      byte* k;
      check(emits("f3 03 00 aa", k = here, e.keep(t), e.lift(t, k, HELD)));     // mov x19, x0
      check(emits("f4 03 00 aa", k = here, e.keep(t), e.lift(t, k, HELD + 1))); // mov x20, x0
    }
    it("takes a scratch into acc, its value to tmp") {
      check(emits("e1 03 00 aa e0 03 09 aa", e.take(t, 0))); // mov x1, x0; mov x0, x9
      check(emits("e1 03 00 aa e0 03 0c aa", e.take(t, 3))); // mov x1, x0; mov x0, x12
      check(emits("e1 03 00 aa e0 03 14 aa", e.take(t, HELD + 1))); // mov x1, x0; mov x0, x20
    }
  }

  context("calc") {
    it("adds a constant through w16, on 32 or 64 bits") {
      check(emits("70 00 80 52 00 00 10 0b", e.calc(t, '+', 1, 3)));  // mov w16, #3; add w0, w0, w16
      // mov w16, #3; sxtw x16, w16; add x0, x0, x16
      check(emits("70 00 80 52 10 7e 40 93 00 00 10 8b", e.calc(t, '+' | WIDE, 1, 3)));
    }
    it("multiplies, divides and takes the remainder by tmp") {
      check(emits("00 7c 01 1b", e.calc(t, '*', 0, 0)));  // mul w0, w0, w1
      check(emits("00 0c c1 1a", e.calc(t, '/', 0, 0)));  // sdiv w0, w0, w1
      check(emits("02 0c c1 1a 40 80 01 1b", e.calc(t, '%', 0, 0))); // sdiv w2, w0, w1; msub w0, w2, w1, w0
    }
    it("divides by a power of 2 with shifts, rounding toward zero") {
      // asr w16, w0, #31; add w0, w0, w16, lsr #30; asr w0, w0, #2
      check(emits("10 7c 1f 13 00 78 50 0b 00 7c 02 13", e.calc(t, '/', 1, 4)));
      // asr w16, w0, #31; add w16, w0, w16, lsr #30; asr w16, w16, #2; sub w0, w0, w16, lsl #2
      check(emits("10 7c 1f 13 10 78 50 0b 10 7e 02 13 00 08 10 4b", e.calc(t, '%', 1, 4)));
      // asr x16, x0, #63; add x0, x0, x16, lsr #63; asr x0, x0, #1
      check(emits("10 fc 7f 93 00 fc 50 8b 00 fc 41 93", e.calc(t, '/' | WIDE, 1, 2)));
      // asr x16, x0, #63; add x16, x0, x16, lsr #56; asr x16, x16, #8; sub x0, x0, x16, lsl #8
      check(emits("10 fc 7f 93 10 e0 50 8b 10 fe 48 93 00 20 10 cb", e.calc(t, '%' | WIDE, 1, 256)));
    }
    it("divides by 1 with nothing, takes the remainder as 0") {
      check(emits("", e.calc(t, '/', 1, 1)));
      check(emits("00 00 80 52", e.calc(t, '%', 1, 1)));  // mov w0, #0
    }
    it("shifts by tmp") {
      check(emits("00 20 c1 1a", e.calc(t, tokd('<', '<'), 0, 0)));  // lsl w0, w0, w1
    }
    it("compares into 0 or 1") {
      check(emits("1f 00 01 6b e0 c7 9f 1a", e.calc(t, tokd('<', '='), 0, 0))); // cmp w0, w1; cset w0, le
    }
    it("compares into the flags alone for a TEST") {
      check(emits("1f 00 01 6b", e.calc(t, tokd('<', '=') | TEST, 0, 0))); // cmp w0, w1
      // mov w16, #5; sxtw x16, w16; cmp x0, x16
      check(emits("b0 00 80 52 10 7e 40 93 1f 00 10 eb", e.calc(t, '<' | TEST | WIDE, 1, 5)));
    }
    it("turns the flags into 0 or 1") {
      check(emits("e0 c7 9f 1a", e.flag(t, tokd('<', '='))));  // cset w0, le
    }
  }

  context("branches") {
    byte* m;
    it("branches on zero past what follows, once mended") {
      // cbz x0, +12; nop; mov w0, #1
      check(emits("60 00 00 b4 1f 20 03 d5 20 00 80 52", e.when(t, 0), m = here, e.move(t, 1), e.back(t, m)));
    }
    it("branches on non zero") {
      check(emits("00 00 00 b5 1f 20 03 d5", e.when(t, 1))); // cbnz x0; nop
    }
    it("branches on the flags when a comparison fails, once mended") {
      // b.ge +12; nop; mov w0, #1
      check(emits("6a 00 00 54 1f 20 03 d5 20 00 80 52", e.cond(t, '<', 0), m = here, e.move(t, 1), e.back(t, m)));
    }
    it("branches on the flags when a comparison holds") {
      check(emits("0b 00 00 54 1f 20 03 d5", e.cond(t, '<', 1))); // b.lt; nop
    }
    it("branches past 1M as the opposite branch over a b") {
      far->tail = farb + sizeof farb;
      far->body = far->head;
      e.when(far, 0), m = far->body, far->body += 1 << 20, e.back(far, m);
      check(at32(far->head) == 0xb5000040);          // cbnz x0, +8
      check(at32(far->head + 4) == 0x14040001);      // b +0x100004
      far->body = far->head;
      e.cond(far, '<', 0), m = far->body, far->body += 1 << 20, e.back(far, m);
      check(at32(far->head) == 0x5400004b);          // b.lt +8
      check(at32(far->head + 4) == 0x14040001);      // b +0x100004
    }
    it("jumps forward, once mended") {
      check(emits("02 00 00 14 20 00 80 52", e.jump(t), m = here, e.move(t, 1), e.back(t, m))); // b +8; mov w0, #1
    }
    it("loops back") {
      check(emits("20 00 80 52 ff ff ff 17", m = here, e.move(t, 1), e.loop(t, m))); // mov w0, #1; b -4
    }
  }

  context("start") {
    it("reaches main past 1M from the C start through x16") {
      far->tail = farb + sizeof farb;
      far->body = far->head + (1 << 20);
      e.crt(far, far->head, far->head, cl_glibc);
      byte* p = far->head + (1 << 20) + 8;          // ldr x1, [sp]; add x2, sp, #8; then
      u32i  d = (at32(p + 4) >> 5 & 0xffff) | (at32(p + 8) >> 5 & 0xffff) << 16;
      check(at32(p) == 0x10000000);                  // adr x0, .
      check(d == (1 << 20) + 8);                     // mov x16, #lo; movk x16, #hi, lsl #16
      check(at32(p + 12) == 0xcb100000);             // sub x0, x0, x16
    }
    it("starts a C library through the import slot in acc, kept in x17") {
      far->tail = farb + sizeof farb;
      far->body = far->head;
      e.crt(far, far->head, NULL, cl_glibc);
      check(at32(far->head) == 0xf9400011);          // ldr x17, [x0]
      check(at32(far->body - 8) == 0xd63f0220);      // blr x17
      check(at32(far->body - 4) == 0xd4200000);      // brk #0
    }
    it("starts bionic with the initial sp, main in x2 and 48 0 bytes of structors in x3") {
      far->tail = farb + sizeof farb;
      far->body = far->head;
      e.crt(far, far->head + 8, NULL, cl_bionic);
      static const u32i want[] = {
        0xf9400011, 0x10000020, 0xaa0003e2, 0x910003e0, // ldr x17, [x0]; adr x0, .+4; mov x2, x0; mov x0, sp
        0xd100c3ff, 0xa9007fff, 0xa9017fff, 0xa9027fff, // sub sp, sp, #48; stp xzr, xzr, [sp, #0, #16, #32]
        0x910003e3, 0xd2800001, 0xd63f0220, 0xd4200000, // mov x3, sp; mov x1, #0; blr x17; brk #0
      };
      check(far->body - far->head == sizeof want && !memcmp(far->head, want, sizeof want));
    }
  }

  context("functions") {
    byte* f;
    byte* d;
    it("opens a frame, sized later") {
      // stp x29, x30, [sp, #-16]!; mov x29, sp; sub sp, sp, #32
      check(emits("fd 7b bf a9 fd 03 00 91 ff 83 00 d1", f = here, e.open(t), e.size(t, f, 32)));
    }
    it("grows a frame past 4K after the function, and comes back") {
      // stp x29, x30, [sp, #-16]!; mov x29, sp; b +16; mov sp, x29; ldp x29, x30, [sp], #16; ret;
      // sub sp, sp, #0x12, lsl #12; sub sp, sp, #0x340; b -20
      check(emits("fd 7b bf a9 fd 03 00 91 04 00 00 14 bf 03 00 91 fd 7b c1 a8 c0 03 5f d6 "
                  "ff 4b 40 d1 ff 03 0d d1 fb ff ff 17", f = here, e.open(t), e.done(t), e.size(t, f, 0x12340)));
    }
    it("unwinds and returns") {
      // mov sp, x29; ldp x29, x30, [sp], #16; ret
      check(emits("bf 03 00 91 fd 7b c1 a8 c0 03 5f d6", e.done(t)));
    }
    it("returns from a function with no frame") {
      check(emits("c0 03 5f d6", e.bare(t)));          // ret
    }
    it("stores a parameter register in its slot") {
      check(emits("a1 03 1f f8", e.parm(t, 1, -16)));  // stur x1, [x29, #-16]
    }
    it("saves a register of a local in a slot, and brings it back") {
      check(emits("b3 83 1f f8", e.stow(t, 0, -8, 0)));         // stur x19, [x29, #-8]
      check(emits("b4 03 5f f8", e.stow(t, 1, -16, 1)));        // ldur x20, [x29, #-16]
      check(emits("bc 03 1b f8", e.stow(t, 9, -80, 0)));        // stur x28, [x29, #-80]
    }
    it("pops an argument into its register") {
      check(emits("e2 07 41 f8", e.args(t, 2)));       // ldr x2, [sp], #16
    }
    it("passes acc in an argument register, x0 as it is") {
      check(emits("", e.pass(t, 0)));
      check(emits("e3 03 00 aa", e.pass(t, 3)));       // mov x3, x0
    }
    it("points a glob forward at data or back at code") {
      // adr x0, .; mov x16, #0x2348; movk x16, #0x1, lsl #16; add x0, x0, x16
      check(emits("00 00 00 10 10 69 84 d2 30 00 a0 f2 00 00 10 8b", d = here, e.glob(t), e.site(t, d, 0x12348)));
      // ...; sub x0, x0, x16
      check(emits("00 00 00 10 10 69 84 d2 30 00 a0 f2 00 00 10 cb", d = here, e.glob(t), e.site(t, d, -0x12348)));
    }
    it("links a call back or forward") {
      // mov sp, x29; ldp x29, x30, [sp], #16; ret; bl -12
      check(emits("bf 03 00 91 fd 7b c1 a8 c0 03 5f d6 fd ff ff 97",
                  f = here, e.done(t), d = here, e.dial(t), e.link(t, d, f)));
      // bl +8; mov w0, #7; ...
      check(emits("02 00 00 94 e0 00 80 52 bf 03 00 91 fd 7b c1 a8 c0 03 5f d6",
                  d = here, e.dial(t), e.move(t, 7), f = here, e.done(t), e.link(t, d, f)));
    }
  }

  context("reals") {
    it("computes in d0, d1 and the registers of real locals, d8 up") {
      check(emits("60 41 60 1e", e.fop(t, '=', ACC, 3)));         // fmov d0, d11
      check(emits("2f 40 60 1e", e.fop(t, '=', 7, TMP)));         // fmov d15, d1
      check(emits("", e.fop(t, '=', 2, 2)));
      check(emits("94 2a 7b 1e", e.fop(t, '+', 8, 15)));          // fadd d20, d20, d27: the spares, d20 up
      check(emits("00 28 61 1e", e.fop(t, '+', ACC, TMP)));       // fadd d0, d0, d1
      check(emits("08 39 60 1e", e.fop(t, '-', 0, ACC)));         // fsub d8, d8, d0
      check(emits("00 08 6d 1e", e.fop(t, '*', ACC, 5)));         // fmul d0, d0, d13
      check(emits("ef 19 6f 1e", e.fop(t, '/', 7, 7)));           // fdiv d15, d15, d15
    }
    it("compares with fcmp, < and <= as mi and ls, which a NaN fails") {
      check(emits("00 20 6a 1e", e.fop(t, '<' | REAL | TEST, ACC, 2)));                 // fcmp d0, d10
      check(emits("00 20 61 1e e0 87 9f 1a", e.fop(t, tokd('<', '=') | REAL, ACC, TMP))); // fcmp d0, d1; cset w0, ls
      check(emits("04 00 00 54 1f 20 03 d5", e.cond(t, '<' | REAL, 1)));  // b.mi; nop
      check(emits("05 00 00 54 1f 20 03 d5", e.cond(t, '<' | REAL, 0)));  // b.pl; nop
    }
    it("makes 0.0 with movi, another real through x16") {
      check(emits("00 e4 00 2f", e.fimm(t, ACC, 0)));             // movi d0, #0
      check(emits("0c e4 00 2f", e.fimm(t, 4, 0)));               // movi d12, #0
      check(emits("90 00 e8 d2 01 02 67 9e", e.fimm(t, TMP, 0x4004000000000000ull))); // mov x16, #0x4004 << 48; fmov d1, x16
      // mov x16, #0x2d18; movk x16, #0x5444, lsl 16; ... #0x21fb, lsl 32; ... #0x4009, lsl 48; fmov d14, x16
      check(emits("10 a3 85 d2 90 88 aa f2 70 3f c4 f2 30 01 e8 f2 0e 02 67 9e", e.fimm(t, 6, 0x400921fb54442d18ull)));
    }
    it("moves a real's bits to and from the integer registers") {
      check(emits("00 00 67 9e", e.fbit(t, 1, ACC, ACC)));        // fmov d0, x0
      check(emits("00 00 66 9e", e.fbit(t, 0, ACC, ACC)));        // fmov x0, d0
      check(emits("0a 00 67 9e", e.fbit(t, 1, 2, ARG)));          // fmov d10, x0
      check(emits("e4 01 66 9e", e.fbit(t, 0, 7, ARG - 4)));      // fmov x4, d15
    }
    it("loads and stores a real in the frame and through x0") {
      check(emits("a0 83 5f fc", e.fslot(t, 0, ACC, -8)));                 // ldur d0, [x29, #-8]
      check(emits("ad 03 1f fc", e.fslot(t, 1, 5, -16)));                  // stur d13, [x29, #-16]
      check(emits("b0 b3 04 d1 00 02 00 fc", e.fslot(t, 1, ACC, -300)));   // sub x16, x29, #300; stur d0, [x16]
      check(emits("00 00 40 fc", e.fat(t, 0, ACC)));                       // ldur d0, [x0]
      check(emits("0b 00 00 fc", e.fat(t, 1, 3)));                         // stur d11, [x0]
    }
    it("saves the low 8 bytes of the register of a real local, which AAPCS64 keeps") {
      check(emits("a8 03 1f fc", e.fstow(t, 0, -16, 0, 8)));               // stur d8, [x29, #-16]
      check(emits("af 03 5e fc", e.fstow(t, 7, -32, 1, 8)));               // ldur d15, [x29, #-32]
    }
    it("keeps d0 pushed, lifted to d16 up or to the register of a local") {
      byte* k;
      check(emits("e0 0f 1f fc", e.fkeep(t)));                    // str d0, [sp, #-16]!
      check(emits("13 40 60 1e", k = here, e.fkeep(t), e.flift(t, k, 3)));        // fmov d19, d0
      check(emits("14 00 66 9e", k = here, e.fkeep(t), e.flift(t, k, HELD + 1))); // fmov x20, d0
      check(emits("01 40 60 1e 40 42 60 1e", e.ftake(t, 2)));     // fmov d1, d0; fmov d0, d18
      check(emits("01 40 60 1e 60 02 67 9e", e.ftake(t, HELD)));  // fmov d1, d0; fmov d0, x19
      check(emits("01 40 60 1e e0 07 41 fc", e.fpull(t)));        // fmov d1, d0; ldr d0, [sp], #16
    }
    it("turns an int or a long into a real in d0, and a real into a long") {
      check(emits("00 00 62 1e", e.turn(t, 1, 0)));  // scvtf d0, w0
      check(emits("00 00 62 9e", e.turn(t, 1, 1)));  // scvtf d0, x0
      check(emits("00 00 78 9e", e.turn(t, 0, 1)));  // fcvtzs x0, d0
    }
    it("passes reals in d registers, and takes a real result from d0") {
      // ldr x16, [x0]; ldr x1, [sp], #16; ldr d0, [sp], #16; ldr x0, [sp], #16; blr x16; fmov x0, d0
      check(emits("10 00 40 f9 e1 07 41 f8 e0 07 41 fc e0 07 41 f8 00 02 3f d6 00 00 66 9e",
                  e.ffi(t, NULL, 3, 3, 0x2 | 1 << RETS, os_linux)));
    }
    it("passes Apple's variadic reals on the stack, as their bits") {
      // ldr x16, [x0]; ldr x10, [sp], #16; ldr x9, [sp], #16; ldr d0, [sp], #16;
      // sub sp, sp, #16; str x9, [sp]; str x10, [sp, #8]; blr x16; add sp, sp, #16
      check(emits("10 00 40 f9 ea 07 41 f8 e9 07 41 f8 e0 07 41 fc ff 43 00 d1 e9 03 00 f9 ea 07 00 f9 "
                  "00 02 3f d6 ff 43 00 91", e.ffi(t, NULL, 3, 1, 0x5, os_mac)));
    }
    it("passes Windows' variadic reals in the x registers of their places") {
      // ldr x16, [x0]; ldr x2, [sp], #16; ldr x1, [sp], #16; ldr x0, [sp], #16; blr x16
      check(emits("10 00 40 f9 e2 07 41 f8 e1 07 41 f8 e0 07 41 f8 00 02 3f d6",
                  e.ffi(t, NULL, 3, 1, 0x6, os_win)));
    }
    it("enters from C code with reals, each to the x register of its place") {
      // mov x3, x1; fmov x2, d1; mov x1, x0; fmov x0, d0;
      // stp x29, x30, [sp, #-16]!; mov x29, sp; bl +16; fmov d0, x0; ldp x29, x30, [sp], #16; ret
      check(emits("e3 03 01 aa 22 00 66 9e e1 03 00 aa 00 00 66 9e fd 7b bf a9 fd 03 00 91 04 00 00 94 "
                  "00 00 67 9e fd 7b c1 a8 c0 03 5f d6", e.hail(t, os_linux, 4, 0x5 | 1 << RETS)));
      check(emits("", e.hail(t, os_linux, 2, 0)));
    }
  }
}
