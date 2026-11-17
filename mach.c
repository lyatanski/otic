// vim: ts=2:sw=2
#include "core.h"

// macOS executable: dyld calls main through LC_MAIN and exits with its result
// through libSystem, so the boot stub (a Linux syscall) is never run. Data and
// bss follow text in __DATA, on a page of their own. arm64 macOS only runs
// signed code: an ad-hoc signature, a SHA-256 of each 4 KB page before it,
// ends the file. Without main, a library: a dylib, or with -s an object.

enum { mh_magic = 0xfeedfacf, mh_object = 1, mh_exec = 2, mh_dylib = 6 };
enum { mh_noundefs = 0x1, mh_dyldlink = 0x4, mh_twolevel = 0x80, mh_noreexport = 0x100000,
       mh_pie = 0x200000 };
enum {
  lc_symtab = 0x2, lc_dysymtab = 0xb, lc_dylib = 0xc, lc_id_dylib = 0xd, lc_dylinker = 0xe,
  lc_segment = 0x19, lc_signature = 0x1d, lc_build = 0x32, lc_dyld_info = 0x80000022,
  lc_main = 0x80000028,
};
enum { s_regular, s_zerofill = 0x1, s_code = 0x80000400 }; // section flags: data, bss, instructions
// symbol types and flags: external, defined in a section, a weak reference or
// definition, and a name dyld looks up in every image (flat)
enum { n_ext = 0x1, n_sect = 0xe, n_weak_ref = 0x40, n_weak_def = 0x80, n_flat = 0xfe00 };
enum { x64_unsigned, x64_signed }; // relocation types
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

typedef struct { u32i magic, cpu, sub, type, ncmds, size, flags, rsvd; } mach_mh;
typedef struct { u32i cmd, size, symoff, nsyms, stroff, strsize; } mach_symtab;
typedef struct { u32i cmd, size, ilocal, nlocal, iext, next, iund, nund, rest[12]; } mach_dysym;
typedef struct { u32i cmd, size, platform, minos, sdk, ntools; } mach_build;
typedef struct { u32i cmd, size, off, fsz; } mach_blob;
typedef struct { u32i cmd, size, name, time, curr, comp; char path[32]; } mach_dylib;
typedef struct { u32i cmd, size, rebase, nrebase, bind, nbind, weak, nweak, lazy, nlazy, trie, ntrie; } mach_info;
typedef struct { u32i strx; byte type, sect; u16i desc; u64i value; } nl64;
typedef struct { s32i addr; u32i info; } mrel; // info: symbol, pc-relative, length, extern, type

// the load commands of an executable, in file order: none needs padding.
// With imports, the binds' (LC_DYLD_INFO_ONLY) and a dylib per library
// besides libSystem follow, then the signature's
typedef struct {
  mach_mh mh;
  seg64 zero, text; sec64 code; seg64 data; sec64 vars[2]; seg64 link;
  mach_symtab syms;
  mach_dysym dsym;
  struct { u32i cmd, size, name; char path[20]; } dyld;
  struct { u32i cmd, size; u64i entry, stack; } main;
  mach_build build;
  mach_dylib libs;
} mach_head;

static const char mach_name[] = "otic"; // the signature's identifier

static u32i ror(u32i x, int n) { return x >> n | x << (32 - n); }

static byte* be32(byte* at, u32i v) {
  for(int i = 0; i < 4; i++) *at++ = (byte)(v >> (24 - 8 * i));
  return at;
}

