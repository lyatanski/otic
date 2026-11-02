// vim: ts=2:sw=2
// elfs' spec: the ELF executable it packs, and the relocatable objects it
// takes, which the case builds section by section
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

spec("elfs") {
  context("pack") {
    elfs_id* id;
    eh64*    eh;
    ph64*    ph;
    file*    out;
    it("writes a 64-bit little endian executable for the target") {
      start(cpu_ia64, CODE, 6, "", 0, 0);
      check((out = packed(elfs)));
      id = (elfs_id*)out->head; eh = (eh64*)(id + 1);
      check(bytes(out->head, "7f 45 4c 46 02 01 01"));
      check(eh->type == et_exec && eh->arch == em_ia64);
      start(cpu_arm8, CODE, 6, "", 0, 0);
      check((out = packed(elfs)));
      check(((eh64*)(out->head + sizeof(elfs_id)))->arch == em_arm8);
    }
    it("loads the code right after its headers, entering at it") {
      start(cpu_ia64, CODE, 6, "", 0, 0);
      check((out = packed(elfs)));
      eh = (eh64*)(out->head + sizeof(elfs_id)); ph = (ph64*)(eh + 1);
      u64i top = sizeof(elfs_id) + sizeof(eh64) + sizeof(ph64);
      check(eh->phnum == 1 && ph->type == 1 && ph->flags == 5 && ph->offset == 0);
      check(eh->entry == 0x400000 + top && ph->filesz == top + 6);
      check(bytes(out->head + top, "b8 07 00 00 00 c3"));
      check((u64i)(out->body - out->head) == top + 6);
    }
    it("docks once, data 64 KB past the aligned code") {
      start(cpu_ia64, CODE, 6, "", 0, 0);
      check(packed(elfs));
      u64i top = sizeof(elfs_id) + sizeof(eh64) + sizeof(ph64);
      check(docked.calls == 1 && docked.at == 16 + 0x10000 && docked.org == 0x400000 + top);
    }
    it("adds a writable segment for data and bss") {
      start(cpu_ia64, CODE, 6, "abcd", 4, 0);
      self.zero = 100;
      check((out = packed(elfs)));
      eh = (eh64*)(out->head + sizeof(elfs_id)); ph = (ph64*)(eh + 1);
      u64i top = sizeof(elfs_id) + sizeof(eh64) + 2 * sizeof(ph64);
      check(eh->phnum == 2 && ph[1].flags == 6);
      check(ph[1].offset == top + 16 && ph[1].vaddr == 0x400000 + top + 16 + 0x10000);
      check(ph[1].filesz == 4 && ph[1].memsz == 16 + 100);
      check(bytes(out->head + top + 16, "61 62 63 64"));
    }
    it("adds it for bss alone") {
      start(cpu_ia64, CODE, 6, "", 0, 0);
      self.zero = 8;
      check((out = packed(elfs)));
      eh = (eh64*)(out->head + sizeof(elfs_id)); ph = (ph64*)(eh + 1);
      check(eh->phnum == 2 && ph[1].filesz == 0 && ph[1].memsz == 8);
    }
    it("aligns the code as an object asks") {
      start(cpu_ia64, CODE, 6, "", 0, 0);
      self.most = 64;
      check((out = packed(elfs)));
      eh = (eh64*)(out->head + sizeof(elfs_id));
      check(eh->entry == 0x400000 + 128 && bytes(out->head + 128, "b8 07"));
      check(docked.at == 64 + 0x10000);
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
