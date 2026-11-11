// vim: ts=2:sw=2
#include "core.h"

// Windows executable (PE32+): Windows has no stable syscalls, so a stub after
// the code exits through a DLL. Without imports, kernel32's ExitProcess:
// exit(main()). With them, msvcrt starts it as a C program: __getmainargs
// gives main its argc and argv, and msvcrt's exit flushes stdio. The image has
// no absolute addresses and needs no relocations. The writable section holds
// the import table, the import slots, then data and bss: the slots are the
// import address table, in the file, each holding its lookup entry until the
// loader binds it (Windows skips a table whose first entry is 0).

static const struct { u16i arch, os; } pe32_cpu[] = {
  [cpu_ia64] = {0x8664, 6}, [cpu_arm8] = {0xaa64, 10}, // Vista, Windows 10
};

// byte offset of a member: its address in a struct placed at 0
#define offs(type, member) ((u64i)&((type*)0)->member)

typedef struct {
  char name[8];
  u32i vmsz, addr, fsz, off, reloff, lineoff;
  u16i nrel, nline;
  u32i flags;
} pe32_sect;

// DOS header without a DOS stub, then the PE headers: none needs padding
typedef struct {
  struct { u16i magic; byte rest[58]; u32i next; } dos;
  u32i seal;
  struct { u16i arch, nsec; u32i time, symoff, nsym; u16i opsize, flags; } coff;
  struct {
    u16i magic; byte major, minor;
    u32i code, data, zero, entry, base;
    u64i org;
    u32i salign, falign;
    u16i os[2], image[2], sub[2];
    u32i win32, size, head, sum;
    u16i system, dll;
    u64i stack, stuck, heap, heaped;
    u32i loader, ndir;
    struct { u32i addr, size; } dir[16];
  } opt;
  pe32_sect text, data;
} pe32_head;

// an import directory entry: its lookup table, its DLL's name, its first slot
typedef struct { u32i look, time, chain, name, bind; } pe32_desc;

// an import of the image: its name and the file of its DLL
typedef struct { const char* name; u32i size; const char* dll; u32i dlln; } pe32_imp;

// the DLL file of library l of a load: msvcrt.dll for c and m, as written
// when it has a dot, else with .dll after it. Into out when given: its length
static u32i pe32_dll(const meta* l, char* out) {
  const char* s = l->name->name + 1; // without the quotes
  u32i n = (u32i)l->name->size - 2;
  bool dot = 0;
  for(u32i i = 0; i < n; i++) dot |= s[i] == '.';
  if(n == 1 && (*s == 'c' || *s == 'm')) s = "msvcrt.dll", n = 10, dot = 1;
  if(out) copy(copy((byte*)out, s, n), ".dll", dot? 0: 4);
  return dot? n: n + 4;
}

// arm64: bl to, adrp x16 and ldr x16 of the slot at to, from pc
static u32i pe32_bl(u32i pc, u32i to) { return 0x94000000 | ((to - pc) >> 2 & 0x3ffffff); }
static u32i pe32_adrp(u32i pc, u32i to) {
  u32i page = (to >> 12) - (pc >> 12);
  return 0x90000010 | (page & 3) << 29 | (page >> 2 & 0x7ffff) << 5;
}
static u32i pe32_ldr(u32i to) { return 0xf9400210 | (to & 0xfff) >> 3 << 10; }

