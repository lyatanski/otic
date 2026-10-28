// vim: ts=2:sw=2
#include "core.h"

// macOS executable: dyld calls main through LC_MAIN and exits with its result
// through libSystem, so the boot stub (a Linux syscall) is never run. Data and
// bss follow text in __DATA, on a page of their own. arm64 macOS only runs
// signed code: an ad-hoc signature, a SHA-256 of each 4 KB page before it,
// ends the file.

enum { mh_magic = 0xfeedfacf, mh_exec = 2 };
enum { mh_noundefs = 0x1, mh_dyldlink = 0x4, mh_twolevel = 0x80, mh_pie = 0x200000 };
enum {
  lc_symtab = 0x2, lc_dysymtab = 0xb, lc_dylib = 0xc, lc_dylinker = 0xe,
  lc_segment = 0x19, lc_signature = 0x1d, lc_build = 0x32, lc_main = 0x80000028,
};
static const struct { u32i type, sub; } mach_cpu[] = {
  [cpu_ia64] = {0x01000007, 3}, [cpu_arm8] = {0x0100000c, 0},
};

typedef struct {
  u32i cmd, size; char name[16];
  u64i addr, vmsz, off, fsz; u32i maxp, prot, nsec, flags;
} seg64;
typedef struct {
  char name[16], seg[16];
  u64i addr, size; u32i off, align, reloff, nrel, flags, rsvd[3];
} sec64;

// every load command, in file order: none needs padding
typedef struct {
  struct { u32i magic, cpu, sub, type, ncmds, size, flags, rsvd; } mh;
  seg64 zero, text; sec64 code; seg64 data, link;
  struct { u32i cmd, size, symoff, nsyms, stroff, strsize; } syms;
  struct { u32i cmd, size, rest[18]; } dsym;
  struct { u32i cmd, size, name; char path[20]; } dyld;
  struct { u32i cmd, size; u64i entry, stack; } main;
  struct { u32i cmd, size, platform, minos, sdk, ntools; } build;
  struct { u32i cmd, size, name, time, curr, comp; char path[32]; } libs;
  struct { u32i cmd, size, off, fsz; } sign;
} mach_head;

static const char mach_name[] = "tic"; // the signature's identifier

static u32i ror(u32i x, int n) { return x >> n | x << (32 - n); }

static byte* be32(byte* at, u32i v) {
  for(int i = 0; i < 4; i++) *at++ = v >> (24 - 8 * i);
  return at;
}

static void sha256(byte* msg, u64i len, byte* out) {
  static const u32i k[64] = {
    0x428a2f98, 0x71374491, 0xb5c0fbcf, 0xe9b5dba5, 0x3956c25b, 0x59f111f1, 0x923f82a4, 0xab1c5ed5,
    0xd807aa98, 0x12835b01, 0x243185be, 0x550c7dc3, 0x72be5d74, 0x80deb1fe, 0x9bdc06a7, 0xc19bf174,
    0xe49b69c1, 0xefbe4786, 0x0fc19dc6, 0x240ca1cc, 0x2de92c6f, 0x4a7484aa, 0x5cb0a9dc, 0x76f988da,
    0x983e5152, 0xa831c66d, 0xb00327c8, 0xbf597fc7, 0xc6e00bf3, 0xd5a79147, 0x06ca6351, 0x14292967,
    0x27b70a85, 0x2e1b2138, 0x4d2c6dfc, 0x53380d13, 0x650a7354, 0x766a0abb, 0x81c2c92e, 0x92722c85,
    0xa2bfe8a1, 0xa81a664b, 0xc24b8b70, 0xc76c51a3, 0xd192e819, 0xd6990624, 0xf40e3585, 0x106aa070,
    0x19a4c116, 0x1e376c08, 0x2748774c, 0x34b0bcb5, 0x391c0cb3, 0x4ed8aa4a, 0x5b9cca4f, 0x682e6ff3,
    0x748f82ee, 0x78a5636f, 0x84c87814, 0x8cc70208, 0x90befffa, 0xa4506ceb, 0xbef9a3f7, 0xc67178f2,
  };
  u32i h[8] = {
    0x6a09e667, 0xbb67ae85, 0x3c6ef372, 0xa54ff53a, 0x510e527f, 0x9b05688c, 0x1f83d9ab, 0x5be0cd19,
  };
  u64i all = (len + 72) / 64 * 64; // message, 0x80, zeros, 64-bit bit length
  for(u64i at = 0; at < all; at += 64) {
    u32i w[64] = {0}, a[8];
    for(u64i j = at; j < at + 64; j++)
      w[(j - at) / 4] = w[(j - at) / 4] << 8 | (byte)(j < len? msg[j]: j == len? 0x80:
        j >= all - 8? (len * 8) >> (8 * (all - 1 - j)): 0);
    for(int i = 16; i < 64; i++)
      w[i] = w[i-16] + w[i-7] + (ror(w[i-15], 7) ^ ror(w[i-15], 18) ^ w[i-15] >> 3)
                              + (ror(w[i-2], 17) ^ ror(w[i-2], 19) ^ w[i-2] >> 10);
    for(int i = 0; i < 8; i++) a[i] = h[i];
    for(int i = 0; i < 64; i++) {
      u32i t1 = a[7] + (ror(a[4], 6) ^ ror(a[4], 11) ^ ror(a[4], 25))
              + ((a[4] & a[5]) ^ (~a[4] & a[6])) + k[i] + w[i];
      u32i t2 = (ror(a[0], 2) ^ ror(a[0], 13) ^ ror(a[0], 22))
              + ((a[0] & a[1]) ^ (a[0] & a[2]) ^ (a[1] & a[2]));
      for(int j = 7; j > 0; j--) a[j] = a[j-1];
      a[4] += t1;
      a[0] = t1 + t2;
    }
    for(int i = 0; i < 8; i++) h[i] += a[i];
  }
  for(int i = 0; i < 8; i++) out = be32(out, h[i]);
}

