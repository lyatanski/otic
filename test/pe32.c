// vim: ts=2:sw=2
// pe32's spec: the PE32+ executable it packs, exiting through kernel32, or
// started by msvcrt when it has imports
#include "form.h"
#include "../pe32.c"
#include "test.h"

#define CODE "\xb8\x07\x00\x00\x00\xc3" "\xb8\x2a\x00\x00\x00\xc3" // f, main
#define HEAD 0x200  // the headers, a file alignment
#define TRVA 0x1000 // text
#define DRVA 0x2000 // the imports, then data
// the import table of kernel32!ExitProcess alone: two directory entries, a
// lookup table of two, the hint and name, the DLL's name, to 16 bytes
#define IDAT ((2 * 20 + 2 * 8 + 14 + 13 + 15) & -16)

// the directory entry i of the image, and the name its lookup entry k points to
static pe32_desc* desc_(file* out, u32i i) {
  pe32_head* h = (pe32_head*)out->head;
  return (pe32_desc*)(out->head + h->data.off + h->opt.dir[1].addr - DRVA) + i;
}
static char*  look_(file* out, const pe32_desc* d, u32i k) {
  pe32_head* h = (pe32_head*)out->head;
  u64i at = ((u64i*)(out->head + h->data.off + d->look - DRVA))[k];
  return at? (char*)out->head + h->data.off + at - DRVA + 2: NULL;
}
static char*  dll_(file* out, const pe32_desc* d) {
  return (char*)out->head + ((pe32_head*)out->head)->data.off + d->name - DRVA;
}

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
      check((u32i)(TRVA + 16 + 17 + iat) == h->opt.dir[12].addr);
    }
    it("calls main with bl on arm8, the slot through adrp and ldr") {
      start(cpu_arm8, CODE, 12, "", 0, 8);
      check((out = packed(pe32)) && (h = (pe32_head*)out->head));
      // bl main (0x1008 - 0x1010) >> 2; adrp x16, 0x2000; ldr x16, [x16, iat]; blr x16
      check(bytes(out->head + HEAD + 16, "fe ff ff 97 10 00 00 b0"));
      u32i ldr = 0xf9400210 | (h->opt.dir[12].addr & 0xfff) >> 3 << 10;
      check(!memcmp(out->head + HEAD + 24, &ldr, 4) && bytes(out->head + HEAD + 28, "00 02 3f d6"));
    }
    it("imports ExitProcess from kernel32.dll, its slot after data") {
      start(cpu_ia64, CODE, 12, "", 0, 6);
      check((out = packed(pe32)) && (h = (pe32_head*)out->head));
      pe32_desc* d = desc_(out, 0);
      check(h->data.addr == DRVA && h->opt.dir[1].addr == DRVA && h->opt.dir[1].size == 20);
      check(h->opt.dir[12].addr == DRVA + IDAT && h->opt.dir[12].size == 8);
      check(!strcmp(dll_(out, d), "kernel32.dll") && !strcmp(look_(out, d, 0), "ExitProcess"));
      check(!look_(out, d, 1) && d->bind == DRVA + IDAT && !d[1].look && !d[1].bind);
    }
    it("docks once, data after the imports") {
      start(cpu_ia64, CODE, 12, "", 0, 6);
      check(packed(pe32));
      check(docked.calls == 1 && docked.at == DRVA + IDAT - TRVA && docked.org == 0);
    }
    it("puts data, bss and the slots in .data") {
      start(cpu_ia64, CODE, 12, "abcd", 4, 6);
      ours.zero = 100;
      check((out = packed(pe32)) && (h = (pe32_head*)out->head));
      check(bytes(out->head + h->data.off + IDAT, "61 62 63 64"));
      check(h->opt.dir[12].addr == DRVA + IDAT + 16 + 104); // bss to 8 bytes
      check(h->data.vmsz == IDAT + 16 + 104 + 8 && h->opt.size == DRVA + 0x1000);
    }
    it("aligns data as an object asks") {
      start(cpu_ia64, CODE, 12, "abcd", 4, 6);
      ours.most = 256;
      check((out = packed(pe32)) && (h = (pe32_head*)out->head));
      check(docked.at == DRVA + 256 - TRVA && bytes(out->head + h->data.off + 256, "61 62 63 64"));
    }
    it("imports by runs of one DLL, then msvcrt's start, the slots in their order") {
      start(cpu_ia64, CODE, 12, "", 0, 6);
      meta c = {.name = named("\"c\"")}, z = {.name = named("\"zlib1\"")}, k = {.name = named("\"k.drv\"")};
      ours.imps[0] = &(meta){.name = named("puts"), .kind = call, .lib = &c};
      ours.imps[1] = &(meta){.name = named("crc32"), .kind = call, .lib = &z};
      ours.imps[2] = &(meta){.name = named("kf"), .kind = call, .lib = &k};
      ours.imps[3] = &(meta){.name = named("printf"), .kind = call, .lib = &c};
      ours.impn = 4;
      check((out = packed(pe32)) && (h = (pe32_head*)out->head));
      check(h->opt.dir[1].size == 4 * 20 && h->opt.dir[12].size == 6 * 8);
      u32i iat = h->opt.dir[12].addr;
      pe32_desc* d = desc_(out, 0);
      check(!strcmp(dll_(out, d), "msvcrt.dll") && d->bind == iat && !strcmp(look_(out, d, 0), "puts"));
      check(!look_(out, d, 1));
      check(!strcmp(dll_(out, d + 1), "zlib1.dll") && d[1].bind == iat + 8 && !strcmp(look_(out, d + 1, 0), "crc32"));
      check(!strcmp(dll_(out, d + 2), "k.drv") && d[2].bind == iat + 16 && !strcmp(look_(out, d + 2, 0), "kf"));
      check(!strcmp(dll_(out, d + 3), "msvcrt.dll") && d[3].bind == iat + 24);
      check(!strcmp(look_(out, d + 3, 0), "printf") && !strcmp(look_(out, d + 3, 1), "__getmainargs"));
      check(!strcmp(look_(out, d + 3, 2), "exit") && !look_(out, d + 3, 3) && !d[4].look);
    }
    it("starts main through msvcrt with imports: argc, argv, then exit") {
      start(cpu_ia64, CODE, 12, "", 0, 6);
      ours.imps[0] = &(meta){.name = named("puts"), .kind = call, .lib = &(meta){.name = named("\"c\"")}};
      ours.impn = 1;
      check((out = packed(pe32)) && (h = (pe32_head*)out->head));
      u32i iat = h->opt.dir[12].addr;
      s32i rel;
      check(bytes(out->head + HEAD + 16, "48 83 ec 48 48 8d 4c 24 28 48 8d 54 24 30 4c 8d 44 24 38"));
      check(bytes(out->head + HEAD + 16 + 40, "ff 15") && bytes(out->head + HEAD + 16 + 46, "8b 7c 24 28"));
      memcpy(&rel, out->head + HEAD + 16 + 42, 4);
      check((u32i)(TRVA + 16 + 46 + rel) == iat + 8); // __getmainargs, after puts
      memcpy(&rel, out->head + HEAD + 16 + 56, 4);
      check((u32i)(TRVA + 16 + 60 + rel) == TRVA + 6); // main
      memcpy(&rel, out->head + HEAD + 16 + 64, 4);
      check((u32i)(TRVA + 16 + 68 + rel) == iat + 16); // exit
      start(cpu_arm8, CODE, 12, "", 0, 8);
      ours.imps[0] = &(meta){.name = named("puts"), .kind = call, .lib = &(meta){.name = named("\"c\"")}};
      ours.impn = 1;
      check((out = packed(pe32)) && (h = (pe32_head*)out->head));
      iat = h->opt.dir[12].addr;
      u32i ldr = 0xf9400210 | ((iat + 8) & 0xfff) >> 3 << 10;
      check(bytes(out->head + HEAD + 16, "ff 03 01 d1") && !memcmp(out->head + HEAD + 16 + 32, &ldr, 4));
      check(bytes(out->head + HEAD + 16 + 48, "f2 ff ff 97")); // bl main: (0x1008 - 0x1040) >> 2
    }
    it("refuses a library, so far") {
      start(cpu_ia64, CODE, 12, "", 0, 6);
      ours.main = NULL;
      check(!packed(pe32) && strstr(said, "a library is not supported"));
    }
  }
}
