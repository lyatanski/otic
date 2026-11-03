// vim: ts=2:sw=2
// elfs' spec: the ELF program it packs, and the relocatable objects it takes,
// which the case builds section by section
#include "form.h"
#include "../elfs.c"
#include "test.h"

// the object being built: its header, the contents, the section headers last
static struct { byte at[4096]; u64i n; sh64 sh[16]; u32i shn; } o;

static void  begin(void) {
  memset(&o, 0, sizeof o);
  o.n = sizeof(elfs_id) + sizeof(eh64);
  o.shn = 1; // the null section
}

// a section of size bytes from src: its index
static u32i  section(u32i type, u64i flags, void* src, u64i size, u64i align) {
  o.n = (o.n + 7) & -8;
  o.sh[o.shn] = (sh64){.type = type, .flags = flags, .offset = o.n, .size = size, .align = align};
  if(type != SHT_NOBITS) { memcpy(o.at + o.n, src, size); o.n += size; }
  return o.shn++;
}

// the symbols s, named in strs: the index of their table
static u32i  symbols(sym64* s, u32i n, char* strs, u32i size) {
  u32i str = section(SHT_STRTAB, 0, strs, size, 1);
  u32i tab = section(SHT_SYMTAB, 0, s, n * sizeof(sym64), 8);
  o.sh[tab].link = str;
  o.sh[tab].entsize = sizeof(sym64);
  return tab;
}

// relocations r of section at, by the symbols of table tab
static void  relocate(rela64* r, u32i n, u32i at, u32i tab) {
  u32i i = section(SHT_RELA, 0, r, n * sizeof(rela64), 8);
  o.sh[i].link = tab;
  o.sh[i].info = at;
  o.sh[i].entsize = sizeof(rela64);
}

// the object for arch, as scan reads a file: NUL ended
static file* finish(u16i arch) {
  o.n = (o.n + 7) & -8;
  elfs_id id = {.seal = {0x7f, 'E', 'L', 'F'}, .bits = eb_cl64, .data = ed_2lsb, .issue = ev_curr};
  eh64 eh = {.type = et_relo, .arch = arch, .issue = ev_curr, .shoff = o.n,
             .ehsize = sizeof(id) + sizeof(eh), .shsize = sizeof(sh64), .shnum = o.shn};
  memcpy(o.at, &id, sizeof id);
  memcpy(o.at + sizeof id, &eh, sizeof eh);
  memcpy(o.at + o.n, o.sh, o.shn * sizeof(sh64));
  o.n += o.shn * sizeof(sh64);
  static file* f;
  free(f);
  f = malloc(sizeof(file) + o.n + 1);
  f->body = f->head;
  f->tail = f->head + o.n;
  memcpy(f->head, o.at, o.n);
  f->head[o.n] = 0;
  return f;
}

// func(): return 42, then a call to ext; var in data, a bss, com common and
// weak a weak alias past func
enum { s_func = 1, s_var, s_ext, s_com, s_weak };
static file* standard(u16i arch) {
  static char strs[] = "\0func\0var\0ext\0com\0weak";
  sym64 s[] = {
    {0},
    {.name = 1,  .bind = STB_GLOBAL, .type = 2, .shidx = 1, .value = 0, .size = 6},
    {.name = 6,  .bind = STB_GLOBAL, .type = 1, .shidx = 2, .value = 4, .size = 4},
    {.name = 10, .bind = STB_GLOBAL, .shidx = SHN_UNDEF},
    {.name = 14, .bind = STB_GLOBAL, .type = 1, .shidx = SHN_COMMON, .value = 32, .size = 64},
    {.name = 18, .bind = STB_WEAK,   .type = 2, .shidx = 1, .value = 6},
  };
  begin();
  u32i text = section(SHT_PROGBITS, SHF_ALLOC | SHF_EXECINSTR,
                      "\xb8\x2a\x00\x00\x00\xc3" "\xe8\x00\x00\x00\x00\xc3", 12, 16);
  section(SHT_PROGBITS, SHF_ALLOC | 1, "\1\2\3\4\5\6\7\x8", 8, 8);
  section(SHT_NOBITS, SHF_ALLOC | 1, NULL, 16, 8);
  u32i tab = symbols(s, sizeof s / sizeof *s, strs, sizeof strs);
  relocate((rela64[]){{.offset = 7, .type = R_X86_64_PLT32, .sym = s_ext, .addend = -4}}, 1, text, tab);
  return finish(arch);
}