file* mach_pack(mode* self) {
  const u64i org = 0x100000000, page = 0x4000; // arm64 pages are 16 KB
  const u64i top = sizeof(mach_head);
  const u64i code = self->text->body - self->text->head;
  const u64i text = (top + code + page - 1) & ~(page - 1); // __TEXT, __DATA, the signature
  const u64i size = self->data->body - self->data->head;
  const u64i vmsz = (((size + 15) & ~15) + self->zero) / page * page + page;
  const u64i link = text + ((size + page - 1) & ~(page - 1));
  const u32i slots = link >> 12;
  const u32i cdsz = 88 + sizeof(mach_name) + 32 * slots;
  const u32i sign = (20 + cdsz + 15) & ~15;
  const u64i all = link + sign;
  dock(self, text - top);

  mach_head h = {
    .mh   = {mh_magic, mach_cpu[self->arch.cpu].type, mach_cpu[self->arch.cpu].sub, mh_exec,
             11, sizeof(h) - sizeof(h.mh), mh_noundefs | mh_dyldlink | mh_twolevel | mh_pie},
    .zero = {lc_segment, sizeof(seg64), "__PAGEZERO", .vmsz = org},
    .text = {lc_segment, sizeof(seg64) + sizeof(sec64), "__TEXT", org, text, 0, text,
             5, 5, 1}, // r-x
    .code = {"__text", "__TEXT", org + top, code, top, 2, .flags = 0x80000400}, // instructions
    .data = {lc_segment, sizeof(seg64), "__DATA", org + text, vmsz, text, size, 3, 3}, // rw-
    .link = {lc_segment, sizeof(seg64), "__LINKEDIT", org + text + vmsz, (sign + page - 1) & ~(page - 1),
             link, sign, 1, 1}, // r--
    .syms = {lc_symtab, sizeof(h.syms), link, 0, link, 0},
    .dsym = {lc_dysymtab, sizeof(h.dsym)},
    .dyld = {lc_dylinker, sizeof(h.dyld), 12, "/usr/lib/dyld"},
    .main = {lc_main, sizeof(h.main), top + (self->main - self->text->head)},
    .build = {lc_build, sizeof(h.build), 1, 0xb0000, 0xb0000}, // macOS 11.0
    .libs = {lc_dylib, sizeof(h.libs), 24, 2, 0x10000, 0x10000, "/usr/lib/libSystem.B.dylib"},
    .sign = {lc_signature, sizeof(h.sign), link, sign},
  };

  file* out = calloc(1, sizeof(file) + all);
  if(!out) fail("out of memory\n");
  out->body = out->head + all;
  out->tail = out->head + all;
  copy(copy(out->head, &h, top), self->text->head, code);
  copy(out->head + text, self->data->head, size);

  // super blob holding one code directory (version 0x20400), big-endian
  u32i blob[] = {
    0xfade0cc0, sign, 1, 0, 20,
    0xfade0c02, cdsz, 0x20400, 0x20002, // adhoc, linker signed
    88 + sizeof(mach_name), 88, 0, slots, link, // hashes, identifier, code limit
    0x2002000c, 0, 0, 0, 0, // SHA-256, 4 KB pages
    0, 0, 0, 0, 0, text, 0, 1, // code limit 64, exec segment base, limit, main binary
  };
  byte* at = out->head + link;
  for(u32i i = 0; i < sizeof(blob) / sizeof(*blob); i++) at = be32(at, blob[i]);
  at = copy(at, (void*)mach_name, sizeof(mach_name));
  for(u32i i = 0; i < slots; i++, at += 32) sha256(out->head + ((u64i)i << 12), 1 << 12, at);
  return out;
}

const form mach = {
  .pack = mach_pack,
};
