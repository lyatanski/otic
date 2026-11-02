// vim: ts=2:sw=2
#pragma once
#include <stdio.h>   /* FILE, printf */
#include <stdlib.h>  /* malloc, free, exit */
#include <stdint.h>  /* standard data types */
#include <stdbool.h> /* bool before C23 */

typedef uint64_t u64i;
typedef  int64_t s64i;
typedef uint32_t u32i;
typedef  int32_t s32i;
typedef uint16_t u16i;
typedef  int16_t s16i;
typedef  uint8_t byte;
//typedef  uint8_t bool;
//typedef     char char;
//typedef     void void;

#define warn(...) (printf(__VA_ARGS__))
#define fail(...) (printf(__VA_ARGS__), exit(1))

// token kinds: up to three characters packed, e.g. tokd('<', '=')
#define tokc(x, y, z, ...) ((x) | (y) << 8 | (z) << 16)
#define tokd(...) tokc(__VA_ARGS__, 0, 0, 0)

byte* copy(byte* dst, void* src, u64i size); // returns dst + size
u64i  slen(char* s); // bytes before the NUL

#include "hash.h"

// p, an array of *cap elements of size bytes with n in use, with room for one
// more: it grows to twice its size plus least
void* swell(mode*, void* p, u32i n, u32i* cap, u64i size, u32i least);
void* zero(mode*, u64i size); // size zeroed bytes from mode.heap

typedef struct {
  byte*  body;
  byte*  tail;
  byte   head[1];
} file;

typedef struct {
  u32i  kind;
  union {
    item* hash;
    u64i  ival;
    u64i  fval;
  };
} word;

// each file in the pass, and the builtins (the root, unit 0): its path and
// its own symbol table. A name is looked up in the unit being parsed, then in
// the others in join order (find). Kinds: the root, a source, an object, an
// archive (its table is the symbol index), a member pulled from an archive.
// A source is parsed from where its own line, talk and from say, so another
// source can be joined from the middle of it
enum { un_root, un_code, un_objs, un_arch, un_memb };
struct unit {
  char*  path; // tidy'd, one name per file
  item** pool; // its symbol table, small and growable: the items stay put
  u32i   size; // slots in pool, a power of 2
  u32i   used;
  file*  code; // the source or object, kept: names point into it
  struct meta* defs; // an archive's index, kept: pulled names leave it
  byte   kind;
  u64i   line; // a source: the line being parsed
  word   talk; // a source: the last token
  struct meta* from; // a source: the library of its last load
};

// arch
typedef enum { cpu_ia64, cpu_arm8 } chip;
typedef enum { os_linux, os_mac, os_win } osys;
#define WIDE (1 << 24) // calc op flag: on 64 bits, for pointers and long
#define TEST (1 << 25) // calc op flag: a comparison sets the flags only, for flag and cond
#define HELD 4         // scratch registers: kept left operands past these are on the stack
// typed slots: a variadic pointer would pass the arguments on the stack on
// Apple arm64, where the encoders read them from registers
typedef struct {
  void (*move)(file*, s32i n);                     // acc = n
  void (*load)(file*, s32i slot, u32i size);       // acc = slot, 1, 2, 4 or 8 bytes
  void (*stor)(file*, s32i slot, u32i size);       // slot = acc
  void (*peek)(file*, s32i slot, u32i size);       // tmp = slot
  void (*addr)(file*, s32i slot);                  // acc = address of slot
  void (*glob)(file*);                             // acc = address of data, patched by site
  void (*site)(file*, byte* glob, s64i dist);      // point a glob dist bytes past itself
  void (*read)(file*, u32i size);                  // acc = [acc]
  void (*fill)(file*, u32i size);                  // [acc] = tmp, acc = tmp
  void (*push)(file*);                             // push acc
  void (*pull)(file*);                             // tmp = acc, pop acc
  void (*swap)(file*);                             // tmp = acc
  void (*cast)(file*, bool wide);                  // acc = acc sign extended, or its low 32 bits
  void (*calc)(file*, u32i op, bool cnst, s32i n); // acc = acc op (n or tmp), op may be WIDE
  void (*when)(file*, bool nz);                    // branch on acc, patched by back
  void (*jump)(file*);                             // forward jump, patched by back
  void (*back)(file*, byte* prev);                 // point a forward branch here
  void (*loop)(file*, byte* dest);                 // jump back to an address
  void (*open)(file*);                             // prologue, the frame size is patched by size
  void (*size)(file*, byte* func, s32i size);
  void (*parm)(file*, u32i i, s32i slot);          // slot = argument register i
  void (*args)(file*, u32i i);                     // pop argument register i
  void (*dial)(file*);                             // call, patched by link
  void (*trap)(file*, u32i argc);                  // pop the arguments and the number, syscall
  void (*done)(file*);                             // epilogue and return
  void (*boot)(file*);                             // entry stub: call main
  void (*link)(file*, byte* boot, byte* main);     // point a call at its function
  // call C code at func, or at the address in [acc] (an import slot), the C
  // way of os: pop the argc pushed arguments, the first fixed of them named.
  // The call to func, which link may point elsewhere, NULL through [acc]
  byte* (*ffi)(file*, byte* func, u32i argc, u32i fixed, osys os);
  // entry of a C library, called by boot: start(main, argc, argv, 0, 0, 0)
  // with argc and argv from the initial stack
  void (*crt)(file*, byte* main, byte* start);
  void (*flag)(file*, u32i op);                    // acc = 1 when the comparison op of a TEST calc held, else 0
  void (*cond)(file*, u32i op, bool hold);         // branch when it held (hold) or failed, patched by back
  // a left operand in acc kept while the right one is computed: pushed, with
  // room for lift to make it a move to scratch k when no call came between
  void (*keep)(file*);
  void (*lift)(file*, byte* keep, u32i k);
  void (*take)(file*, u32i k);                     // tmp = acc, acc = scratch k
  void (*pass)(file*, u32i i);                     // argument register i = acc
  chip  cpu;  // target, each form maps it to its own machine id
} emit;