// take f: 1 when it was, else 0 and said tells why
static bool  took(file* f) {
  if(setjmp(bail)) return 0;
  elfs.take(&self, f);
  return 1;
}
static bool  refused(file* f, char* why) {
  if(!took(f) && strstr(said, why)) return 1;
  printf("    want %s\n    got  %s\n", why, said[0]? said: "no error\n");
  return 0;
}

#define CODE "\xb8\x07\x00\x00\x00\xc3" // mov eax, 7; ret
#define meant(name) ((meta*)named(name)->data)

// a library of the image start lays out: CODE, then a call at 7; 16 bytes of
// data, the last 8 an address; 32 of bss. It defines lf at 0 in text, lv at
// 4 in data and lz, weak, at 8 in bss
static meta  lf, lv, lz, int4 = {.kind = base, .size = 4};
static void  library(chip cpu, byte libs) {
  start(cpu, "\xb8\x07\x00\x00\x00\xc3" "\xe8\x00\x00\x00\x00", 11,
        "abcdefgh\x10\x00\x00\x00\x00\x00\x00\x00", 16, 0);
  self.main = NULL;
  self.libs = libs;
  self.zero = 32;
  lf = (meta){.name = named("lf"), .kind = call, .addr = self.text->head};
  lv = (meta){.name = named("lv"), .kind = glob, .slot = 4, .type = &int4};
  lz = (meta){.name = named("lz"), .kind = glob, .slot = 8 | ZERO, .weak = 1};
  signs[0] = &lf; signs[1] = &lv; signs[2] = &lz;
  signn = 3;
}
static void  cord_(cord c) { cords[cordn++] = c; }

// the header and sections of an image, symbol i of table tab, its name, and
// relocation i of section sec
#define HEAD(out) ((eh64*)((out)->head + sizeof(elfs_id)))
#define SECT(out) ((sh64*)((out)->head + HEAD(out)->shoff))
static sym64*  sym_(file* out, u32i tab, u32i i) { return (sym64*)(out->head + SECT(out)[tab].offset) + i; }
static char*   name_(file* out, u32i tab, sym64* s) {
  return (char*)out->head + SECT(out)[SECT(out)[tab].link].offset + s->name;
}
static rela64* rel_(file* out, u32i sec, u32i i) { return (rela64*)(out->head + SECT(out)[sec].offset) + i; }
static bool    rela_(rela64* r, u64i offset, u32i type, u32i sym, s64i addend) {
  if(r->offset == offset && r->type == type && r->sym == sym && r->addend == addend) return 1;
  printf("    want %llx %u %u %lld\n    got  %llx %u %u %lld\n", (unsigned long long)offset, type, sym,
         (long long)addend, (unsigned long long)r->offset, r->type, r->sym, (long long)r->addend);
  return 0;
}

