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

  context("registers of locals") {
    it("moves between a register and acc, tmp or an argument register") {
      check(emits("89 d8", e.rop(t, '=', ACC, 0, 0)));                 // mov eax, ebx
      check(emits("4c 89 e1", e.rop(t, '=' | WIDE, TMP, 1, 0)));       // mov rcx, r12
      check(emits("89 c3", e.rop(t, '=', 0, ACC, 0)));                 // mov ebx, eax
      check(emits("49 89 c7", e.rop(t, '=' | WIDE, 4, ACC, 0)));       // mov r15, rax
      check(emits("89 fb", e.rop(t, '=', 0, ARG - 0, 0)));             // mov ebx, edi
      check(emits("4d 89 c4", e.rop(t, '=' | WIDE, 1, ARG - 4, 0)));   // mov r12, r8
      check(emits("4c 89 ee", e.rop(t, '=' | WIDE, ARG - 1, 2, 0)));   // mov rsi, r13
    }
    it("has the spares rdi, rsi and rdx past them, and moves none to itself on 64 bits") {
      check(emits("89 f7", e.rop(t, '=', 5, ARG - 1, 0)));             // mov edi, esi
      check(emits("48 01 f2", e.rop(t, '+' | WIDE, 7, 6, 0)));         // add rdx, rsi
      check(emits("89 ff", e.rop(t, '=', 5, ARG - 0, 0)));             // mov edi, edi: clears the top half
      check(emits("", e.rop(t, '=' | WIDE, 7, ARG - 2, 0)));          // mov rdx, rdx
    }
    it("moves a constant, sign extended on 64 bits when below 0") {
      check(emits("bb 05 00 00 00", e.rop(t, '=', 0, NUM, 5)));                     // mov ebx, 5
      check(emits("41 bc 05 00 00 00", e.rop(t, '=' | WIDE, 1, NUM, 5)));           // mov r12d, 5
      check(emits("48 c7 c3 ff ff ff ff", e.rop(t, '=' | WIDE, 0, NUM, -1)));       // mov rbx, -1
      check(emits("48 c7 c7 fe ff ff ff", e.rop(t, '=' | WIDE, ARG - 0, NUM, -2))); // mov rdi, -2
      check(emits("b9 07 00 00 00", e.rop(t, '=', TMP, NUM, 7)));                   // mov ecx, 7
    }
    it("computes in a register, by a constant, acc or another register") {
      check(emits("83 c3 01", e.rop(t, '+', 0, NUM, 1)));                       // add ebx, 1
      check(emits("49 81 c5 e8 03 00 00", e.rop(t, '+' | WIDE, 2, NUM, 1000))); // add r13, 1000
      check(emits("41 29 dc", e.rop(t, '-', 1, 0, 0)));                         // sub r12d, ebx
      check(emits("81 e3 ff 00 00 00", e.rop(t, '&', 0, NUM, 255)));            // and ebx, 255
      check(emits("49 31 c6", e.rop(t, '^' | WIDE, 3, ACC, 0)));                // xor r14, rax
    }
    it("computes in acc by a register") {
      check(emits("44 01 e0", e.rop(t, '+', ACC, 1, 0)));                       // add eax, r12d
      check(emits("44 09 f0", e.rop(t, '|', ACC, 3, 0)));                       // or eax, r14d
    }
    it("compares a register into the flags") {
      check(emits("44 39 e3", e.rop(t, '<' | TEST, 0, 1, 0)));                            // cmp ebx, r12d
      check(emits("48 83 fb 00", e.rop(t, tokd('!', '=') | TEST | WIDE, 0, NUM, 0)));     // cmp rbx, 0
      check(emits("41 81 ff 00 09 3d 00", e.rop(t, tokd('<', '=') | TEST, 4, NUM, 4000000))); // cmp r15d, 4000000
    }
    it("adds an index to acc, sign extended and scaled") {
      check(emits("48 63 cb 48 8d 04 88", e.step(t, 0, 4, 0)));       // movsxd rcx, ebx; lea rax, [rax+rcx*4]
      check(emits("49 63 cc 48 01 c8", e.step(t, 1, 1, 0)));          // movsxd rcx, r12d; add rax, rcx
      check(emits("4a 8d 04 e8", e.step(t, 2, 8, 1)));                // lea rax, [rax+r13*8]
      check(emits("4c 01 f0", e.step(t, 3, 1, 1)));                   // add rax, r14
      check(emits("48 63 c9 48 8d 04 48", e.step(t, TMP, 2, 0)));     // movsxd rcx, ecx; lea rax, [rax+rcx*2]
      check(emits("4a 8d 04 60", e.step(t, 1, 2, 1)));                // lea rax, [rax+r12*2]
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
    it("lifts a keep into a move to a register of locals from HELD up") {
      byte* k;
      check(emits("48 89 c3", k = here, e.keep(t), e.lift(t, k, HELD)));     // mov rbx, rax
      check(emits("49 89 c4", k = here, e.keep(t), e.lift(t, k, HELD + 1))); // mov r12, rax
    }
    it("takes a scratch into acc, its value to tmp") {
      check(emits("48 89 c1 4c 89 c0", e.take(t, 0)));  // mov rcx, rax; mov rax, r8
      check(emits("48 89 c1 4c 89 d8", e.take(t, 3)));  // mov rcx, rax; mov rax, r11
      check(emits("48 89 c1 48 89 d8", e.take(t, HELD))); // mov rcx, rax; mov rax, rbx
      check(emits("48 89 c1 4c 89 e0", e.take(t, HELD + 1))); // mov rcx, rax; mov rax, r12
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
    it("divides by a power of 2 with shifts, rounding toward zero") {
      // cdq; shr edx, 30; add eax, edx; sar eax, 2
      check(emits("99 c1 ea 1e 01 d0 c1 f8 02", e.calc(t, '/', 1, 4)));
      // cdq; shr edx, 30; add eax, edx; and eax, 3; sub eax, edx
      check(emits("99 c1 ea 1e 01 d0 83 e0 03 29 d0", e.calc(t, '%', 1, 4)));
      // cqo; shr rdx, 63; add rax, rdx; sar rax, 1
      check(emits("48 99 48 c1 ea 3f 48 01 d0 48 d1 f8", e.calc(t, '/' | WIDE, 1, 2)));
      // cqo; shr rdx, 56; add rax, rdx; and rax, 255; sub rax, rdx
      check(emits("48 99 48 c1 ea 38 48 01 d0 48 81 e0 ff 00 00 00 48 29 d0", e.calc(t, '%' | WIDE, 1, 256)));
    }
    it("divides by 1 with nothing, takes the remainder as 0") {
      check(emits("", e.calc(t, '/', 1, 1)));
      check(emits("31 c0", e.calc(t, '%', 1, 1)));             // xor eax, eax
    }
    it("divides by other constants through ecx") {
      check(emits("c7 c1 06 00 00 00 99 f7 f9", e.calc(t, '/', 1, 6)));   // mov ecx, 6; cdq; idiv ecx
      check(emits("c7 c1 fc ff ff ff 99 f7 f9", e.calc(t, '/', 1, -4)));  // mov ecx, -4; cdq; idiv ecx
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
    it("grows a frame past 4K after the function a page at a time, and comes back") {
      // push rbp; mov rbp, rsp; jmp +4; nop; leave; ret;
      // sub rsp, 0x340; test [rsp], rsp; mov r11d, 0x12;
      // sub rsp, 4096; test [rsp], rsp; dec r11d; jnz -16; jmp -40
      check(emits("55 48 89 e5 e9 04 00 00 00 66 90 c9 c3 "
                  "48 81 ec 40 03 00 00 48 85 24 24 41 bb 12 00 00 00 "
                  "48 81 ec 00 10 00 00 48 85 24 24 41 ff cb 75 f0 e9 d8 ff ff ff",
                  f = here, e.open(t), e.done(t), e.size(t, f, 0x12340)));
    }
    it("leaves and returns") {
      check(emits("c9 c3", e.done(t)));                 // leave; ret
    }
    it("returns from a function with no frame") {
      check(emits("c3", e.bare(t)));                    // ret
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
                  "e8 03 00 00 00 5e 5f c3", e.hail(t, os_win, 0, 0)));
      check(emits("", e.hail(t, os_linux, 0, 0)));
    }
    it("saves a register of a local in a slot, and brings it back") {
      check(emits("48 89 5d f8", e.stow(t, 0, -8, 0)));            // mov [rbp-8], rbx
      check(emits("4c 8b 65 f0", e.stow(t, 1, -16, 1)));           // mov r12, [rbp-16]
      check(emits("4c 89 7d d8", e.stow(t, 4, -40, 0)));           // mov [rbp-40], r15
    }
    it("starts a C library through the import slot in acc") {
      // mov esi, [rsp+8]; lea rdx, [rsp+16]; lea rdi, [rip-16]; xor ecx, ecx;
      // xor r8d, r8d; xor r9d, r9d; and rsp, -16; call [rax]; hlt
      check(emits("8b 74 24 08 48 8d 54 24 10 48 8d 3d f0 ff ff ff 31 c9 45 31 c0 45 31 c9 "
                  "48 83 e4 f0 ff 10 f4", e.crt(t, t->head, NULL, cl_glibc)));
    }
    it("starts bionic with the initial stack, main in rdx and 48 0 bytes of structors in rcx") {
      // lea rdi, [rsp+8]; xor esi, esi; lea rdx, [rip-14]; and rsp, -16; xor ecx, ecx;
      // push rcx (6 times); mov rcx, rsp; call [rax]; hlt
      check(emits("48 8d 7c 24 08 31 f6 48 8d 15 f2 ff ff ff 48 83 e4 f0 31 c9 "
                  "51 51 51 51 51 51 48 89 e1 ff 10 f4", e.crt(t, t->head, NULL, cl_bionic)));
    }
  }

  context("reals") {
    it("computes in xmm0, xmm1 and the registers of real locals, xmm8 up") {
      check(emits("66 41 0f 28 c3", e.fop(t, '=', ACC, 3)));      // movapd xmm0, xmm11
      check(emits("66 44 0f 28 f9", e.fop(t, '=', 7, TMP)));      // movapd xmm15, xmm1
      check(emits("", e.fop(t, '=', 2, 2)));
      check(emits("f2 0f 58 c1", e.fop(t, '+', ACC, TMP)));       // addsd xmm0, xmm1
      check(emits("f2 44 0f 5c c0", e.fop(t, '-', 0, ACC)));      // subsd xmm8, xmm0
      check(emits("f2 41 0f 59 c5", e.fop(t, '*', ACC, 5)));      // mulsd xmm0, xmm13
      check(emits("f2 45 0f 5e ff", e.fop(t, '/', 7, 7)));        // divsd xmm15, xmm15
    }
    it("has the spares xmm6 and xmm7 past them") {
      check(emits("f2 0f 58 f7", e.fop(t, '+', 8, 9)));           // addsd xmm6, xmm7
      check(emits("66 0f 28 c7", e.fop(t, '=', ACC, 9)));         // movapd xmm0, xmm7
    }
    it("compares < the other way round, so a NaN fails it") {
      check(emits("66 44 0f 2e d0", e.fop(t, '<' | REAL | TEST, ACC, 2)));                // ucomisd xmm10, xmm0
      check(emits("66 44 0f 2e c9", e.fop(t, tokd('>', '=') | REAL | TEST, 1, TMP)));     // ucomisd xmm9, xmm1
      check(emits("0f 87 00 00 00 00", e.cond(t, '<' | REAL, 1)));                        // ja
      check(emits("0f 86 00 00 00 00", e.cond(t, '<' | REAL, 0)));                        // jbe
      check(emits("0f 83 00 00 00 00", e.cond(t, tokd('>', '=') | REAL, 1)));             // jae
    }
    it("compares == as equal and ordered, in ZF") {
      // ucomisd xmm0, xmm1; setnp al; sete cl; and cl, al; setne al; movzx eax, al
      check(emits("66 0f 2e c1 0f 9b c0 0f 94 c1 20 c1 0f 95 c0 0f b6 c0", e.fop(t, tokd('=', '=') | REAL, ACC, TMP)));
      check(emits("0f 84 00 00 00 00", e.cond(t, tokd('!', '=') | REAL, 1))); // je
    }
    it("makes 0.0 with xorps, another real through rax") {
      check(emits("0f 57 c0", e.fimm(t, ACC, 0)));                // xorps xmm0, xmm0
      check(emits("45 0f 57 e4", e.fimm(t, 4, 0)));               // xorps xmm12, xmm12
      // movabs rax, 0x400921fb54442d18; movq xmm14, rax
      check(emits("48 b8 18 2d 44 54 fb 21 09 40 66 4c 0f 6e f0", e.fimm(t, 6, 0x400921fb54442d18ull)));
    }
    it("moves a real's bits to and from the integer registers") {
      check(emits("66 48 0f 6e c0", e.fbit(t, 1, ACC, ACC)));     // movq xmm0, rax
      check(emits("66 48 0f 7e c0", e.fbit(t, 0, ACC, ACC)));     // movq rax, xmm0
      check(emits("66 4c 0f 6e d7", e.fbit(t, 1, 2, ARG)));       // movq xmm10, rdi
      check(emits("66 4d 0f 7e f8", e.fbit(t, 0, 7, ARG - 4)));   // movq r8, xmm15
    }
    it("loads and stores a real in the frame and through rax") {
      check(emits("f2 0f 10 45 f8", e.fslot(t, 0, ACC, -8)));             // movsd xmm0, [rbp-8]
      check(emits("f2 44 0f 11 6d f0", e.fslot(t, 1, 5, -16)));          // movsd [rbp-16], xmm13
      check(emits("f2 0f 10 8d 00 f0 ff ff", e.fslot(t, 0, TMP, -4096))); // movsd xmm1, [rbp-4096]
      check(emits("f2 0f 10 00", e.fat(t, 0, ACC)));                      // movsd xmm0, [rax]
      check(emits("f2 44 0f 11 18", e.fat(t, 1, 3)));                     // movsd [rax], xmm11
    }
    it("saves the register of a real local, all 16 bytes for Win64") {
      check(emits("f2 44 0f 11 45 f0", e.fstow(t, 0, -16, 0, 8)));        // movsd [rbp-16], xmm8
      check(emits("44 0f 11 7d e0", e.fstow(t, 7, -32, 0, 16)));          // movups [rbp-32], xmm15
      check(emits("44 0f 10 8d 00 ff ff ff", e.fstow(t, 1, -256, 1, 16))); // movups xmm9, [rbp-256]
    }
    it("keeps xmm0 pushed, lifted to xmm2 up or to the register of a local") {
      byte* k;
      check(emits("66 48 0f 7e c0 50", e.fkeep(t)));              // movq rax, xmm0; push rax
      check(emits("66 0f 28 e8 66 90", k = here, e.fkeep(t), e.flift(t, k, 3)));       // movapd xmm5, xmm0; xchg ax, ax
      check(emits("66 49 0f 7e c4 90", k = here, e.fkeep(t), e.flift(t, k, HELD + 1))); // movq r12, xmm0; nop
      check(emits("66 0f 28 c8 66 0f 28 c4", e.ftake(t, 2)));     // movapd xmm1, xmm0; movapd xmm0, xmm4
      check(emits("66 0f 28 c8 66 48 0f 6e c3", e.ftake(t, HELD))); // movapd xmm1, xmm0; movq xmm0, rbx
      check(emits("66 0f 28 c8 58 66 48 0f 6e c0", e.fpull(t)));  // movapd xmm1, xmm0; pop rax; movq xmm0, rax
    }
    it("turns an int or a long into a real in xmm0, and a real into a long") {
      check(emits("f2 0f 2a c0", e.turn(t, 1, 0)));               // cvtsi2sd xmm0, eax
      check(emits("f2 48 0f 2a c0", e.turn(t, 1, 1)));            // cvtsi2sd xmm0, rax
      check(emits("f2 48 0f 2c c0", e.turn(t, 0, 1)));            // cvttsd2si rax, xmm0
    }
    it("passes SysV reals in xmm registers, counted in al, and takes a real result from xmm0") {
      // mov r11, [rax]; pop rsi; pop rax; movq xmm0, rax; pop rdi; push rsp; push [rsp];
      // and rsp, -16; mov eax, 1; call r11; movq rax, xmm0; mov rsp, [rsp+8]
      check(emits("4c 8b 18 5e 58 66 48 0f 6e c0 5f 54 ff 34 24 48 83 e4 f0 b8 01 00 00 00 "
                  "41 ff d3 66 48 0f 7e c0 48 8b 64 24 08", e.ffi(t, NULL, 3, 3, 0x2 | 1 << RETS, os_linux)));
    }
    it("passes Win64 reals in both registers of their place") {
      // mov r11, [rax]; pop r8; movq xmm2, r8; pop rdx; pop rcx; movq xmm0, rcx; ...
      check(emits("4c 8b 18 41 58 66 49 0f 6e d0 5a 59 66 48 0f 6e c1 54 ff 34 24 48 83 e4 f0 "
                  "48 83 ec 20 41 ff d3 48 8b 64 24 28", e.ffi(t, NULL, 3, 1, 0x5, os_win)));
    }
    it("enters from SysV C code with reals, each to the register of its place") {
      // mov rcx, rsi; movq rdx, xmm1; mov rsi, rdi; movq rdi, xmm0;
      // call +6; movq xmm0, rax; ret
      check(emits("48 89 f1 66 48 0f 7e ca 48 89 fe 66 48 0f 7e c7 e8 06 00 00 00 66 48 0f 6e c0 c3",
                  e.hail(t, os_linux, 4, 0x5 | 1 << RETS)));
    }
    it("enters from Win64 C code with reals") {
      // ...; mov rcx, r9; movq rsi, xmm1; mov r8, [rsp+56]; mov r9, [rsp+64];
      // call +8; movq xmm0, rax; pop rsi; pop rdi; ret
      check(emits("57 56 48 89 cf 48 89 d6 4c 89 c2 4c 89 c9 66 48 0f 7e ce 4c 8b 44 24 38 4c 8b 4c 24 40 "
                  "e8 08 00 00 00 66 48 0f 6e c0 5e 5f c3", e.hail(t, os_win, 2, 0x2 | 1 << RETS)));
    }
  }
}
