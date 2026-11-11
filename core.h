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

byte* copy(byte* dst, const void* src, u64i size); // returns dst + size
u64i  slen(const char* s); // bytes before the NUL

// the symbol tables
typedef struct unit unit;
typedef struct mode mode;

// a name in the table of a unit, and what it means there
typedef struct item item;
struct item {
  u64i  size;
  char* name;
  void* data;
  item* next; // the same spelling in the table of a later unit (mode.names)
  u32i  unit; // the unit whose table it is in
  u32i  pick; // in the function being compiled: the register its locals get, plus 1
};

u64i  clue(const char* mark, u64i size);
item* hsee(unit*, const char* mark, u64i size, u64i clue); // NULL when not in
item* hget(mode*, u32i at, char* mark, u64i size);   // in unit at, made when not in

// p, an array of *cap elements of size bytes with n in use, with room for one
// more: it grows to twice its size plus least
void* swell(const mode*, void* p, u32i n, u32i* cap, u64i size, u32i least);
void* zero(const mode*, u64i size); // size zeroed bytes from mode.heap

// an index by hash over the things of a caller's array: a slot holds the
// place of a thing plus 1 (0 when free) and its hash. dsee is the slot of the
// thing hashed h for which is(ctx, place) holds, else a free one, then taken:
// the caller puts its new thing there. The index doubles when half full
typedef struct { u32i place, hash; } dent;
typedef struct { dent* slot; u32i size, used; } dex;
u32i* dsee(const mode*, dex*, u64i h, bool (*is)(const void* ctx, u32i place), const void* ctx);

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
  const char* path; // tidy'd, one name per file
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
// operands of rop that are no register of a local: acc, tmp, the constant n,
// argument register i (ARG - i)
enum { ACC = -1, TMP = -2, NUM = -3, ARG = -4 };
// typed slots: a variadic pointer would pass the arguments on the stack on
// Apple arm64, where the encoders read them from registers
typedef struct {
  void (*move)(file*, s32i n);                     // acc = n
  void (*load)(file*, s32i slot, u32i size);       // acc = slot, 1, 2, 4 or 8 bytes
  void (*stor)(file*, s32i slot, u32i size);       // slot = acc
  void (*peek)(file*, s32i slot, u32i size);       // tmp = slot
  void (*addr)(file*, s32i slot);                  // acc = address of slot
  void (*glob)(file*);                             // acc = address of data, patched by site
  void (*site)(const file*, byte* glob, s64i dist); // point a glob dist bytes past itself
  void (*read)(file*, u32i size);                  // acc = [acc]
  void (*fill)(file*, u32i size);                  // [acc] = tmp, acc = tmp
  void (*push)(file*);                             // push acc
  void (*pull)(file*);                             // tmp = acc, pop acc
  void (*swap)(file*);                             // tmp = acc
  void (*cast)(file*, bool wide);                  // acc = acc sign extended, or its low 32 bits
  void (*calc)(file*, u32i op, bool cnst, s32i n); // acc = acc op (n or tmp), op may be WIDE
  void (*when)(file*, bool nz);                    // branch on acc, patched by back
  void (*jump)(file*);                             // forward jump, patched by back
  void (*back)(const file*, byte* prev);           // point a forward branch here
  void (*loop)(file*, const byte* dest);           // jump back to an address
  void (*open)(file*);                             // prologue, the frame size is patched by size
  void (*size)(file*, byte* func, s32i size);
  void (*parm)(file*, u32i i, s32i slot);          // slot = argument register i
  void (*args)(file*, u32i i);                     // pop argument register i
  void (*dial)(file*);                             // call, patched by link
  void (*trap)(file*, u32i argc);                  // pop the arguments and the number, syscall
  void (*done)(file*);                             // epilogue and return
  void (*boot)(file*);                             // entry stub: call main
  void (*link)(const file*, byte* boot, const byte* main); // point a call at its function
  // call C code at func, or at the address in [acc] (an import slot), the C
  // way of os: pop the argc pushed arguments, the first fixed of them named.
  // The call to func, which link may point elsewhere, NULL through [acc]
  byte* (*ffi)(file*, const byte* func, u32i argc, u32i fixed, osys os);
  // entry of a C library, called by boot: start(main, argc, argv, 0, 0, 0)
  // with argc and argv from the initial stack. start NULL: through the import
  // slot whose address is in acc
  void (*crt)(file*, const byte* main, const byte* start);
  void (*flag)(file*, u32i op);                    // acc = 1 when the comparison op of a TEST calc held, else 0
  void (*cond)(file*, u32i op, bool hold);         // branch when it held (hold) or failed, patched by back
  // a left operand in acc kept while the right one is computed: pushed, with
  // room for lift to make it a move to scratch k when no call came between
  void (*keep)(file*);
  void (*lift)(const file*, byte* keep, u32i k);
  void (*take)(file*, u32i k);                     // tmp = acc, acc = scratch k
  void (*pass)(file*, u32i i);                     // argument register i = acc
  // an entry for C code of os to call the function that follows it: nothing
  // when otic's way of passing arguments is that of os
  void (*hail)(file*, osys os);
  // to op= by, each a register of a local or an operand above, on 64 bits
  // when WIDE: op is = (a move, a constant sign extended), + - & | ^, or a
  // comparison, which sets the flags only
  void (*rop)(file*, u32i op, s32i to, s32i by, s32i n);
  void (*stow)(file*, u32i r, s32i slot, bool back); // slot = register r of a local, or back
  chip  cpu;  // target, each form maps it to its own machine id
  u32i  regs; // registers for locals, kept by the functions called
} emit;

