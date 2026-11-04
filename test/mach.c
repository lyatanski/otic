// vim: ts=2:sw=2
// mach's spec: the Mach-O executable it packs, signed ad hoc
#include "form.h"
#include "../mach.c"
#include "test.h"

#define CODE "\xb8\x07\x00\x00\x00\xc3" "\xb8\x2a\x00\x00\x00\xc3" // f, main
#define PAGE 0x4000

// sha256 of the size bytes at msg is want, in hex
static bool  hashes(char* msg, u64i size, char* want) {
  byte out[32];
  sha256((byte*)msg, size, out);
  char got[65];
  for(int i = 0; i < 32; i++) sprintf(got + 2 * i, "%02x", out[i]);
  if(!strcmp(got, want)) return 1;
  printf("    want %s\n    got  %s\n", want, got);
  return 0;
}

static u32i  be(byte* at) { return (u32i)at[0] << 24 | at[1] << 16 | at[2] << 8 | at[3]; }

// a library of the image start lays out: CODE, then a lea at 12; 16 bytes of
// data; 32 of bss. It defines lf at 0 in text, lv at 4 in data and lz, weak,
// at 8 in bss
static meta  lf, lv, lz;
static void  library(chip cpu, byte libs) {
  start(cpu, CODE "\x48\x8d\x05\x00\x00\x00\x00", 19, "abcdefghijklmnop", 16, 0);
  ours.main = NULL;
  ours.libs = libs;
  ours.zero = 32;
  lf = (meta){.name = named("lf"), .kind = call, .addr = ours.text->head};
  lv = (meta){.name = named("lv"), .kind = glob, .slot = 4};
  lz = (meta){.name = named("lz"), .kind = glob, .slot = 8 | ZERO, .weak = 1};
  signs[0] = &lf; signs[1] = &lv; signs[2] = &lz;
  signn = 3;
}
static void  cord_(cord c) { knits[cordn++] = c; }
static void  import(char* name) {
  static meta imps[4];
  imps[ours.impn] = (meta){.name = named(name), .kind = call};
  ours.imps[ours.impn] = imps + ours.impn;
  ours.impn++;
}

// the first load command cmd of an image, section i of the segment seg, symbol
// i and its name
#define HEAD(out) ((mach_mh*)(out)->head)
static void*  cmd_(file* out, u32i cmd) {
  byte* at = out->head + sizeof(mach_mh);
  for(u32i i = 0; i < HEAD(out)->ncmds; i++, at += ((u32i*)at)[1])
    if(*(u32i*)at == cmd) return at;
  return NULL;
}
static sec64* sec_(seg64* seg, u32i i) { return (sec64*)(seg + 1) + i; }
static nl64*  sym_(file* out, u32i i) {
  return (nl64*)(out->head + ((mach_symtab*)cmd_(out, lc_symtab))->symoff) + i;
}
static char*  name_(file* out, nl64* s) {
  return (char*)out->head + ((mach_symtab*)cmd_(out, lc_symtab))->stroff + s->strx;
}
static u64i   uleb_(byte* at) { // a ULEB128 number
  u64i v = 0;
  for(u32i k = 0; k == 0 || at[-1] & 0x80; k += 7) v |= (u64i)(*at++ & 0x7f) << k;
  return v;
}
static mrel*  rel_(file* out, sec64* sec, u32i i) { return (mrel*)(out->head + sec->reloff) + i; }
// relocation r: at addr, to symbol or section sym, pc-relative, of 2^len bytes,
// external, of type
static bool   rel_is(mrel* r, s32i addr, u32i sym, bool pcrel, u32i len, bool ext, u32i type) {
  u32i want = sym | pcrel << 24 | len << 25 | ext << 27 | type << 28;
  if(r->addr == addr && r->info == want) return 1;
  printf("    want %x %08x\n    got  %x %08x\n", addr, want, r->addr, r->info);
  return 0;
}