// core
#define VARY (~0u) // meta.size of a function from an object
typedef void* (*poke)(void*, size_t);
typedef void  (*pack)(mode*);
typedef file* (*grab)(char*); // a source file, NUL terminated, NULL when it cannot be opened

// container
typedef struct {
  file* (*pack)(mode*);                    // lay text/data out as an output image
  void  (*take)(mode*, file*);             // load a relocatable object, named by mode.name
  osys  os;
} form;

// base and list (struct) are types, data a local, glob a global, call a function,
// refs a pointer type, many an array type, pend a name in an archive's index
// whose member is not pulled yet, dlib a shared library named by load: its name
// is the string, its addr the handle once the run loads it
typedef enum { base, list, data, call, glob, refs, many, pend, dlib } what;
typedef struct meta meta;

// static libraries: none when load is NULL. pull is called only for the names
// load put in a table (pend)
typedef struct {
  void  (*load)(mode*, file*); // the symbol index of an archive, its table
  void  (*pull)(mode*, meta*); // the member that defines an archive name
} ario;

struct meta {
  meta* type; // data link to its type
  meta* hide; // what the name meant before
  item* name;
  byte* addr;
  meta* lib;  // the library of an import
  u32i  size; // bytes of a type, parameters of a call
  s32i  slot; // frame offset, data offset, offset of a member
  what  kind;
  bool  own;  // static: the name is hidden outside its source file
  bool  weak; // defined by an object as weak: a strong definition replaces it
  bool  ext;  // a function from an object, or an import
  bool  more; // variadic: arguments may follow the size named ones
  byte  longs; // a function of the source: bit i when parameter i is 64 bits
  u32i  unit; // the unit that defines it
};

// call to a function not seen yet, or to C code in an archive member not
// pulled yet (ext, through ffi), patched when the run ends
typedef struct {
  item* name; // in the table of unit
  byte* at;   // NULL when the call was dropped as dead code
  u32i  argc;
  u64i  line;
  u32i  unit; // the unit of the call
  bool  ext;
} fix;

// address of a global (data offset, bss offset | ZERO, import slot i | SLOT,
// or the global named by ask i | WANT), patched by dock
#define WANT (1u << 29)
#define SLOT (1u << 30)
#define ZERO (1u << 31)
typedef struct {
  byte* at;
  u32i  off;
} seat;

// a reference of object code: site holds the distance to aim (wide: its
// address), or to what name, in the table of unit, means when the run ends,
// plus add; patched by dock. Address 0 when name is weak and nothing defines it
typedef struct {
  seat  site;
  seat  aim;
  item* name;
  u32i  unit;
  s64i  add;
  bool  wide; // 64-bit absolute, else 32-bit pc-relative
  bool  weak;
  bool  load; // a GOT load made a REX lea: mov reg, 0 for address 0
} hook;

