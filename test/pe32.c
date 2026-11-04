// vim: ts=2:sw=2
// pe32's spec: the PE32+ executable it packs, exiting through kernel32
#include "form.h"
#include "../pe32.c"
#include "test.h"

#define CODE "\xb8\x07\x00\x00\x00\xc3" "\xb8\x2a\x00\x00\x00\xc3" // f, main
#define HEAD 0x200  // the headers, a file alignment
#define TRVA 0x1000 // text
#define DRVA 0x2000 // the imports, then data
#define IDAT ((sizeof(pe32_idata) + 15) & -16)

spec("pe32") {
  context("pack") {
    file*      out;
    pe32_head* h;
    it("writes a PE32+ executable for the target") {
      start(cpu_ia64, CODE, 12, "", 0, 6);
      check((out = packed(pe32)) && (h = (pe32_head*)out->head));
      check(bytes(out->head, "4d 5a") && bytes(out->head + h->dos.next, "50 45 00 00"));
      check(h->coff.arch == 0x8664 && h->coff.nsec == 2 && h->opt.magic == 0x20b);
      check(h->opt.system == 3 && h->opt.org == 0x140000000);
      start(cpu_arm8, CODE, 12, "", 0, 6);
      check((out = packed(pe32)) && ((pe32_head*)out->head)->coff.arch == 0xaa64);
    }
    it("enters at a stub after the code: exit(main())") {
      start(cpu_ia64, CODE, 12, "", 0, 6);
      check((out = packed(pe32)) && (h = (pe32_head*)out->head));
      check(h->text.off == HEAD && bytes(out->head + HEAD, "b8 07 00 00 00 c3 b8 2a"));
      check(h->opt.entry == TRVA + 16);
      // sub rsp, 40; call main (0x1006 - 0x1019); mov ecx, eax; call [iat]
      check(bytes(out->head + HEAD + 16, "48 83 ec 28 e8 ed ff ff ff 89 c1 ff 15"));
      s32i iat;
      memcpy(&iat, out->head + HEAD + 16 + 13, 4);
      check(TRVA + 16 + 17 + iat == DRVA + offs(pe32_idata, bind));
    }
    it("calls main with bl on arm8") {
      start(cpu_arm8, CODE, 12, "", 0, 8);
      check((out = packed(pe32)));
      // bl main (0x1008 - 0x1010) >> 2; ldr x16, iat; blr x16
      check(bytes(out->head + HEAD + 16, "fe ff ff 97"));
      check(bytes(out->head + HEAD + 24, "00 02 3f d6"));
    }
    it("imports ExitProcess from kernel32.dll") {
      start(cpu_ia64, CODE, 12, "", 0, 6);
      check((out = packed(pe32)) && (h = (pe32_head*)out->head));
      pe32_idata* d = (pe32_idata*)(out->head + h->data.off);
      check(h->data.addr == DRVA && h->opt.dir[1].addr == DRVA);
      check(h->opt.dir[12].addr == DRVA + offs(pe32_idata, bind));
      check(!strcmp(d->dll, "kernel32.dll") && !strcmp(d->func, "ExitProcess"));
      check(d->desc[0].name == DRVA + offs(pe32_idata, dll) && d->look[0] == d->bind[0]);
    }
    it("docks once, data after the imports") {
      start(cpu_ia64, CODE, 12, "", 0, 6);
      check(packed(pe32));
      check(docked.calls == 1 && docked.at == DRVA + IDAT - TRVA && docked.org == 0);
    }
    it("puts data and bss in .data") {
      start(cpu_ia64, CODE, 12, "abcd", 4, 6);
      ours.zero = 100;
      check((out = packed(pe32)) && (h = (pe32_head*)out->head));
      check(bytes(out->head + h->data.off + IDAT, "61 62 63 64"));
      check(h->data.vmsz == IDAT + 16 + 100 && h->opt.size == DRVA + 0x1000);
    }
    it("refuses a library, so far") {
      start(cpu_ia64, CODE, 12, "", 0, 6);
      ours.main = NULL;
      check(!packed(pe32) && strstr(said, "a library is not supported"));
    }
  }
}