// the stub: exit(main()) through ExitProcess, its slot at iat, or with crt
// through msvcrt, the slots of __getmainargs and exit at iat and iat + 8. Its
// size, or with a buffer, written at rva
static u32i pe32_boot(byte* at, chip cpu, u32i rva, u32i main, u32i iat, bool crt) {
  if(cpu == cpu_ia64 && !crt) {
    s32i call = main - (rva + 9), load = iat - (rva + 17);
    if(!at) return 17;
    at = copy(at, (byte[]){0x48, 0x83, 0xec, 0x28, 0xe8}, 5); // sub rsp, 40 (shadow space); call main
    at = copy(at, &call, 4);
    at = copy(at, (byte[]){0x89, 0xc1, 0xff, 0x15}, 4);       // mov ecx, eax; call [rip + iat]
    copy(at, &load, 4);
    return 17;
  }
  if(cpu == cpu_ia64) { // argc at rsp+40, argv, envp, the startup info at rsp+64
    static const byte code[] = {
      0x48, 0x83, 0xec, 0x48,                         // sub rsp, 72
      0x48, 0x8d, 0x4c, 0x24, 0x28,                   // lea rcx, [rsp+40]
      0x48, 0x8d, 0x54, 0x24, 0x30,                   // lea rdx, [rsp+48]
      0x4c, 0x8d, 0x44, 0x24, 0x38,                   // lea r8, [rsp+56]
      0x45, 0x31, 0xc9,                               // xor r9d, r9d: no wildcards
      0xc7, 0x44, 0x24, 0x40, 0, 0, 0, 0,             // mov dword [rsp+64], 0
      0x48, 0x8d, 0x44, 0x24, 0x40,                   // lea rax, [rsp+64]
      0x48, 0x89, 0x44, 0x24, 0x20,                   // mov [rsp+32], rax: the fifth argument
      0xff, 0x15, 0, 0, 0, 0,                         // call [rip + __getmainargs]
      0x8b, 0x7c, 0x24, 0x28,                         // mov edi, [rsp+40]
      0x48, 0x8b, 0x74, 0x24, 0x30,                   // mov rsi, [rsp+48]
      0xe8, 0, 0, 0, 0,                               // call main
      0x89, 0xc1,                                     // mov ecx, eax
      0xff, 0x15, 0, 0, 0, 0,                         // call [rip + exit]
    };
    if(!at) return sizeof(code);
    s32i args = iat - (rva + 46), call = main - (rva + 60), quit = iat + 8 - (rva + 68);
    copy(at, code, sizeof(code));
    copy(at + 42, &args, 4);
    copy(at + 56, &call, 4);
    copy(at + 64, &quit, 4);
    return sizeof(code);
  }
  // arm64: a slot through adrp and ldr, as far as data goes
  if(!crt) {
    u32i code[] = {pe32_bl(rva, main), pe32_adrp(rva + 4, iat), pe32_ldr(iat), 0xd63f0200}; // blr x16
    if(at) copy(at, code, sizeof(code));
    return sizeof(code);
  }
  u32i code[] = { // argc at sp, argv, envp, the startup info at sp+24
    0xd10103ff,                                      // sub sp, sp, #64
    0x910003e0, 0x910023e1, 0x910043e2,              // add x0, sp, #0; x1 #8; x2 #16
    0x52800003,                                      // mov w3, #0: no wildcards
    0xb9001bff, 0x910063e4,                          // str wzr, [sp, #24]; add x4, sp, #24
    pe32_adrp(rva + 28, iat), pe32_ldr(iat), 0xd63f0200,         // blr x16: __getmainargs
    0xb94003e0, 0xf94007e1,                          // ldr w0, [sp]; ldr x1, [sp, #8]
    pe32_bl(rva + 48, main),
    pe32_adrp(rva + 52, iat + 8), pe32_ldr(iat + 8), 0xd63f0200, // blr x16: exit
  };
  if(at) copy(at, code, sizeof(code));
  return sizeof(code);
}