// what a form may ask of the front end: make fills it in, a spec mocks it.
// dock lays data out at text + at, bss after it, text at org; gap is the
// alignment of data and of bss after it; lay makes room for size bytes in
// data, or in bss (| ZERO) when zero. An object is taken into the unit being
// joined: name is a spelling in the table of unit at, find what it means
// there, heft how firmly x defines it, ask pulls the archive member of x
typedef struct {
  void  (*dock)(mode*, u64i at, u64i org);
  u64i  (*gap)(mode*);
  u32i  (*lay)(mode*, u32i size, bool zero, u32i align);
  item* (*name)(mode*, u32i at, char* mark, u64i size);
  meta* (*find)(mode*, u32i at, item* name);
  u32i  (*heft)(meta* x);
  u32i  (*ask)(mode*, meta* x);
} core;

struct mode {
  char* name; // input
  char* outp; // output
  u32i  unit; // the unit being parsed
  u32i  unitn, unitc;
  unit* units; // in join order, the root first
  unit  names; // each spelling in any unit's table: data, the first of its
               // items in join order (item.next)
  emit  arch;
  form  form;
  ario  ario;
  core  core; // for the form

  file* text; // binary code
  byte* main; // entry of main in text
  byte* boot; // what the boot stub calls: main, or crt
  file* data; // non-zero initialized data
  u64i  zero; // zero initialized data (bss) bytes
  u32i  most; // largest alignment of data or code an object asks for
  meta* rune; // char: what a string literal points to

  poke  hold;
  poke  drop;
  // the host's heap, as realloc: p resized to size bytes, a new block when p
  // is NULL, freed when size is 0 (then NULL). It fails rather than return NULL
  poke  heap;
  pack  show;
  grab  scan;
  bool  (*have)(item*); // running: whether the host has shared library s

  u32i  top;
  meta  pile[256];
  u32i  held;      // left operands kept: the next one goes to scratch held
  u32i  dials;     // calls emitted: a kept operand stays on the stack across one
  s32i  slot;      // frame bytes in use
  s32i  deep;      // frame bytes the function needs
  meta* func;      // the function being compiled
  byte* cont;      // continue target, NULL outside loops
  u32i  ends;      // pending break jumps
  byte* quit[64];
  u32i  fixs;
  fix   link[256];
  u32i  seats;
  seat  aims[1024];
  u32i  defn;
  meta  defs[512]; // pointer, array and struct types, members, libraries
  u32i  impn;
  meta* imps[256]; // imports in use, each an 8-byte slot the loader fills
  u32i  askn, askc, askd;
  item** asks;     // archive names used, their members pulled up to askd
  u32i  dirn;
  char* dirs[16];  // libraries locations
  u32i  hookn, hookc;
  hook* hooks;
};

void  make(mode*);
void  dock(mode*, u64i at, u64i org); // lay data out at text + at, bss after it, text at org
u64i  gap(mode*); // alignment of data and of bss after it
bool  alike(void* x, void* y, u64i size);
u32i  lay(mode*, u32i size, bool zero, u32i align);

// the unit being parsed
#define home(self) ((self)->units + (self)->unit)
// fail at the line being parsed, in the file of the unit being parsed
#define flaw(self, fmt, ...) \
  fail("%s:%llu: " fmt, home(self)->path, (unsigned long long)home(self)->line __VA_OPT__(,) __VA_ARGS__)
#define emit(self, call, ...) (self->arch.call(self->text __VA_OPT__(,) __VA_ARGS__))
#define nick(x) (int)(x)->size, (x)->name

// core.c, for link.c
meta* mint(mode*, meta x);
u32i  wide(meta*);
u32i  quote(item* s, byte* out);
void  bind(mode*, byte* at, item* name, meta* func, u32i argc, bool ext);
bool  join(mode*, char* path, u64i size, file* code, byte kind);
bool  land(mode*, char* path, u64i size, file* code, byte kind);

// link.c
u64i  tidy(char* path, u64i size);
void  haul(mode*, item* s);
u32i  nest(mode*, unit x);
bool  akin(item* x, item* y);
u32i  heft(meta* x);
meta* find(mode*, u32i at, item* name);
u32i  ask(mode*, meta* x);
void  reel(mode*);
meta* rack(mode*, item* s);
u32i  port(mode*, meta* x);
void  tie(mode*, u32i from, bool last);

