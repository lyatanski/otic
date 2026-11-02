// vim: ts=2:sw=2
// arm8's spec: AArch64 encodings of each emit call, acc in w0/x0, tmp in
// w1/x1, little endian words
#include "emit.h"
#include "../arm8.c"
#include "test.h"

#define e arm8

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
  }

  context("branches") {
    byte* m;
    it("branches on zero past what follows, once mended") {
      // cbz x0, +8; mov w0, #1
      check(emits("40 00 00 b4 20 00 80 52", e.when(t, 0), m = here, e.move(t, 1), e.back(t, m)));
    }
    it("branches on non zero") {
      check(emits("00 00 00 b5", e.when(t, 1)));       // cbnz x0
    }
    it("jumps forward, once mended") {
      check(emits("02 00 00 14 20 00 80 52", e.jump(t), m = here, e.move(t, 1), e.back(t, m))); // b +8; mov w0, #1
    }
    it("loops back") {
      check(emits("20 00 80 52 ff ff ff 17", m = here, e.move(t, 1), e.loop(t, m))); // mov w0, #1; b -4
    }
  }

  context("functions") {
    byte* f;
    byte* d;
    it("opens a frame, sized later") {
      // stp x29, x30, [sp, #-16]!; mov x29, sp; sub sp, sp, #32
      check(emits("fd 7b bf a9 fd 03 00 91 ff 83 00 d1", f = here, e.open(t), e.size(t, f, 32)));
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