file* pe32_pack(mode* self) {
  if(!self->main) fail("a library is not supported in this form yet\n");
  const u64i org = 0x140000000;
  const u32i salign = 0x1000, falign = 0x200;
#define up(n, align) ((u32i)((n) + (align) - 1) & ~((align) - 1))
  const chip cpu = self->arch.cpu;
  const bool crt = self->impn != 0;

  // the imports, the stub's last: a directory entry for each run of them
  // from one DLL, each with its lookup table, then the names they point to
  const u32i nimp = self->impn + (crt? 2: 1);
  pe32_imp* imp = self->heap(NULL, nimp * sizeof(pe32_imp));
  for(u32i i = 0; i < self->impn; i++) {
    const meta* x = self->imps[i];
    char* dll = self->heap(NULL, pe32_dll(x->lib, NULL) + 1);
    imp[i] = (pe32_imp){x->name->name, (u32i)x->name->size, dll, pe32_dll(x->lib, dll)};
  }
  if(crt) {
    imp[nimp - 2] = (pe32_imp){"__getmainargs", 13, "msvcrt.dll", 10};
    imp[nimp - 1] = (pe32_imp){"exit", 4, "msvcrt.dll", 10};
  }
  else imp[0] = (pe32_imp){"ExitProcess", 11, "kernel32.dll", 12};
  u32i ndesc = 0, hints = 0, dlls = 0;
  for(u32i i = 0; i < nimp; i++) {
    hints += up(2 + imp[i].size + 1, 2);
    if(i && imp[i].dlln == imp[i - 1].dlln && alike(imp[i].dll, imp[i - 1].dll, imp[i].dlln)) continue;
    ndesc++;
    dlls += imp[i].dlln + 1;
  }
  const u32i look = (ndesc + 1) * sizeof(pe32_desc), hint = look + (nimp + ndesc) * 8;
  const u32i step = (u32i)self->core.gap(self);
  const u32i slot = up(hint + hints + dlls, 8);    // the slots after the import table
  const u32i idat = up(slot + 8 * nimp, step);     // then data

  const u32i code = (u32i)(self->text->body - self->text->head);
  const u32i boot = up(code, 16); // the stub after the code
  const u32i text = boot + pe32_boot(NULL, cpu, 0, 0, 0, crt);
  const u32i head = up(sizeof(pe32_head), falign);
  const u32i trva = salign, drva = trva + up(text, salign); // section addresses
  const u32i toff = head, doff = toff + up(text, falign);   // and file offsets
  const u32i size = (u32i)(self->data->body - self->data->head);
  const u32i iat  = drva + slot;
  const u32i fsz = up(idat + size, falign), vmsz = idat + up(size, step) + (u32i)self->zero;
  const u32i all = doff + fsz;
  self->fore = idat - slot; // dock places the slots there
  self->core.dock(self, drva + idat - trva, 0);

  pe32_head h = {
    .dos  = {.magic = 0x5a4d, .next = offs(pe32_head, seal)}, // "MZ"
    .seal = 0x4550, // "PE\0\0"
    .coff = {pe32_cpu[cpu].arch, 2, .opsize = sizeof(h.opt),
             .flags = 0x22}, // executable, large address aware
    .opt  = {
      .magic  = 0x20b, // PE32+
      .code   = up(text, falign),
      .data   = fsz,
      .entry  = trva + boot,
      .base   = trva,
      .org    = org,
      .salign = salign,
      .falign = falign,
      .os     = {pe32_cpu[cpu].os},
      .sub    = {pe32_cpu[cpu].os},
      .size   = drva + up(vmsz, salign),
      .head   = head,
      .system = 3,      // console
      .dll    = 0x8160, // terminal server aware, NX, dynamic base, high entropy VA
      .stack  = 0x100000, .stuck = 0x1000, .heap = 0x100000, .heaped = 0x1000,
      .ndir   = 16,
      .dir    = {[1] = {drva, ndesc * sizeof(pe32_desc)}, // imports
                 [12] = {iat, 8 * nimp}},                 // import addresses
    },
    .text = {".text", text, trva, up(text, falign), toff,
             .flags = 0x60000020}, // code, r-x
    .data = {".data", vmsz, drva, fsz, doff,
             .flags = 0xc0000040}, // initialized data, rw-
  };
#undef up

  file* out = zero(self, sizeof(file) + all);
  out->body = out->head + all;
  out->tail = out->head + all;
  copy(out->head, &h, sizeof(h));
  copy(out->head + toff, self->text->head, code);
  pe32_boot(out->head + toff + boot, cpu, trva + boot, trva + (u32i)(self->main - self->text->head),
            iat + 8 * self->impn, crt);
  byte* o = out->head + doff;
  pe32_desc* d = (pe32_desc*)o - 1;
  u64i* l = (u64i*)(o + look);
  u32i  at = hint, dll = hint + hints;
  for(u32i i = 0; i < nimp; i++) {
    if(!i || imp[i].dlln != imp[i - 1].dlln || !alike(imp[i].dll, imp[i - 1].dll, imp[i].dlln)) {
      if(i) l++; // the lookup table of the run before ends with 0
      *++d = (pe32_desc){drva + (u32i)((byte*)l - o), 0, 0, drva + dll, iat + 8 * i};
      copy(o + dll, imp[i].dll, imp[i].dlln);
      dll += imp[i].dlln + 1;
    }
    ((u64i*)(o + slot))[i] = *l++ = drva + at; // by name: its hint (0), then the name
    copy(o + at + 2, imp[i].name, imp[i].size);
    at += (2 + imp[i].size + 1 + 1) & ~1u;
  }
  copy(o + idat, self->data->head, size);
  for(u32i i = 0; i < self->impn; i++) self->heap((void*)imp[i].dll, 0);
  self->heap(imp, 0);
  return out;
}

const form pe32 = {
  .pack = pe32_pack,
  .os   = os_win,
};