spec("elfs") {
  context("program") { // the image of a shared library, entered at the start of text, with a loader
    file* out;
    ph64* ph;
    it("writes a 64-bit little endian image for the target") {
      start(cpu_ia64, CODE, 6, "", 0, 0);
      check((out = packed(elfs)));
      check(bytes(out->head, "7f 45 4c 46 02 01 01"));
      check(HEAD(out)->type == et_dyno && HEAD(out)->arch == em_ia64);
      start(cpu_arm8, CODE, 6, "", 0, 0);
      check((out = packed(elfs)) && HEAD(out)->arch == em_arm8);
    }
    it("enters at its code") {
      start(cpu_ia64, CODE, 6, "", 0, 0);
      check((out = packed(elfs)));
      sh64* sh = SECT(out);
      check(HEAD(out)->entry == sh[5].addr && sh[5].size == 6 && sh[5].addr == sh[5].offset);
      check(bytes(out->head + HEAD(out)->entry, "b8 07 00 00 00 c3"));
    }
    it("docks once, text at its address, data 64 KB further") {
      start(cpu_ia64, CODE, 6, "", 0, 0);
      check((out = packed(elfs)));
      sh64* sh = SECT(out);
      check(docked.calls == 1 && docked.org == sh[5].addr && docked.at == sh[7].addr - sh[5].addr);
      check(sh[7].addr == sh[7].offset + 0x10000);
    }
    it("describes its headers, then names its loader, before any segment") {
      start(cpu_ia64, CODE, 6, "", 0, 0);
      check((out = packed(elfs)) && HEAD(out)->phnum == 6);
      ph = (ph64*)(HEAD(out) + 1);
      check(ph[0].type == PT_PHDR && ph[0].offset == HEAD(out)->phoff && ph[0].filesz == 6 * sizeof(ph64));
      check(ph[0].vaddr == ph[0].offset && ph[0].flags == 4);
      check(ph[1].type == PT_INTERP && ph[1].vaddr == ph[1].offset && ph[1].offset >= ph[0].offset + ph[0].filesz);
      check(!strcmp((char*)out->head + ph[1].offset, "/lib64/ld-linux-x86-64.so.2"));
      check(ph[1].filesz == strlen("/lib64/ld-linux-x86-64.so.2") + 1 && ph[2].type == PT_LOAD);
      check(SECT(out)[1].offset >= ph[1].offset + ph[1].filesz);
      start(cpu_arm8, CODE, 6, "", 0, 0);
      check((out = packed(elfs)));
      ph = (ph64*)(HEAD(out) + 1);
      check(!strcmp((char*)out->head + ph[1].offset, "/lib/ld-linux-aarch64.so.1"));
    }
    it("has no soname") {
      start(cpu_ia64, CODE, 6, "", 0, 0);
      self.outp = "bins/prog";
      check((out = packed(elfs)));
      ph = (ph64*)(HEAD(out) + 1);
      dyn64* d = (dyn64*)(out->head + SECT(out)[6].offset);
      check(ph[4].type == PT_DYNAMIC && ph[4].filesz == 9 * sizeof(dyn64));
      check(d[0].tag == DT_HASH && d[8].tag == DT_NULL);
    }
    it("keeps data and bss in the writable segment") {
      start(cpu_ia64, CODE, 6, "abcd", 4, 0);
      self.zero = 100;
      check((out = packed(elfs)));
      ph = (ph64*)(HEAD(out) + 1);
      sh64* sh = SECT(out);
      check(ph[3].flags == 6 && ph[3].vaddr == sh[6].addr);
      check(bytes(out->head + sh[7].offset, "61 62 63 64") && sh[7].size == 4);
      check(sh[8].type == SHT_NOBITS && sh[8].size == 104); // 100, to 8 bytes
      check(ph[3].memsz == sh[7].offset - ph[3].offset + 16 + 104);
    }
    it("aligns the code as an object asks") {
      start(cpu_ia64, CODE, 6, "", 0, 0);
      self.most = 64;
      check((out = packed(elfs)));
      sh64* sh = SECT(out);
      check(HEAD(out)->entry % 64 == 0 && bytes(out->head + HEAD(out)->entry, "b8 07"));
      check(sh[7].offset % 64 == 0 && docked.at == sh[7].addr - sh[5].addr);
    }
    it("refuses an address in code") {
      start(cpu_ia64, CODE, 6, "", 0, 0);
      cord_((cord){.site = {self.text->head + 1, 0}, .aim = {self.text->head, 0}, .kind = cd_wide});
      check(!packed(elfs) && strstr(said, "an address in code: a program"));
    }
    it("has the loader relocate an address in data") {
      start(cpu_ia64, CODE, 6, "\2\0\0\0\0\0\0\0", 8, 0); // dock puts the address at base 0 there
      cord_((cord){.site = {NULL, 0}, .aim = {self.text->head, 0}, .kind = cd_wide});
      check((out = packed(elfs)) && SECT(out)[4].size == sizeof(rela64));
      check(rela_(rel_(out, 4, 0), SECT(out)[7].addr, R_X86_64_RELATIVE, 0, 2));
    }
  }

  context("take") {
    meta* m;
    hook* h;
    it("puts the code after text, aligned") {
      start(cpu_ia64, "\x90\x90\x90", 3, "", 0, 0);
      check(took(standard(em_ia64)));
      check(self.text->body == self.text->head + 16 + 12);
      check(bytes(self.text->head + 16, "b8 2a 00 00 00 c3 e8 00 00 00 00 c3"));
      check(self.most == 32); // com
    }
    it("lays data, bss and common symbols out through core") {
      start(cpu_ia64, "", 0, "", 0, 0);
      check(took(standard(em_ia64)));
      check(laid == 3 && self.zero == 32 + 64);
      check(bytes(self.data->head, "01 02 03 04 05 06 07 08"));
    }
    it("defines its functions as C code in text") {
      start(cpu_ia64, "\x90\x90\x90", 3, "", 0, 0);
      check(took(standard(em_ia64)));
      check((m = meant("func")) && m->kind == call && m->ext && m->size == VARY);
      check(m->addr == self.text->head + 16 && m->unit == 1 && !m->weak);
      check((m = meant("weak")) && m->weak && m->addr == self.text->head + 16 + 6);
    }
    it("defines its data at their offset, common ones in bss") {
      start(cpu_ia64, "", 0, "", 0, 0);
      check(took(standard(em_ia64)));
      check((m = meant("var")) && m->kind == glob && m->slot == 4);
      check((m = meant("com")) && m->kind == glob && m->weak && (u32i)m->slot == (32 | ZERO));
    }
    it("asks for the archive member of an undefined name") {
      start(cpu_ia64, "", 0, "", 0, 0);
      define("ext", (meta){.kind = pend});
      check(took(standard(em_ia64)));
      check(asked == 1 && asks[0]->name == named("ext"));
    }
    it("asks for nothing a unit defines") {
      start(cpu_ia64, "", 0, "", 0, 0);
      define("ext", (meta){.kind = call});
      check(took(standard(em_ia64)) && asked == 0);
    }
    it("hooks a reference by name for dock") {
      start(cpu_ia64, "", 0, "", 0, 0);
      check(took(standard(em_ia64)));
      check(self.hookn == 1 && (h = self.hooks));
      check(h->site.at == self.text->head + 7 && h->name == named("ext"));
      check(h->add == -4 && !h->wide && !h->weak && h->unit == 1);
    }
    it("leaves a weak definition out when a unit defines the name") {
      start(cpu_ia64, "", 0, "", 0, 0);
      define("weak", (meta){.kind = call});
      check(took(standard(em_ia64)) && !meant("weak"));
    }
    it("defines a name a unit defines weakly") {
      start(cpu_ia64, "", 0, "", 0, 0);
      define("func", (meta){.kind = call, .weak = 1});
      check(took(standard(em_ia64)) && meant("func"));
    }
    it("makes a GOT load the address itself") {
      static char strs[] = "\0ext";
      sym64 s[] = {{0}, {.name = 1, .bind = STB_GLOBAL}};
      start(cpu_ia64, "", 0, "", 0, 0);
      begin();
      u32i text = section(SHT_PROGBITS, SHF_ALLOC | SHF_EXECINSTR, "\x48\x8b\x05\0\0\0\0\xc3", 8, 1);
      u32i tab = symbols(s, 2, strs, sizeof strs);
      relocate((rela64[]){{.offset = 3, .type = R_X86_64_REX_GOTPCRELX, .sym = 1, .addend = -4}}, 1, text, tab);
      check(took(finish(em_ia64)));
      check(bytes(self.text->head, "48 8d 05")); // mov rax, [rip+x] -> lea rax, [rip+x]
      check(self.hookn == 1 && self.hooks->load);
    }
  }

  context("static library") { // sections: text, data, bss, symbols, names, relocations
    file* out;
    it("writes one relocatable object without main, with -s") {
      library(cpu_ia64, lb_relo);
      check((out = packed(elfs)) && HEAD(out)->type == et_relo && HEAD(out)->shnum == 9);
      check(docked.calls == 0 && HEAD(out)->entry == 0 && HEAD(out)->phnum == 0);
      sh64* sh = SECT(out);
      check(bytes(out->head + sh[1].offset, "b8 07 00 00 00 c3 e8") && sh[1].size == 11);
      check(sh[1].flags == (SHF_ALLOC | SHF_EXECINSTR) && sh[2].flags == (SHF_ALLOC | SHF_WRITE));
      check(bytes(out->head + sh[2].offset, "61 62 63 64") && sh[2].size == 16);
      check(sh[3].type == SHT_NOBITS && sh[3].size == 32);
      check(!strcmp((char*)out->head + sh[8].offset + sh[1].name, ".text"));
    }
    it("defines the public names in their sections") {
      library(cpu_ia64, lb_relo);
      check((out = packed(elfs)) && SECT(out)[4].info == 4);
      sym64* s = sym_(out, 4, 1);
      check(s[0].type == STT_SECTION && s[0].shidx == 1 && s[2].shidx == 3);
      check(!strcmp(name_(out, 4, s + 3), "lf") && s[3].type == STT_FUNC && s[3].shidx == 1);
      check(!strcmp(name_(out, 4, s + 4), "lv") && s[4].shidx == 2 && s[4].value == 4 && s[4].size == 4);
      check(s[5].bind == STB_WEAK && s[5].shidx == 3 && s[5].value == 8);
    }
    it("relocates a global by the symbol of its section") {
      library(cpu_ia64, lb_relo);
      cord_((cord){.site = {self.text->head, 0}, .aim = {NULL, 4}, .kind = cd_glob});
      cord_((cord){.site = {self.text->head, 0}, .aim = {NULL, 8 | ZERO}, .kind = cd_glob});
      check((out = packed(elfs)) && SECT(out)[6].size == 2 * sizeof(rela64));
      check(rela_(rel_(out, 6, 0), 3, R_X86_64_PC32, 2, 0));
      check(rela_(rel_(out, 6, 1), 3, R_X86_64_PC32, 3, 4));
    }
    it("patches a distance within text") {
      library(cpu_ia64, lb_relo);
      cord_((cord){.site = {self.text->head + 7, 0}, .aim = {self.text->head, 0}, .add = -4, .kind = cd_rel});
      check((out = packed(elfs)) && SECT(out)[6].size == 0);
      check(bytes(out->head + SECT(out)[1].offset + 7, "f5 ff ff ff"));
    }
    it("calls a name nothing defines through the linker") {
      library(cpu_ia64, lb_relo);
      cord_((cord){.site = {self.text->head + 7, 0}, .name = named("ext"), .add = -4, .kind = cd_rel, .weak = 1});
      check((out = packed(elfs)));
      check(rela_(rel_(out, 6, 0), 7, R_X86_64_PLT32, 7, -4));
      sym64* s = sym_(out, 4, 7);
      check(!strcmp(name_(out, 4, s), "ext") && s->shidx == SHN_UNDEF && s->bind == STB_WEAK);
    }
    it("makes the lea of a GOT load to such a name a load again") {
      library(cpu_ia64, lb_relo);
      copy(self.text->head, "\x48\x8d\x05", 3);
      cord_((cord){.site = {self.text->head + 3, 0}, .name = named("ext"), .add = -4, .kind = cd_rel, .got = 1});
      check((out = packed(elfs)) && bytes(out->head + SECT(out)[1].offset, "48 8b 05"));
      check(rela_(rel_(out, 6, 0), 3, R_X86_64_GOTPCREL, 7, -4));
    }
    it("relocates an address in data") {
      library(cpu_ia64, lb_relo);
      cord_((cord){.site = {NULL, 8}, .aim = {self.text->head + 6, 0}, .add = 2, .kind = cd_wide});
      check((out = packed(elfs)) && SECT(out)[7].size == sizeof(rela64));
      check(rela_(rel_(out, 7, 0), 8, R_X86_64_64, 1, 8));
    }
    it("puts the import slots after data, each relocated by its name") {
      library(cpu_ia64, lb_relo);
      self.imps[0] = &(meta){.name = named("puts"), .kind = call};
      self.impn = 1;
      cord_((cord){.site = {self.text->head, 0}, .aim = {NULL, SLOT}, .kind = cd_glob});
      check((out = packed(elfs)) && SECT(out)[2].size == 24);
      check(rela_(rel_(out, 6, 0), 3, R_X86_64_PC32, 2, 12));
      check(rela_(rel_(out, 7, 0), 16, R_X86_64_64, 7, 0));
      sym64* s = sym_(out, 4, 7);
      check(!strcmp(name_(out, 4, s), "puts") && s->shidx == SHN_UNDEF && s->type == STT_FUNC);
    }
    it("relocates the movz and movk of a global on arm8") {
      library(cpu_arm8, lb_relo);
      cord_((cord){.site = {self.text->head, 0}, .aim = {NULL, 4}, .kind = cd_glob});
      check((out = packed(elfs)) && HEAD(out)->arch == em_arm8);
      check(rela_(rel_(out, 6, 0), 4, R_AARCH64_MOVW_PREL_G0_NC, 2, 8));
      check(rela_(rel_(out, 6, 1), 8, R_AARCH64_MOVW_PREL_G1_NC, 2, 12));
    }
  }

  context("shared library") { // sections: hash, dynsym, dynstr, rela, text, dynamic, data, bss
    file* out;
    ph64* ph;
    it("writes one without main by default") {
      library(cpu_ia64, lb_dyno);
      check((out = packed(elfs)) && HEAD(out)->type == et_dyno && HEAD(out)->entry == 0);
      ph = (ph64*)(HEAD(out) + 1);
      check(HEAD(out)->phnum == 4 && ph[0].vaddr == 0 && ph[2].type == PT_DYNAMIC);
      check(ph[3].type == PT_GNU_STACK && ph[3].flags == 6); // no executable stack
      sh64* sh = SECT(out);
      check(bytes(out->head + sh[5].offset, "b8 07 00 00 00 c3 e8") && sh[5].addr == sh[5].offset);
      check(ph[1].vaddr == sh[6].addr && sh[7].addr == sh[7].offset + 0x10000);
    }
    it("docks once, text at its address, data 64 KB further") {
      library(cpu_ia64, lb_dyno);
      check((out = packed(elfs)));
      sh64* sh = SECT(out);
      check(docked.calls == 1 && docked.org == sh[5].addr && docked.at == sh[7].addr - sh[5].addr);
    }
    it("exports the public names, its globals protected") {
      library(cpu_ia64, lb_dyno);
      check((out = packed(elfs)) && SECT(out)[2].info == 1);
      sh64*  sh = SECT(out);
      sym64* s = sym_(out, 2, 1);
      check(!strcmp(name_(out, 2, s), "lf") && s[0].value == sh[5].addr && s[0].other == 0);
      check(s[1].value == sh[7].addr + 4 && s[1].shidx == 7 && s[1].other == STV_PROTECTED);
      check(s[2].value == sh[8].addr + 8 && s[2].shidx == 8 && s[2].bind == STB_WEAK);
    }
    it("finds every symbol through one hash bucket") {
      library(cpu_ia64, lb_dyno);
      check((out = packed(elfs)));
      u32i* h = (u32i*)(out->head + SECT(out)[1].offset);
      check(h[0] == 1 && h[1] == 4 && h[2] == 1);
      check(h[3] == 0 && h[4] == 2 && h[5] == 3 && h[6] == 0);
    }
    it("names itself after its file") {
      library(cpu_ia64, lb_dyno);
      self.outp = "bins/libq.so";
      check((out = packed(elfs)));
      dyn64* d = (dyn64*)(out->head + SECT(out)[6].offset);
      check(d[0].tag == DT_SONAME && !strcmp((char*)out->head + SECT(out)[3].offset + d[0].val, "libq.so"));
    }
    it("relocates the addresses in data by the load address") {
      library(cpu_ia64, lb_dyno);
      cord_((cord){.site = {NULL, 8}, .aim = {self.text->head, 0}, .kind = cd_wide});
      cord_((cord){.site = {NULL, 0}, .name = named("gone"), .kind = cd_wide, .weak = 1}); // stays 0
      check((out = packed(elfs)) && SECT(out)[4].size == sizeof(rela64));
      check(rela_(rel_(out, 4, 0), SECT(out)[7].addr + 8, R_X86_64_RELATIVE, 0, 0x10));
    }
    it("binds the imports through their slots, after bss") {
      library(cpu_arm8, lb_dyno);
      self.imps[0] = &(meta){.name = named("puts"), .kind = call};
      self.impn = 1;
      check((out = packed(elfs)) && SECT(out)[8].size == 32 + 8);
      check(rela_(rel_(out, 4, 0), SECT(out)[8].addr + 32, R_AARCH64_GLOB_DAT, 4, 0));
      check(sym_(out, 2, 4)->shidx == SHN_UNDEF);
    }
    it("refuses an address in code") {
      library(cpu_ia64, lb_dyno);
      cord_((cord){.site = {self.text->head + 7, 0}, .aim = {self.text->head, 0}, .kind = cd_wide});
      check(!packed(elfs) && strstr(said, "an address in code"));
    }
  }

  context("take refuses") {
    it("what is not an object") {
      start(cpu_ia64, "", 0, "", 0, 0);
      file* f = finish(em_ia64);
      f->head[0] = 0;
      check(refused(f, "obj.o: not a 64-bit object"));
    }
    it("an object for another target") {
      start(cpu_ia64, "", 0, "", 0, 0);
      check(refused(standard(em_arm8), "an object for another target"));
    }
    it("arm8 objects, so far") {
      start(cpu_arm8, "", 0, "", 0, 0);
      check(refused(standard(em_arm8), "only x86-64 objects so far"));
    }
    it("a name defined twice") {
      start(cpu_ia64, "", 0, "", 0, 0);
      define("func", (meta){.kind = call});
      check(refused(standard(em_ia64), "'func' is defined twice"));
    }
    it("a name declared as an import before it") {
      start(cpu_ia64, "", 0, "", 0, 0);
      define("var", (meta){.kind = glob, .lib = &(meta){0}});
      check(refused(standard(em_ia64), "'var' is declared before the object"));
    }
    it("thread-local storage") {
      start(cpu_ia64, "", 0, "", 0, 0);
      begin();
      section(SHT_PROGBITS, SHF_ALLOC | SHF_TLS, "\0\0\0\0", 4, 4);
      check(refused(finish(em_ia64), "thread-local storage"));
    }
  }
}