// core
#define VARY (~0u) // meta.size of a function from an object
typedef void* (*poke)(void*, size_t);
typedef void  (*pack)(mode*);
typedef file* (*grab)(const char*); // a source file, NUL terminated, NULL when it cannot be opened

// container
typedef struct {
  file* (*pack)(mode*);                    // lay text/data out as an output image
  void  (*take)(mode*, file*);             // load a relocatable object, named by mode.name
  osys  os;
} form;

// base and list (struct) are types, data a local, glob a global, call a function,
// refs a pointer type, many an array type, pend a name in an archive's index
// whose member is not pulled yet, dlib a shared library named by load: its name
// is the string, its addr the handle once the run loads it. bitf is the type
// of a bit-field: type its integer type, size the bytes of the unit it is read
// and written in, slot its first bit there, longs its width. proc is the type
// of a function that a pointer points to: type its result, slot its parameters
// (-1 when unknown), longs and more as for a call; it is no value (size 0)
typedef enum { base, list, data, call, glob, refs, many, pend, dlib, bitf, proc } what;
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
  byte* hail; // a function's entry for C code (emit.hail), NULL when it is addr
  meta* lib;  // the library of an import
  u32i  size; // bytes of a type, parameters of a call
  s32i  slot; // frame offset (a local: below 0, else its register), data offset, offset of a member
  what  kind;
  bool  own;  // static: the name is hidden outside its source file
  bool  weak; // defined by an object as weak: a strong definition replaces it
  bool  ext;  // a function from an object, or an import
  bool  more; // variadic: arguments may follow the size named ones
  byte  longs; // a function of the source: bit i when parameter i is 64 bits
  u32i  unit; // the unit that defines it
};

// metas in chunks of PART that never move, as names and types point at them:
// pin is meta i of part, cell the same with its chunk made when new (n chunks
// so far), for i at most one past the last
#define PART 1024
meta* cell(const mode*, meta*** p, u32i* n, u32i i);
static inline meta* pin(meta* const* p, u32i i) { return p[i / PART] + i % PART; }

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
  s64i  add;
  u32i  unit;
  bool  wide; // 64-bit absolute, else 32-bit pc-relative
  bool  weak;
  bool  load; // a GOT load made a REX lea: mov reg, 0 for address 0
  bool  got;  // a GOT load made a lea
} hook;

// a reference of a library object for its linker (knit): at site, to aim, or
// to name when nothing in the image defines it (weak: it may stay undefined),
// plus add. A glob is the arch's (emit.glob) at site.at, rel a 32-bit
// distance from the site, wide an address
enum { cd_glob, cd_rel, cd_wide };
typedef struct {
  seat  site;
  seat  aim;
  item* name;
  s64i  add;
  byte  kind;
  bool  weak;
  bool  got;  // a rel of a GOT load that object code made a lea (hook.got)
} cord;

// what a form may ask of the front end: make fills it in, a spec mocks it.
// dock lays data out at text + at, bss after it, text at org, the import
// slots after bss or mode.fore bytes before data; gap is the
// alignment of data and of bss after it; lay makes room for size bytes in
// data, or in bss (| ZERO) when zero. An object is taken into the unit being
// joined: name is a spelling in the table of unit at, find what it means
// there, heft how firmly x defines it, ask pulls the archive member of x. For
// a library: sign lists the public definitions (functions and globals, not
// imports), knit the references, both into arrays from mode.heap: their count
typedef struct {
  void  (*dock)(mode*, u64i at, u64i org);
  u64i  (*gap)(const mode*);
  u32i  (*lay)(mode*, u32i size, bool zero, u32i align);
  item* (*name)(mode*, u32i at, char* mark, u64i size);
  meta* (*find)(mode*, u32i at, item* name);
  u32i  (*heft)(const meta* x);
  u32i  (*ask)(mode*, meta* x);
  u32i  (*sign)(mode*, meta*** out);
  u32i  (*knit)(mode*, cord** out);
} core;