static void sha256(const byte* msg, u64i len, byte* out) {
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

// the bytes of an ad-hoc signature of the image's first limit bytes
static u32i  mach_sigsz(u64i limit) {
  return (20 + 88 + sizeof(mach_name) + 32 * ((limit + 4095) >> 12) + 15) & ~15;
}

// sign the image at o, its first limit bytes, the signature at limit: text
// the size of __TEXT, exe for an executable
static void  mach_seal(byte* o, u64i limit, u64i text, bool exe) {
  const u32i slots = (u32i)((limit + 4095) >> 12);
  const u32i cdsz = 88 + sizeof(mach_name) + 32 * slots;
  // super blob holding one code directory (version 0x20400), big-endian
  u32i blob[] = {
    0xfade0cc0, mach_sigsz(limit), 1, 0, 20,
    0xfade0c02, cdsz, 0x20400, 0x20002, // adhoc, linker signed
    88 + sizeof(mach_name), 88, 0, slots, (u32i)limit, // hashes, identifier, code limit
    0x2002000c, 0, 0, 0, 0, // SHA-256, 4 KB pages
    0, 0, 0, 0, 0, (u32i)text, 0, exe, // code limit 64, exec segment base, limit, main binary
  };
  byte* at = o + limit;
  for(u32i i = 0; i < sizeof(blob) / sizeof(*blob); i++) at = be32(at, blob[i]);
  at = copy(at, mach_name, sizeof(mach_name));
  for(u64i i = 0; i < slots; i++, at += 32) // the last page up to limit
    sha256(o + (i << 12), limit - (i << 12) < 4096? limit - (i << 12): 4096, at);
}

static byte* uleb(byte* at, u64i v) {
  do { *at++ = (v & 0x7f) | (v > 0x7f? 0x80: 0); v >>= 7; } while(v);
  return at;
}

static u32i  mach_log(u64i step) { // log2 of an alignment
  u32i k = 0;
  while(((u64i)1 << k) < step) k++;
  return k;
}

// the dylib of library l of a load, into out when given: its length, 0 for c
// and m, which libSystem has. As written when it has a slash or a dot, else
// lib<name>.dylib, which dyld looks for in its fallback paths
static u32i  mach_lib(const meta* l, char* out) {
  const char* s = l->name->name + 1; // without the quotes
  u32i n = (u32i)l->name->size - 2;
  bool as_is = 0;
  if(n == 1 && (*s == 'c' || *s == 'm')) return 0;
  for(u32i i = 0; i < n; i++) as_is |= s[i] == '.' || s[i] == '/';
  if(as_is) { if(out) copy((byte*)out, s, n); return n; }
  if(out) copy(copy(copy((byte*)out, "lib", 3), s, n), ".dylib", 6);
  return n + 9;
}

// LC_UUID with the 16 bytes of uuid, at c: past it
static byte* mach_uuid_cmd(byte* c, const byte* uuid) {
  const u32i head[2] = {0x1b, 24};
  return copy(copy(c, head, sizeof head), uuid, 16);
}

// the segments and sections of an image, for its debug bundle: __TEXT at org,
// its section at org + top, the bytes of __TEXT, then __DATA with the data
// (size bytes), bss from bss to end, and vmsz in all
typedef struct { u64i org, top, code, text, size, bss, end, vmsz; u32i lg; } mach_geo;

// an identifier for the image, which its debug bundle repeats: from its text and data
static void  mach_uuid(const mode* self, byte* out) {
  byte a[32], b[32];
  sha256(self->text->head, self->text->body - self->text->head, a);
  sha256(self->data->head, self->data->body - self->data->head, b);
  for(u32i i = 0; i < 16; i++) out[i] = a[i] ^ b[i + 16];
}

// -g: the debug bundle of the image, beside it as <out>.dSYM, which lldb reads:
// a file of type MH_DSYM with the segments of the image holding no bytes, so
// that its addresses mean the same, the same UUID, and a __DWARF segment with
// the sections. The frames are left to the debugger: it unwinds by the frame
// pointer
static void  mach_dsym(mode* self, const mach_geo* g, const byte* uuid) {
  static const char names[4][16] = {"__debug_info", "__debug_abbrev", "__debug_line", "__debug_aranges"};
  const u64i page = 0x4000;
  dwarf dw;
  self->core.dbug(self, g->org + g->top, g->org + g->text, g->org + g->text + g->bss, &dw);
  const blob* blobs[4] = {&dw.info, &dw.abbr, &dw.line, &dw.ranges};
  const u64i top = sizeof(mach_mh) + 24 + sizeof(mach_symtab) + 3 * sizeof(seg64) + 7 * sizeof(sec64);
  const u64i off = (top + page - 1) & ~(page - 1), vm = g->org + g->text + g->vmsz;
  u64i  len = 0;
  for(u32i i = 0; i < 4; i++) len += blobs[i]->n;
  const u64i all = off + len;

  seg64 seg[3] = {
    {lc_segment, sizeof(seg64) + sizeof(sec64), "__TEXT", g->org, g->text, 0, 0, 5, 5, 1, 0},
    {lc_segment, sizeof(seg64) + 2 * sizeof(sec64), "__DATA", g->org + g->text, g->vmsz, 0, 0, 3, 3, 2, 0},
    {lc_segment, sizeof(seg64) + 4 * sizeof(sec64), "__DWARF", vm, (len + page - 1) & ~(page - 1), off, len, 7, 3, 4, 0},
  };
  sec64 sec[7] = {
    {"__text", "__TEXT", g->org + g->top, g->code, 0, g->lg, .flags = (u32i)s_code},
    {"__data", "__DATA", g->org + g->text, g->size, 0, g->lg, .flags = s_regular},
    {"__bss", "__DATA", g->org + g->text + g->bss, g->end - g->bss, 0, g->lg, .flags = s_zerofill},
  };
  for(u32i i = 0, at = 0; i < 4; at += blobs[i++]->n) {
    sec[3 + i] = (sec64){.addr = vm + at, .size = blobs[i]->n, .off = (u32i)(off + at), .flags = 0x02000000}; // S_ATTR_DEBUG
    copy((byte*)sec[3 + i].name, names[i], 16);
    copy((byte*)sec[3 + i].seg, "__DWARF", 8);
  }
  mach_symtab st = {lc_symtab, sizeof st, (u32i)off, 0, (u32i)off, 0};
  mach_mh mh = {(u32i)mh_magic, mach_cpu[self->arch.cpu].type, mach_cpu[self->arch.cpu].sub, 10, 5,
                (u32i)(top - sizeof(mach_mh)), 0, 0}; // MH_DSYM

  file* out = zero(self, sizeof(file) + all);
  byte* o = out->head;
  out->body = out->tail = o + all;
  byte* c = copy(mach_uuid_cmd(copy(o, &mh, sizeof mh), uuid), &st, sizeof st);
  c = copy(copy(c, seg, sizeof(seg64)), sec, sizeof(sec64));
  c = copy(copy(c, seg + 1, sizeof(seg64)), sec + 1, 2 * sizeof(sec64));
  copy(copy(c, seg + 2, sizeof(seg64)), sec + 3, 4 * sizeof(sec64));
  for(u32i i = 0, at = 0; i < 4; at += blobs[i++]->n) copy(o + off + at, blobs[i]->p, blobs[i]->n);
  self->heap(dw.info.p, 0);
  self->heap(dw.abbr.p, 0);
  self->heap(dw.line.p, 0);
  self->heap(dw.frame.p, 0);
  self->heap(dw.ranges.p, 0);

  const char* name = self->outp;
  for(const char* n = name; *n; n++) if(*n == '/') name = n + 1;
  static const char plist[] =
    "<?xml version=\"1.0\" encoding=\"UTF-8\"?>\n"
    "<!DOCTYPE plist PUBLIC \"-//Apple//DTD PLIST 1.0//EN\" \"http://www.apple.com/DTDs/PropertyList-1.0.dtd\">\n"
    "<plist version=\"1.0\">\n\t<dict>\n"
    "\t\t<key>CFBundleDevelopmentRegion</key>\n\t\t<string>English</string>\n"
    "\t\t<key>CFBundleIdentifier</key>\n\t\t<string>com.apple.xcode.dsym.%s</string>\n"
    "\t\t<key>CFBundleInfoDictionaryVersion</key>\n\t\t<string>6.0</string>\n"
    "\t\t<key>CFBundlePackageType</key>\n\t\t<string>dSYM</string>\n"
    "\t\t<key>CFBundleSignature</key>\n\t\t<string>?\?\?\?</string>\n"
    "\t\t<key>CFBundleShortVersionString</key>\n\t\t<string>1.0</string>\n"
    "\t\t<key>CFBundleVersion</key>\n\t\t<string>1</string>\n"
    "\t</dict>\n</plist>\n";
  const u64i n = slen(self->outp) + slen(name) + 48, m = sizeof plist + slen(name);
  char* path = self->heap(NULL, n), *info = self->heap(NULL, n);
  snprintf(path, n, "%s.dSYM/Contents/Resources/DWARF/%s", self->outp, name);
  snprintf(info, n, "%s.dSYM/Contents/Info.plist", self->outp);
  file* list = zero(self, sizeof(file) + m);
  list->body = list->head + snprintf((char*)list->head, m, plist, name);
  list->tail = list->head + m;
  self->side[0] = (sidecar){path, out};
  self->side[1] = (sidecar){info, list};
  self->sides = 2;
}

// an executable: its imports are bound by dyld through their slots, after
// bss in __DATA, each to the library of its load (its ordinal, libSystem
// first), the binds in __LINKEDIT before the signature
static file* mach_exec(mode* self) {
  const u64i org = 0x100000000, page = 0x4000; // arm64 pages are 16 KB
  const u64i step = self->core.gap(self);
  const meta** need = self->heap(NULL, (self->impn + 1) * sizeof(meta*)); // each library once
  u32i* ord = self->heap(NULL, (self->impn + 1) * sizeof(u32i));
  u32i  nneed = 0;
  u64i  dylibs = 0, binds = 2;
  for(u32i i = 0; i < self->impn; i++) {
    const meta* l = self->imps[i]->lib;
    u32i n = mach_lib(l, NULL), k = 0;
    binds += 28 + self->imps[i]->name->size;
    ord[i] = 1;
    if(!n) continue;
    while(k < nneed && need[k] != l) k++;
    if(k == nneed) { need[nneed++] = l; dylibs += (24 + n + 1 + 7) & -8; }
    ord[i] = 2 + k;
  }
  const u64i info = self->impn? sizeof(mach_info): 0;
  const u64i top  = sizeof(mach_head) + info + dylibs + sizeof(mach_blob) + (self->debug? 24: 0);
  const u64i code = self->text->body - self->text->head;
  const u64i text = (top + code + page - 1) & ~(page - 1); // __TEXT, __DATA, the signature
  const u64i size = self->data->body - self->data->head;
  const u64i bss  = (size + step - 1) & ~(step - 1), slots = bss + ((self->zero + 7) & -8);
  const u64i end  = slots + 8 * self->impn;
  const u64i vmsz = ((end? end: 1) + page - 1) & ~(page - 1);
  const u64i link = text + ((size + page - 1) & ~(page - 1));
  const u32i lg = mach_log(step);
  self->core.dock(self, text - top, 0);
  byte uuid[16];
  if(self->debug) {
    mach_uuid(self, uuid);
    mach_dsym(self, &(mach_geo){org, top, code, text, size, bss, end, vmsz, lg}, uuid);
  }

  // the binds: each slot to its name in the library of its ordinal
  byte* bind = self->heap(NULL, binds), *b = bind;
  if(self->impn) {
    *b++ = 0x51; // type: pointer
    for(u32i i = 0; i < self->impn; i++) {
      const item* s = self->imps[i]->name;
      if(ord[i] < 16) *b++ = 0x10 | (byte)ord[i]; // dylib: its ordinal
      else { *b++ = 0x20; b = uleb(b, ord[i]); }
      *b++ = 0x40; // its name, then __DATA and the slot's offset there
      *b++ = '_';
      b = copy(b, s->name, s->size);
      *b++ = 0;
      *b++ = 0x72;
      b = uleb(b, slots + 8 * i);
      *b++ = 0x90; // bind
    }
    *b++ = 0;
  }
  const u64i nbind = b - bind, sig = (link + nbind + 15) & -16;
  const u32i sign = mach_sigsz(sig);
  const u64i all = sig + sign;

  mach_head h = {
    .mh   = {(u32i)mh_magic, mach_cpu[self->arch.cpu].type, mach_cpu[self->arch.cpu].sub, mh_exec,
             11 + (info? 1: 0) + nneed + (self->debug? 1: 0), (u32i)(top - sizeof(h.mh)),
             (self->impn? 0: mh_noundefs) | mh_dyldlink | mh_twolevel | mh_pie, 0},
    .zero = {lc_segment, sizeof(seg64), "__PAGEZERO", .vmsz = org},
    .text = {lc_segment, sizeof(seg64) + sizeof(sec64), "__TEXT", org, text, 0, text,
             5, 5, 1, 0}, // r-x
    .code = {"__text", "__TEXT", org + top, code, (u32i)top, 2, .flags = (u32i)s_code},
    .data = {lc_segment, sizeof(seg64) + 2 * sizeof(sec64), "__DATA", org + text, vmsz, text, size,
             3, 3, 2, 0}, // rw-
    .vars = {{"__data", "__DATA", org + text, size, (u32i)text, lg, .flags = s_regular},
             {"__bss", "__DATA", org + text + bss, end - bss, 0, lg, .flags = s_zerofill}},
    .link = {lc_segment, sizeof(seg64), "__LINKEDIT", org + text + vmsz, (all - link + page - 1) & ~(page - 1),
             link, all - link, 1, 1, 0, 0}, // r--
    .syms = {lc_symtab, sizeof(h.syms), (u32i)link, 0, (u32i)link, 0},
    .dsym = {lc_dysymtab, sizeof(h.dsym), .ilocal = 0},
    .dyld = {lc_dylinker, sizeof(h.dyld), 12, "/usr/lib/dyld"},
    .main = {(u32i)lc_main, sizeof(h.main), top + (self->main - self->text->head), 0},
    .build = {lc_build, sizeof(h.build), 1, 0xb0000, 0xb0000, 0}, // macOS 11.0
    .libs = {lc_dylib, sizeof(h.libs), 24, 2, 0x10000, 0x10000, "/usr/lib/libSystem.B.dylib"},
  };
  mach_info  binfo = {(u32i)lc_dyld_info, sizeof binfo, .bind = (u32i)link, .nbind = (u32i)nbind};
  mach_blob  seal  = {lc_signature, sizeof seal, (u32i)sig, sign};

  file* out = zero(self, sizeof(file) + all);
  byte* o = out->head;
  out->body = out->tail = o + all;
  byte* c = copy(o, &h, sizeof h);
  if(info) c = copy(c, &binfo, sizeof binfo);
  for(u32i k = 0; k < nneed; k++) { // the path after the command, NUL padded
    u32i n = mach_lib(need[k], NULL), cmd = (24 + n + 1 + 7) & -8;
    copy(c, &(mach_dylib){lc_dylib, cmd, 24, 2, 0x10000, 0x10000, ""}, 24);
    mach_lib(need[k], (char*)c + 24);
    c += cmd;
  }
  if(self->debug) c = mach_uuid_cmd(c, uuid);
  copy(copy(c, &seal, sizeof seal), self->text->head, code);
  copy(o + text, self->data->head, size);
  copy(o + link, bind, nbind);
  mach_seal(o, sig, text, 1);
  self->heap(bind, 0);
  self->heap(ord, 0);
  self->heap(need, 0);
  return out;
}

// a name a library leaves undefined: weak when every reference to it is
typedef struct { item* name; bool weak; } mach_und;

// the symbols of a library: its public definitions, then the names it leaves
// undefined, each once (the imports first, in slot order), and the bytes of
// their names, each with C's underscore before it and a NUL after it
typedef struct {
  meta**    defs;
  mach_und* unds;
  u32i      ndef, nund, cund;
  dex       undx; // the undefined ones, by spelling
  u64i      strs;
} mach_syms;

// the index of name among the undefined ones, added when new
// whether undefined name i of the syms ctx[0] is spelled as the item ctx[1]
static bool  mach_same(const void* ctx, u32i i) {
  const mach_syms* t = ((const void**)ctx)[0];
  const item* name = ((const void**)ctx)[1];
  return t->unds[i].name->size == name->size && alike(t->unds[i].name->name, name->name, name->size);
}

static u32i  mach_need(const mode* self, mach_syms* t, item* name, bool weak) {
  const void* ctx[] = {t, name};
  u32i* at = dsee(self, &t->undx, clue(name->name, name->size), mach_same, ctx);
  if(*at) { t->unds[*at - 1].weak &= weak; return *at - 1; }
  *at = t->nund + 1;
  t->unds = swell(self, t->unds, t->nund, &t->cund, sizeof(mach_und), 16);
  t->unds[t->nund++] = (mach_und){name, weak};
  t->strs += name->size + 2;
  return t->nund - 1;
}

// the definitions, and the imports as undefined names
static void  mach_sign(mode* self, mach_syms* t) {
  *t = (mach_syms){.strs = 1};
  t->ndef = self->core.sign(self, &t->defs);
  for(u32i i = 0; i < t->ndef; i++) t->strs += t->defs[i]->name->size + 2;
  for(u32i i = 0; i < self->impn; i++) mach_need(self, t, self->imps[i]->name, 0);
}

// sym, the symbols of t, their names in str after a NUL: a definition in
// section 1 (text), 2 (data) or 3 (bss) at its address there, an undefined
// name with the flags und
static void  mach_put(const mode* self, const mach_syms* t, nl64* sym, byte* str,
                      u64i text, u64i data, u64i bss, u16i und) {
  u64i at = 1;
  str[0] = 0;
  for(u32i i = 0; i < t->ndef + t->nund; i++, sym++) {
    const item* s = i < t->ndef? t->defs[i]->name: t->unds[i - t->ndef].name;
    *sym = (nl64){.strx = (u32i)at};
    str[at++] = '_';
    copy(str + at, s->name, s->size);
    at += s->size;
    str[at++] = 0;
    if(i >= t->ndef) {
      sym->type = n_ext;
      sym->desc = und | (t->unds[i - t->ndef].weak? n_weak_ref: 0);
      continue;
    }
    const meta* m = t->defs[i];
    bool  f = m->kind == call, z = !f && m->slot & ZERO;
    sym->type  = n_sect | n_ext;
    sym->sect  = f? 1: z? 3: 2;
    sym->desc  = m->weak? n_weak_def: 0;
    sym->value = f? text + ((m->hail? m->hail: m->addr) - self->text->head): (z? bss: data) + (m->slot & ~ZERO);
  }
}

// the section of seat s in an object, 1 text, 2 data (then the import slots,
// at slots) or 3 bss, and its address there: data at data, bss at bss
static u32i  mach_part(const mode* self, seat s, u64i data, u64i slots, u64i bss, u64i* addr) {
  if(s.at) { *addr = s.at - self->text->head; return 1; }
  if(s.off & ZERO) { *addr = bss + (s.off & ~ZERO); return 3; }
  *addr = data + (s.off & SLOT? slots + 8 * (s.off & ~SLOT): s.off);
  return 2;
}

// a static library: the image as one x86-64 object, its sections text, data
// (then the import slots) and bss at their addresses in one segment, laid out
// in the file as in memory. The form takes no objects, so every reference is
// a global of the source: a lea in text, relocated to its section or to the
// name nothing defines, and the slots are relocated by their names. arm64
// objects have no relocation for its globals (movz and movk) in this form
static file* mach_relo(mode* self) {
  if(self->arch.cpu != cpu_ia64) fail("an arm64 object is not supported in this form yet\n");
  const u64i step = self->core.gap(self);
  const u64i code = self->text->body - self->text->head;
  const u64i size = self->data->body - self->data->head;
  const u64i slots = (size + 7) & -8, dsz = slots + 8 * self->impn;
  const u64i data = (code + step - 1) & ~(step - 1), bss = (data + dsz + step - 1) & ~(step - 1);
  mach_syms t;
  mach_sign(self, &t);
  cord* cords;
  u32i  ncord = self->core.knit(self, &cords);
  for(cord* c = cords; c < cords + ncord; c++)
    if(c->name) mach_need(self, &t, c->name, c->weak);
  const u32i nsym = t.ndef + t.nund;

  const u64i top  = sizeof(mach_mh) + sizeof(seg64) + 3 * sizeof(sec64) + sizeof(mach_build)
                  + sizeof(mach_symtab) + sizeof(mach_dysym);
  const u64i base = (top + step - 1) & ~(step - 1); // text, then data, as in memory
  const u64i rels = (base + data + dsz + 3) & -4, rdat = rels + ncord * sizeof(mrel);
  const u64i syms = (rdat + self->impn * sizeof(mrel) + 7) & -8, strs = syms + nsym * sizeof(nl64);
  const u64i all  = strs + t.strs;
  const u32i lg   = mach_log(step);
  struct {
    mach_mh mh; seg64 seg; sec64 sec[3]; mach_build build; mach_symtab syms; mach_dysym dsym;
  } h = {
    .mh    = {(u32i)mh_magic, mach_cpu[self->arch.cpu].type, mach_cpu[self->arch.cpu].sub, mh_object,
              4, (u32i)(top - sizeof(mach_mh)), 0, 0},
    .seg   = {lc_segment, sizeof(seg64) + 3 * sizeof(sec64), "", 0, bss + self->zero, base,
              data + dsz, 7, 7, 3, 0},
    .sec   = {{"__text", "__TEXT", 0, code, (u32i)base, lg, (u32i)rels, ncord, (u32i)s_code, {0}},
              {"__data", "__DATA", data, dsz, (u32i)(base + data), lg, (u32i)rdat, self->impn, 0, {0}},
              {"__bss", "__DATA", bss, self->zero, 0, lg, 0, 0, s_zerofill, {0}}},
    .build = {lc_build, sizeof(mach_build), 1, 0xb0000, 0xb0000, 0}, // macOS 11.0
    .syms  = {lc_symtab, sizeof(mach_symtab), (u32i)syms, nsym, (u32i)strs, (u32i)t.strs},
    .dsym  = {lc_dysymtab, sizeof(mach_dysym), .next = t.ndef, .iund = t.ndef, .nund = t.nund},
  };

  file* out = zero(self, sizeof(file) + all);
  byte* o = out->head;
  out->body = out->tail = o + all;
  copy(o, &h, sizeof h);
  copy(o + base, self->text->head, code);
  copy(o + base + data, self->data->head, size);
  mrel* r = (mrel*)(o + rels);
  for(cord* c = cords; c < cords + ncord; c++, r++) { // lea rax, [rip+x]: x at 3
    u64i at = c->site.at - self->text->head + 3, to;
    s32i v = 0;
    if(c->name) // extern: the name, the distance from the lea's end
      *r = (mrel){(s32i)at, (t.ndef + mach_need(self, &t, c->name, c->weak)) | 1 << 24 | 2 << 25 | 1 << 27
                      | x64_signed << 28};
    else { // its section: the distance there, which the linker moves with it
      u32i sec = mach_part(self, c->aim, data, slots, bss, &to);
      v = (s32i)(to - (at + 4));
      *r = (mrel){(s32i)at, sec | 1 << 24 | 2 << 25 | x64_signed << 28};
    }
    copy(o + base + at, &v, sizeof(v));
  }
  for(u32i i = 0; i < self->impn; i++, r++) // a slot: the address of its name
    *r = (mrel){(s32i)(slots + 8 * i), (t.ndef + i) | 3 << 25 | 1 << 27 | x64_unsigned << 28};
  mach_put(self, &t, (nl64*)(o + syms), o + strs, 0, data, bss, 0);
  self->heap(cords, 0);
  self->heap(t.defs, 0);
  self->heap(t.unds, 0);
  self->heap(t.undx.slot, 0);
  return out;
}

// a before b, NUL ended names, by their bytes
static bool  mach_less(const byte* a, const byte* b) {
  for(; *a && *a == *b; a++, b++);
  return *a < *b;
}

// the export trie of a dylib at at: the root with an edge to each of the n
// symbols by its whole name, each to a node with its address. dyld follows the
// first edge that begins the name it looks for, so a name comes before those
// that are its prefixes: by name, last first. Its end
static byte* mach_trie(const mode* self, byte* at, const nl64* sym, u32i n, byte* str) {
  u32i* by = self->heap(NULL, n * sizeof(u32i) + 1);
  u64i  node = 1;
  for(u32i i = 0; i < n; i++) {
    u32i j = i;
    for(; j && mach_less(str + sym[by[j - 1]].strx, str + sym[i].strx); j--)
      by[j] = by[j - 1];
    by[j] = i;
    node += slen((char*)str + sym[i].strx) + 1 + 4;
  }
  byte k[10];
  node += uleb(k, n) - k;
  *at++ = 0; // no symbol at the root
  at = uleb(at, n);
  for(u32i i = 0; i < n; i++) { // the edges, their nodes' offsets in 4 bytes
    const char* s = (const char*)str + sym[by[i]].strx;
    at = copy(at, s, slen(s) + 1);
    for(u32i b = 0; b < 4; b++) *at++ = (node >> 7 * b & 0x7f) | (b < 3? 0x80: 0);
    node += 3 + (uleb(k, sym[by[i]].value) - k);
  }
  for(u32i i = 0; i < n; i++) { // its size, flags (weak), address, no edges
    const nl64* m = sym + by[i];
    *at++ = (byte)(1 + (uleb(k, m->value) - k));
    *at++ = m->desc & n_weak_def? 0x4: 0;
    at = uleb(at, m->value);
    *at++ = 0;
  }
  self->heap(by, 0);
  return at;
}

// a shared library, position independent: __TEXT from address 0 with the
// headers and text, __DATA with data, bss and the import slots, __LINKEDIT with
// the binds of the slots, the export trie, the symbols and the signature. An
// import is bound to whichever loaded image defines it, as on Linux: the
// program links its libraries. Its install name is @rpath/ and its file name
static file* mach_dyno(mode* self) {
  const u64i page = 0x4000, step = self->core.gap(self);
  const u64i code = self->text->body - self->text->head;
  const u64i size = self->data->body - self->data->head;
  mach_syms t;
  mach_sign(self, &t);
  const u32i nsym = t.ndef + t.nund;
  const char* name = self->outp? self->outp: "";
  for(const char* c = name; *c; c++) if(*c == '/') name = c + 1;
  const u64i idsz = (24 + 7 + slen(name) + 1 + 7) & -8;

  const u64i top  = sizeof(mach_mh) + 3 * sizeof(seg64) + 3 * sizeof(sec64) + sizeof(mach_info)
                  + sizeof(mach_symtab) + sizeof(mach_dysym) + idsz + sizeof(mach_dylib)
                  + sizeof(mach_build) + sizeof(mach_blob) + (self->debug? 24: 0);
  const u64i text = (top + code + page - 1) & ~(page - 1); // __DATA in the file and in memory
  const u64i bss  = (size + step - 1) & ~(step - 1), slots = bss + ((self->zero + 7) & -8);
  const u64i end  = slots + 8 * self->impn;
  const u64i vmsz = ((end? end: 1) + page - 1) & ~(page - 1);
  const u64i link = text + ((size + page - 1) & ~(page - 1));
  self->core.dock(self, text - top, top);
  byte uuid[16];

  // __LINKEDIT: binds, trie, symbols and their names, made before it is laid out
  nl64* sym = self->heap(NULL, nsym * sizeof(nl64) + 1);
  byte* str = self->heap(NULL, t.strs);
  mach_put(self, &t, sym, str, top, text, text + bss, n_flat);
  byte* bind = self->heap(NULL, 3 + t.strs + 12 * self->impn), *b = bind;
  if(self->impn) {
    *b++ = 0x3e; // dylib: flat lookup
    *b++ = 0x51; // type: pointer
    for(u32i i = 0; i < self->impn; i++) {
      *b++ = 0x40; // its name, then __DATA and the slot's offset there
      const char* s = (const char*)str + sym[t.ndef + i].strx;
      b = copy(b, s, slen(s) + 1);
      *b++ = 0x71;
      b = uleb(b, slots + 8 * i);
      *b++ = 0x90; // bind
    }
    *b++ = 0;
  }
  byte* trie = self->heap(NULL, 16 + t.strs + 20 * t.ndef);
  const u64i nbind = b - bind, ntrie = mach_trie(self, trie, sym, t.ndef, str) - trie;
  const u64i at   = link + ((nbind + 7) & -8);
  const u64i syms = at + ((ntrie + 7) & -8), strs = syms + nsym * sizeof(nl64);
  const u64i sig  = (strs + t.strs + 15) & -16, all = sig + mach_sigsz(sig);

  const u32i lg = mach_log(step);
  if(self->debug) {
    mach_uuid(self, uuid);
    mach_dsym(self, &(mach_geo){0, top, code, text, size, bss, end, vmsz, lg}, uuid);
  }
  seg64 seg[3] = {
    {lc_segment, sizeof(seg64) + sizeof(sec64), "__TEXT", 0, text, 0, text, 5, 5, 1, 0}, // r-x
    {lc_segment, sizeof(seg64) + 2 * sizeof(sec64), "__DATA", text, vmsz, text, size, 3, 3, 2, 0},
    {lc_segment, sizeof(seg64), "__LINKEDIT", text + vmsz, (all - link + page - 1) & ~(page - 1), link,
     all - link, 1, 1, 0, 0},
  };
  sec64 sec[3] = {
    {"__text", "__TEXT", top, code, (u32i)top, lg, .flags = (u32i)s_code},
    {"__data", "__DATA", text, size, (u32i)text, lg, .flags = 0},
    {"__bss", "__DATA", text + bss, end - bss, 0, lg, .flags = s_zerofill},
  };
  mach_info  info = {(u32i)lc_dyld_info, sizeof info, 0, 0, nbind? (u32i)link: 0, (u32i)nbind, 0, 0, 0, 0,
                     (u32i)at, (u32i)ntrie};
  mach_symtab st  = {lc_symtab, sizeof st, (u32i)syms, nsym, (u32i)strs, (u32i)t.strs};
  mach_dysym dsym = {lc_dysymtab, sizeof dsym, .next = t.ndef, .iund = t.ndef, .nund = t.nund};
  mach_dylib id   = {lc_id_dylib, (u32i)idsz, 24, 1, 0x10000, 0x10000, .path = ""};
  mach_dylib libs = {lc_dylib, sizeof libs, 24, 2, 0x10000, 0x10000,
                     "/usr/lib/libSystem.B.dylib"};
  mach_build bld  = {lc_build, sizeof bld, 1, 0xb0000, 0xb0000, 0}; // macOS 11.0
  mach_blob  sign = {lc_signature, sizeof sign, (u32i)sig, (u32i)(all - sig)};
  mach_mh    mh   = {(u32i)mh_magic, mach_cpu[self->arch.cpu].type, mach_cpu[self->arch.cpu].sub, mh_dylib,
                     10 + (self->debug? 1: 0), (u32i)(top - sizeof(mach_mh)),
                     mh_dyldlink | mh_twolevel | mh_noreexport | (self->impn? 0: mh_noundefs), 0};

  file* out = zero(self, sizeof(file) + all);
  byte* o = out->head;
  out->body = out->tail = o + all;
  byte* c = copy(o, &mh, sizeof mh);
  c = copy(copy(c, seg, sizeof(seg64)), sec, sizeof(sec64));
  c = copy(copy(c, seg + 1, sizeof(seg64)), sec + 1, 2 * sizeof(sec64));
  c = copy(c, seg + 2, sizeof(seg64));
  c = copy(copy(copy(c, &info, sizeof info), &st, sizeof st), &dsym, sizeof dsym);
  copy(copy(c, &id, 24), "@rpath/", 7);
  copy(c + 24 + 7, name, slen(name));
  c += idsz;
  c = copy(copy(c, &libs, sizeof libs), &bld, sizeof bld);
  if(self->debug) c = mach_uuid_cmd(c, uuid);
  c = copy(c, &sign, sizeof sign);
  copy(o + top, self->text->head, code);
  copy(o + text, self->data->head, size);
  copy(o + link, bind, nbind);
  copy(o + at, trie, ntrie);
  copy(o + syms, sym, nsym * sizeof(nl64));
  copy(o + strs, str, t.strs);
  mach_seal(o, sig, text, 0);
  self->heap(sym, 0);
  self->heap(str, 0);
  self->heap(bind, 0);
  self->heap(trie, 0);
  self->heap(t.defs, 0);
  self->heap(t.unds, 0);
  self->heap(t.undx.slot, 0);
  return out;
}

// an executable, or without main a library: a dylib, or static as an object
file* mach_pack(mode* self) {
  if(self->debug && !self->main && self->libs == lb_relo) fail("-g is not supported for an object yet\n");
  if(self->main) return mach_exec(self);
  return self->libs == lb_relo? mach_relo(self): mach_dyno(self);
}

const form mach = {
  .pack = mach_pack,
  .os   = os_mac,
};
