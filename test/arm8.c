// vim: ts=2:sw=2
// arm8's spec: AArch64 encodings of each emit call, acc in w0/x0, tmp in
// w1/x1, little endian words
#include "emit.h"
#include "../arm8.c"
#include "test.h"

#define e arm8

// a text past the reach of a conditional branch, and its words
static byte  farb[sizeof(file) + (1 << 21)];
static file* far = (file*)farb;
static u32i  at32(const byte* p) { u32i w; memcpy(&w, p, 4); return w; }

spec("arm8") {
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
    it("takes a scratch into acc, its value to tmp") {
      check(emits("e1 03 00 aa e0 03 09 aa", e.take(t, 0))); // mov x1, x0; mov x0, x9
      check(emits("e1 03 00 aa e0 03 0c aa", e.take(t, 3))); // mov x1, x0; mov x0, x12
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
      e.crt(far, far->head, far->head);
      byte* p = far->head + (1 << 20) + 8;          // ldr x1, [sp]; add x2, sp, #8; then
      u32i  d = (at32(p + 4) >> 5 & 0xffff) | (at32(p + 8) >> 5 & 0xffff) << 16;
      check(at32(p) == 0x10000000);                  // adr x0, .
      check(d == (1 << 20) + 8);                     // mov x16, #lo; movk x16, #hi, lsl #16
      check(at32(p + 12) == 0xcb100000);             // sub x0, x0, x16
    }
    it("starts a C library through the import slot in acc, kept in x17") {
      far->tail = farb + sizeof farb;
      far->body = far->head;
      e.crt(far, far->head, NULL);
      check(at32(far->head) == 0xf9400011);          // ldr x17, [x0]
      check(at32(far->body - 8) == 0xd63f0220);      // blr x17
      check(at32(far->body - 4) == 0xd4200000);      // brk #0
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
    it("stores a parameter register in its slot") {
      check(emits("a1 03 1f f8", e.parm(t, 1, -16)));  // stur x1, [x29, #-16]
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
}