// what a source without main makes: nothing (running: it fails), a shared
// library, or a relocatable object (static)
enum { lb_none, lb_dyno, lb_relo };

struct mode {
  const char* name; // input
  const char* outp; // output
  u32i  unit; // the unit being parsed
  u32i  unitn, unitc;
  unit* units; // in join order, the root first
  unit  names; // each spelling in any unit's table: the first of its items
               // in join order, the others after it (item.next)
  emit  arch;
  form  form;
  ario  ario;
  core  core; // for the form

  file* text; // binary code
  byte* main; // entry of main in text, NULL in a library
  byte  libs; // what a source without main makes
  bool  musl; // a Linux image is loaded by musl's loader and names its libraries, not glibc's
  byte* boot; // what the boot stub calls: main, or crt
  byte* hail; // main's entry for C code (emit.hail), main when it has none
  file* data; // non-zero initialized data
  u64i  zero; // zero initialized data (bss) bytes
  u32i  fore; // the import slots fore bytes before data, for a form that wants them
              // in the file (PE's import address table); 0: after bss
  u32i  most; // largest alignment of data or code an object asks for
  meta* rune; // char: what a string literal points to

  poke  hold;
  poke  drop;
  // the host's heap, as realloc: p resized to size bytes, a new block when p
  // is NULL, freed when size is 0 (then NULL). It fails rather than return NULL
  poke  heap;
  pack  show;
  grab  scan;
  bool  (*have)(const item*); // running: whether the host has shared library s

  u32i  top, piles;
  meta** pile;     // the names in scope, in chunks (cell): top of them, piles chunks
  u32i  held;      // left operands kept: the next one goes to scratch held
  u32i  dials;     // calls emitted: a kept operand stays on the stack across one
  s32i  slot;      // frame bytes in use
  s32i  deep;      // frame bytes the function needs
  s32i  lits;      // frame bytes the compound literals of the block keep
  u32i  regn;      // registers the function keeps locals in, saved in its first slots
  u32i  busy;      // bit r: register r holds a local in scope
  item* pick[16];  // the names of the locals they go to
  meta* func;      // the function being compiled
  byte* cont;      // continue target, NULL outside loops
  u32i  ends, endc;
  byte** quit;     // pending break jumps
  u32i  fixs, fixc;
  fix*  link;      // forward calls
  u32i  seats, seatc;
  seat* aims;      // references to globals
  u32i  defn, defp;
  meta** defs;     // pointer, array and struct types, members, libraries, in
                   // chunks (cell): defn of them, defp chunks
  u32i  maden, madec;
  meta** made;     // the pointer, array, function and bit-field types of defs,
                   // by what they are made of (sole): a table of madec slots
  u32i  impn, impc;
  meta** imps;     // imports in use, each an 8-byte slot the loader fills
  u32i  askn, askc, askd;
  item** asks;     // archive names used, their members pulled up to askd
  dex   askx;      // the asks, by name
  u32i  dirn;
  const char* dirs[16]; // libraries locations
  u32i  hookn, hookc;
  hook* hooks;
};

void  make(mode*);
void  dock(mode*, u64i at, u64i org); // lay data out at text + at, bss after it, text at org
u64i  gap(const mode*); // alignment of data and of bss after it
bool  alike(const void* x, const void* y, u64i size);
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
u32i  wide(const meta*);
u32i  quote(const item* s, byte* out);
void  bind(const mode*, byte* at, const item* name, const meta* func, u32i argc, bool ext);
bool  join(mode*, const char* path, u64i size, file* code, byte kind);
bool  land(mode*, char* path, u64i size, file* code, byte kind);

// link.c
u64i  tidy(char* path, u64i size);
void  haul(mode*, item* s);
u32i  nest(mode*, unit x);
bool  akin(const item* x, const item* y);
u32i  heft(const meta* x);
meta* find(mode*, u32i at, item* name);
u32i  ask(mode*, meta* x);
void  reel(mode*);
meta* rack(mode*, item* s);
u32i  port(mode*, meta* x);
void  tie(mode*, u32i from, bool last);
u32i  sign(mode*, meta*** out);
u32i  knit(mode*, cord** out);