spec("mach") {
  context("pack") {
    file*      out;
    mach_head* h;
    it("writes a 64-bit executable for the target") {
      start(cpu_ia64, CODE, 12, "", 0, 6);
      check((out = packed(mach)) && (h = (mach_head*)out->head));
      check(h->mh.magic == mh_magic && h->mh.type == mh_exec && h->mh.ncmds == 11);
      check(h->mh.cpu == 0x01000007 && h->mh.sub == 3 && h->mh.flags & mh_pie);
      start(cpu_arm8, CODE, 12, "", 0, 6);
      check((out = packed(mach)) && ((mach_head*)out->head)->mh.cpu == 0x0100000c);
    }
    it("loads the code after its headers, entering at main through LC_MAIN") {
      start(cpu_ia64, CODE, 12, "", 0, 6);
      check((out = packed(mach)) && (h = (mach_head*)out->head));
      check(bytes(out->head + sizeof(mach_head), "b8 07 00 00 00 c3 b8 2a"));
      check(h->main.entry == sizeof(mach_head) + 6);
      check(h->code.off == sizeof(mach_head) && h->code.size == 12);
      check(h->text.addr == 0x100000000 && h->text.fsz == PAGE);
    }
    it("docks once, data on the page after the code") {
      start(cpu_ia64, CODE, 12, "", 0, 6);
      check(packed(mach));
      check(docked.calls == 1 && docked.at == PAGE - sizeof(mach_head) && docked.org == 0);
    }
    it("puts data and bss in __DATA") {
      start(cpu_ia64, CODE, 12, "abcd", 4, 6);
      ours.zero = 100;
      check((out = packed(mach)) && (h = (mach_head*)out->head));
      check(!strcmp(h->data.name, "__DATA") && h->data.off == PAGE && h->data.fsz == 4);
      check(h->data.addr == 0x100000000 + PAGE && h->data.vmsz == PAGE);
      check(bytes(out->head + PAGE, "61 62 63 64"));
    }
    it("ends with a signature of each 4 KB page") {
      start(cpu_ia64, CODE, 12, "abcd", 4, 6);
      check((out = packed(mach)) && (h = (mach_head*)out->head));
      byte* blob = out->head + h->sign.off;
      check(h->sign.off == 2 * PAGE && blob + h->sign.fsz == out->body);
      check(be(blob) == 0xfade0cc0 && be(blob + 20) == 0xfade0c02);
      byte* hash = blob + 20 + 88 + sizeof(mach_name); // the code directory's slots
      byte  want[32];
      for(u32i i = 0; i < h->sign.off >> 12; i++) {
        sha256(out->head + i * 4096, 4096, want);
        check(!memcmp(hash + 32 * i, want, 32));
      }
    }
  }

  context("static library") { // x86-64: one segment, sections text, data, bss
    file*  out;
    seg64* seg;
    it("writes one object without main, with -s") {
      library(cpu_ia64, lb_relo);
      check((out = packed(mach)) && HEAD(out)->type == mh_object && HEAD(out)->ncmds == 4);
      check(docked.calls == 0 && (seg = cmd_(out, lc_segment)) && seg->nsec == 3);
      sec64* s = sec_(seg, 0);
      check(!strcmp(s[0].name, "__text") && s[0].addr == 0 && s[0].size == 19 && s[0].flags == s_code);
      check(bytes(out->head + s[0].off, "b8 07 00 00 00 c3"));
      check(s[1].addr == 32 && s[1].size == 16 && s[1].off == s[0].off + 32);
      check(bytes(out->head + s[1].off, "61 62 63 64"));
      check(s[2].addr == 48 && s[2].size == 32 && s[2].flags == s_zerofill);
    }
    it("refuses arm64, so far") {
      library(cpu_arm8, lb_relo);
      check(!packed(mach) && strstr(said, "an arm64 object is not supported"));
    }
    it("names the public definitions as C does, with an underscore") {
      library(cpu_ia64, lb_relo);
      check((out = packed(mach)));
      mach_dysym* d = cmd_(out, lc_dysymtab);
      check(d->iext == 0 && d->next == 3 && d->iund == 3 && d->nund == 0);
      nl64* s = sym_(out, 0);
      check(!strcmp(name_(out, s), "_lf") && s[0].type == (n_sect | n_ext) && s[0].sect == 1 && s[0].value == 0);
      check(!strcmp(name_(out, s + 1), "_lv") && s[1].sect == 2 && s[1].value == 32 + 4);
      check(s[2].sect == 3 && s[2].value == 48 + 8 && s[2].desc == n_weak_def);
    }
    it("relocates a global by its section, the distance in place") {
      library(cpu_ia64, lb_relo);
      cord_((cord){.site = {ours.text->head + 12, 0}, .aim = {NULL, 4}, .kind = cd_glob});
      cord_((cord){.site = {ours.text->head + 12, 0}, .aim = {NULL, 8 | ZERO}, .kind = cd_glob});
      check((out = packed(mach)) && (seg = cmd_(out, lc_segment)) && sec_(seg, 0)->nrel == 2);
      check(rel_is(rel_(out, sec_(seg, 0), 0), 15, 2, 1, 2, 0, x64_signed));
      check(rel_is(rel_(out, sec_(seg, 0), 1), 15, 3, 1, 2, 0, x64_signed));
      check(bytes(out->head + sec_(seg, 0)->off + 15, "25 00 00 00")); // 48 + 8 - 19
    }
    it("relocates a global nothing defines by its name") {
      library(cpu_ia64, lb_relo);
      cord_((cord){.site = {ours.text->head + 12, 0}, .name = named("ext"), .kind = cd_glob, .weak = 1});
      check((out = packed(mach)) && (seg = cmd_(out, lc_segment)));
      check(rel_is(rel_(out, sec_(seg, 0), 0), 15, 3, 1, 2, 1, x64_signed));
      check(bytes(out->head + sec_(seg, 0)->off + 15, "00 00 00 00"));
      nl64* s = sym_(out, 3);
      check(!strcmp(name_(out, s), "_ext") && s->type == n_ext && s->desc == n_weak_ref);
    }
    it("puts the import slots after data, each relocated by its name") {
      library(cpu_ia64, lb_relo);
      import("puts");
      cord_((cord){.site = {ours.text->head + 12, 0}, .aim = {NULL, SLOT}, .kind = cd_glob});
      check((out = packed(mach)) && (seg = cmd_(out, lc_segment)) && sec_(seg, 1)->size == 24);
      check(rel_is(rel_(out, sec_(seg, 0), 0), 15, 2, 1, 2, 0, x64_signed));
      check(bytes(out->head + sec_(seg, 0)->off + 15, "1d 00 00 00")); // 32 + 16 - 19
      check(sec_(seg, 1)->nrel == 1 && rel_is(rel_(out, sec_(seg, 1), 0), 16, 3, 0, 3, 1, x64_unsigned));
      check(!strcmp(name_(out, sym_(out, 3)), "_puts") && sym_(out, 3)->type == n_ext);
    }
  }

  context("dylib") { // segments __TEXT, __DATA, __LINKEDIT
    file*  out;
    seg64* seg;
    it("writes one without main by default") {
      library(cpu_ia64, lb_dyno);
      check((out = packed(mach)) && HEAD(out)->type == mh_dylib && HEAD(out)->ncmds == 10);
      check(HEAD(out)->flags == (mh_noundefs | mh_dyldlink | mh_twolevel | mh_noreexport));
      check((seg = cmd_(out, lc_segment)) && seg->addr == 0 && seg->off == 0 && seg->fsz == PAGE);
      sec64* s = sec_(seg, 0);
      check(s->addr == s->off && bytes(out->head + s->off, "b8 07 00 00 00 c3"));
      seg64* data = (seg64*)((byte*)seg + seg->size);
      check(!strcmp(data->name, "__DATA") && data->addr == PAGE && data->off == PAGE && data->fsz == 16);
      check(sec_(data, 1)->addr == PAGE + 16 && sec_(data, 1)->size == 32);
    }
    it("docks once, text at its address, data on the page after it") {
      library(cpu_ia64, lb_dyno);
      check((out = packed(mach)) && (seg = cmd_(out, lc_segment)));
      check(docked.calls == 1 && docked.org == sec_(seg, 0)->addr && docked.at == PAGE - docked.org);
    }
    it("names itself @rpath/ and its file name") {
      library(cpu_ia64, lb_dyno);
      ours.outp = "bins/libq.dylib";
      mach_dylib* id;
      check((out = packed(mach)) && (id = cmd_(out, lc_id_dylib)));
      check(!strcmp((char*)id + id->name, "@rpath/libq.dylib") && id->size % 8 == 0);
    }
    it("exports the public names, a name before its prefixes") {
      library(cpu_ia64, lb_dyno);
      lv.name = named("lff");
      mach_info* info;
      check((out = packed(mach)) && (info = cmd_(out, (u32i)lc_dyld_info)) && (seg = cmd_(out, lc_segment)));
      byte* t = out->head + info->trie;
      check(t[0] == 0 && t[1] == 3 && !strcmp((char*)t + 2, "_lz") && !strcmp((char*)t + 10, "_lff"));
      check(!strcmp((char*)t + 19, "_lf"));
      byte* n = t + uleb_(t + 23); // _lf: its size, flags (not weak), address, no edges
      check(n[0] == 3 && n[1] == 0 && uleb_(n + 2) == sec_(seg, 0)->addr && n[4] == 0);
      n = t + uleb_(t + 6); // _lz: weak
      check(n[1] == 4 && uleb_(n + 2) == PAGE + 16 + 8);
    }
    it("binds the imports through their slots after bss, in any image") {
      library(cpu_arm8, lb_dyno);
      import("puts");
      mach_info* info;
      check((out = packed(mach)) && (info = cmd_(out, (u32i)lc_dyld_info)));
      check(!(HEAD(out)->flags & mh_noundefs) && HEAD(out)->cpu == 0x0100000c);
      check(info->nbind == 13 && bytes(out->head + info->bind, "3e 51 40 5f 70 75 74 73 00 71 30 90 00"));
      check(sym_(out, 3)->type == n_ext && sym_(out, 3)->desc == n_flat);
    }
    it("ends with a signature of its pages, the last up to it") {
      library(cpu_ia64, lb_dyno);
      mach_blob* sign;
      check((out = packed(mach)) && (sign = cmd_(out, lc_signature)) && sign->off % 16 == 0);
      check(sign->off + sign->fsz == out->body - out->head && sign->off % 4096);
      byte* blob = out->head + sign->off;
      u32i  n = (sign->off + 4095) >> 12;
      check(be(blob + 20 + 28) == n && be(blob + 20 + 32) == sign->off && be(blob + 20 + 84) == 0);
      byte want[32];
      sha256(out->head + (n - 1) * 4096, sign->off % 4096, want);
      check(!memcmp(blob + 20 + 88 + sizeof(mach_name) + 32 * (n - 1), want, 32));
    }
  }

  context("sha256") {
    it("hashes the empty message") {
      check(hashes("", 0, "e3b0c44298fc1c149afbf4c8996fb92427ae41e4649b934ca495991b7852b855"));
    }
    it("hashes one block") {
      check(hashes("abc", 3, "ba7816bf8f01cfea414140de5dae2223b00361a396177a9cb410ff61f20015ad"));
    }
    it("hashes past a block, the length in the next") {
      char* m = "abcdbcdecdefdefgefghfghighijhijkijkljklmklmnlmnomnopnopq";
      check(hashes(m, 56, "248d6a61d20638b8e5c026930c3e6039a33ce45964ff2167f6ecedd419db06c1"));
    }
  }
}
