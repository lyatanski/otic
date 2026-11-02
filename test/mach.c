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
      self.zero = 100;
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
