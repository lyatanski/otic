// vim: ts=2:sw=2
#include "core.h"

// Windows executable (PE32+): Windows has no stable syscalls, so a stub after
// the code exits through kernel32's ExitProcess, the only import:
// exit(main()). The image has no absolute addresses and needs no relocations.
// The writable section holds the import table, then data and bss.

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

// import table for kernel32.dll!ExitProcess
typedef struct {
  struct { u32i look, time, chain, name, bind; } desc[2];
  u64i look[2], bind[2]; // lookup and address tables: by name, then 0
  u16i hint; char func[14];
  char dll[16];
} pe32_idata;

// exit(main()): its size, or with a buffer, written at rva
static u32i pe32_boot(byte* at, chip cpu, u32i rva, u32i main, u32i iat) {
  if(cpu == cpu_ia64) {
    s32i call = main - (rva + 9), load = iat - (rva + 17);
    if(!at) return 17;
    at = copy(at, (byte[]){0x48, 0x83, 0xec, 0x28, 0xe8}, 5); // sub rsp, 40 (shadow space); call main
    at = copy(at, &call, 4);
    at = copy(at, (byte[]){0x89, 0xc1, 0xff, 0x15}, 4);       // mov ecx, eax; call [rip + iat]
    copy(at, &load, 4);
    return 17;
  }
  u32i code[] = {
    0x94000000 | ((main - rva) >> 2 & 0x3ffffff),            // bl main
    0x58000010 | ((iat - (rva + 4)) >> 2 & 0x7ffff) << 5,    // ldr x16, iat
    0xd63f0200,                                              // blr x16
  };
  if(at) copy(at, code, sizeof(code));
  return sizeof(code);
}

file* pe32_pack(mode* self) {
  const u64i org = 0x140000000;
  const u32i salign = 0x1000, falign = 0x200;
#define up(n, align) (((n) + (align) - 1) & ~((align) - 1))
  const chip cpu = self->arch.cpu;
  const u32i code = self->text->body - self->text->head;
  const u32i boot = up(code, 16); // the stub after the code
  const u32i text = boot + pe32_boot(NULL, cpu, 0, 0, 0);
  const u32i head = up(sizeof(pe32_head), falign);
  const u32i trva = salign, drva = trva + up(text, salign); // section addresses
  const u32i toff = head, doff = toff + up(text, falign);   // and file offsets
  const u32i iat = drva + offs(pe32_idata, bind);
  const u32i idat = up(sizeof(pe32_idata), 16);             // data after the imports
  const u32i size = self->data->body - self->data->head;
  const u32i fsz = up(idat + size, falign), vmsz = idat + up(size, 16) + self->zero;
  const u32i all = doff + fsz;
  dock(self, drva + idat - trva, 0);

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
      .dir    = {[1] = {drva, sizeof(((pe32_idata*)0)->desc)},  // imports
                 [12] = {iat, sizeof(((pe32_idata*)0)->bind)}}, // import addresses
    },
    .text = {".text", text, trva, up(text, falign), toff,
             .flags = 0x60000020}, // code, r-x
    .data = {".data", vmsz, drva, fsz, doff,
             .flags = 0xc0000040}, // initialized data, rw-
  };
  const u32i name = drva + offs(pe32_idata, hint);
  pe32_idata d = {
    .desc = {{drva + offs(pe32_idata, look), 0, 0, drva + offs(pe32_idata, dll), iat}},
    .look = {name}, .bind = {name}, // the loader overwrites bind
    .func = "ExitProcess",
    .dll  = "kernel32.dll",
  };
#undef up

  file* out = calloc(1, sizeof(file) + all);
  if(!out) fail("out of memory\n");
  out->body = out->head + all;
  out->tail = out->head + all;
  copy(out->head, &h, sizeof(h));
  copy(out->head + toff, self->text->head, code);
  pe32_boot(out->head + toff + boot, cpu, trva + boot, trva + (self->main - self->text->head), iat);
  copy(out->head + doff, &d, sizeof(d));
  copy(out->head + doff + idat, self->data->head, size);
  return out;
}

const form pe32 = {
  .pack = pe32_pack,
  .os   = os_win,
};
