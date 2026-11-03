// vim: ts=2:sw=2
// ia64's spec: x86-64 encodings of each emit call, acc in eax/rax, tmp in ecx
#include "emit.h"
#include "../ia64.c"
#include "test.h"

#define e ia64

spec("ia64") {
  context("values") {
    it("moves an immediate") {
      check(emits("b8 2a 00 00 00", e.move(t, 42)));    // mov eax, 42
      check(emits("b8 ff ff ff ff", e.move(t, -1)));    // mov eax, -1
    }
    it("loads a slot by size, zero extending") {
      check(emits("0f b6 45 f8", e.load(t, -8, 1)));    // movzx eax, byte [rbp-8]
      check(emits("8b 45 f8", e.load(t, -8, 4)));       // mov eax, [rbp-8]
      check(emits("48 8b 45 f0", e.load(t, -16, 8)));   // mov rax, [rbp-16]
    }
    it("stores a slot by size") {
      check(emits("66 89 45 f8", e.stor(t, -8, 2)));    // mov [rbp-8], ax
      check(emits("89 45 f8", e.stor(t, -8, 4)));       // mov [rbp-8], eax
    }
    it("peeks into tmp") {
      check(emits("8b 4d f0", e.peek(t, -16, 4)));      // mov ecx, [rbp-16]
    }
    it("takes the address of a slot") {
      check(emits("48 8d 45 f8", e.addr(t, -8)));       // lea rax, [rbp-8]
    }
    it("sign extends to 64 bits") {
      check(emits("48 63 c0", e.cast(t, 1)));           // movsxd rax, eax
    }
  }

  context("stack") {
    it("pushes acc") {
      check(emits("50", e.push(t)));                    // push rax
    }
    it("pulls into acc, its value to tmp") {
      check(emits("48 89 c1 58", e.pull(t)));           // mov rcx, rax; pop rax
    }
    it("swaps acc to tmp") {
      check(emits("48 89 c1", e.swap(t)));              // mov rcx, rax
    }
    it("keeps acc on the stack, with room for a move") {
      check(emits("50 66 90", e.keep(t)));              // push rax; xchg ax, ax
    }
    it("lifts a keep into a move to scratch r8 + k") {
      byte* k;
      check(emits("49 89 c0", k = here, e.keep(t), e.lift(t, k, 0))); // mov r8, rax
      check(emits("49 89 c3", k = here, e.keep(t), e.lift(t, k, 3))); // mov r11, rax
    }
    it("takes a scratch into acc, its value to tmp") {
      check(emits("48 89 c1 4c 89 c0", e.take(t, 0)));  // mov rcx, rax; mov rax, r8
      check(emits("48 89 c1 4c 89 d8", e.take(t, 3)));  // mov rcx, rax; mov rax, r11
    }
  }

  context("calc") {
    it("adds a constant, on 32 or 64 bits") {
      check(emits("83 c0 03", e.calc(t, '+', 1, 3)));          // add eax, 3
      check(emits("48 83 c0 03", e.calc(t, '+' | WIDE, 1, 3))); // add rax, 3
    }
    it("multiplies, divides and takes the remainder by tmp") {
      check(emits("0f af c1", e.calc(t, '*', 0, 0)));          // imul eax, ecx
      check(emits("99 f7 f9", e.calc(t, '/', 0, 0)));          // cdq; idiv ecx
      check(emits("99 f7 f9 89 d0", e.calc(t, '%', 0, 0)));    // cdq; idiv ecx; mov eax, edx
    }
    it("shifts by tmp") {
      check(emits("d3 e0", e.calc(t, tokd('<', '<'), 0, 0)));  // shl eax, cl
    }
    it("compares into 0 or 1") {
      // cmp eax, ecx; setle al; movzx eax, al
      check(emits("39 c8 0f 9e c0 0f b6 c0", e.calc(t, tokd('<', '='), 0, 0)));
    }
    it("compares into the flags alone for a TEST") {
      check(emits("39 c8", e.calc(t, tokd('<', '=') | TEST, 0, 0)));    // cmp eax, ecx
      check(emits("48 83 f8 05", e.calc(t, '<' | TEST | WIDE, 1, 5)));  // cmp rax, 5
    }
    it("turns the flags into 0 or 1") {
      check(emits("0f 9e c0 0f b6 c0", e.flag(t, tokd('<', '=')))); // setle al; movzx eax, al
    }
  }

  context("branches") {
    byte* m;
    it("branches on zero past what follows, once mended") {
      // test rax, rax; je +5; mov eax, 1
      check(emits("48 85 c0 0f 84 05 00 00 00 b8 01 00 00 00",
                  e.when(t, 0), m = here, e.move(t, 1), e.back(t, m)));
    }
    it("branches on non zero") {
      check(emits("48 85 c0 0f 85 00 00 00 00", e.when(t, 1)));  // test rax, rax; jne
    }
    it("branches on the flags when a comparison fails, once mended") {
      // jge +5; mov eax, 1
      check(emits("0f 8d 05 00 00 00 b8 01 00 00 00", e.cond(t, '<', 0), m = here, e.move(t, 1), e.back(t, m)));
    }
    it("branches on the flags when a comparison holds") {
      check(emits("0f 8c 00 00 00 00", e.cond(t, '<', 1)));       // jl
    }
    it("jumps forward, once mended") {
      // jmp +5; mov eax, 1
      check(emits("e9 05 00 00 00 b8 01 00 00 00", e.jump(t), m = here, e.move(t, 1), e.back(t, m)));
    }
    it("loops back in a short jump") {
      // mov eax, 1; jmp -7
      check(emits("b8 01 00 00 00 eb f9", m = here, e.move(t, 1), e.loop(t, m)));
    }
  }

  context("functions") {
    byte* f;
    byte* d;
    it("opens a frame, sized later") {
      // push rbp; mov rbp, rsp; sub rsp, 32
      check(emits("55 48 89 e5 48 81 ec 20 00 00 00", f = here, e.open(t), e.size(t, f, 32)));
    }
    it("leaves and returns") {
      check(emits("c9 c3", e.done(t)));                 // leave; ret
    }
    it("stores a parameter register in its slot") {
      check(emits("48 89 75 f0", e.parm(t, 1, -16)));   // mov [rbp-16], rsi
    }
    it("pops an argument into its register") {
      check(emits("5a", e.args(t, 2)));                 // pop rdx
    }
    it("passes acc in an argument register") {
      check(emits("48 89 c7", e.pass(t, 0)));           // mov rdi, rax
      check(emits("48 89 c1", e.pass(t, 3)));           // mov rcx, rax
      check(emits("49 89 c1", e.pass(t, 5)));           // mov r9, rax
    }
    it("links a call back or forward") {
      // leave; ret; call -7
      check(emits("c9 c3 e8 f9 ff ff ff", f = here, e.done(t), d = here, e.dial(t), e.link(t, d, f)));
      // call +5; mov eax, 7; leave; ret
      check(emits("e8 05 00 00 00 b8 07 00 00 00 c9 c3",
                  d = here, e.dial(t), e.move(t, 7), f = here, e.done(t), e.link(t, d, f)));
    }
    it("enters from Win64 C code through the SysV registers, none elsewhere") {
      // push rdi; push rsi; mov rdi, rcx; mov rsi, rdx; mov rdx, r8; mov rcx, r9;
      // mov r8, [rsp+56]; mov r9, [rsp+64]; call +3; pop rsi; pop rdi; ret
      check(emits("57 56 48 89 cf 48 89 d6 4c 89 c2 4c 89 c9 4c 8b 44 24 38 4c 8b 4c 24 40 "
                  "e8 03 00 00 00 5e 5f c3", e.hail(t, os_win)));
      check(emits("", e.hail(t, os_linux)));
    }
  }
}
