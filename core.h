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

#define warn(...) (printf(__VA_ARGS__))
#define fail(...) (printf(__VA_ARGS__), exit(1))

// a token kind: up to three characters packed, e.g. tokd('<', '=')
#define tokc(x, y, z, ...) ((x) | (y) << 8 | (z) << 16)
#define tokd(...) tokc(__VA_ARGS__, 0, 0, 0)

byte* copy(byte* dst, const void* src, u64i size); // returns dst + size
u64i  slen(const char* s);
s32i  ilog(s64i n); // k when n is 2^k and positive, else -1

typedef struct unit unit;
typedef struct mode mode;
typedef struct dwarf dwarf;

// a name in a unit's table, and what it means there
typedef struct item item;
struct item {
  u64i  size;
  char* name;
  void* data;
  item* next; // the same spelling in a later unit's table (mode.names)
  u32i  unit;
  u32i  pick; // in the function being compiled: the register its locals get, plus 1
};

u64i  clue(const char* mark, u64i size);
item* hsee(unit*, const char* mark, u64i size, u64i clue); // NULL when not in
item* hget(mode*, u32i at, char* mark, u64i size);   // in unit at, made when not in

// room in p (*cap elements of size bytes, n used) for one more: *cap grows to
// 2 * *cap + least
void* swell(const mode*, void* p, u32i n, u32i* cap, u64i size, u32i least);
void* zero(const mode*, u64i size); // from mode.heap

// a hash index over a caller's array: a slot holds a place plus 1 (0: free)
// and its hash. dsee is the slot of the place hashed h for which is(ctx,
// place) holds, else a free one, taken for the caller's new thing. It doubles
// when half full
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

// a token as sift lexed it: the source before it, after it, the line there
typedef struct { byte *from, *to; u64i line; word talk; } lexed;

// a file of the pass, or the builtins (the root, unit 0), with its own symbol
// table. A name is looked up in the unit being parsed, then in the others in
// join order (find). An archive's table is its symbol index. A source is
// parsed from where its line, talk and from say, so another source can be
// joined from the middle of it
enum { un_root, un_code, un_objs, un_arch, un_memb };
struct unit {
  const char* path; // tidy'd, one name per file
  item** pool; // growable, but the items stay put
  u32i   size; // slots in pool, a power of 2
  u32i   used;
  file*  code; // kept: names point into it
  struct meta* defs; // an archive's index, kept: pulled names leave it
  byte   kind;
  u64i   line; // a source: the line being parsed
  word   talk; // a source: the last token
  struct meta* from; // a source: the library of its last load
};

typedef enum { cpu_ia64, cpu_arm8 } chip;
typedef enum { os_linux, os_mac, os_win } osys;
// a Linux program's C library: its loader, library names and start
typedef enum { cl_glibc, cl_musl, cl_bionic } clib;
#define WIDE (1 << 24) // calc op flag: on 64 bits, for pointers and long
#define TEST (1 << 25) // calc op flag: a comparison sets the flags only, for flag and cond
#define REAL (1 << 26) // flop op flag: a comparison of reals, for flag and cond
#define RETS 6         // ffi and hail: bit RETS of real when the result is a real
#define HELD 4         // scratch registers: kept left operands past these are on the stack
#define SIFT 64        // names sift follows in a function
// rop operands other than a local's register: acc, tmp, the constant n,
// argument register i (ARG - i)
enum { ACC = -1, TMP = -2, NUM = -3, ARG = -4 };
// no variadic slots: Apple arm64 would pass their arguments on the stack,
// where the encoders read registers
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
  void (*bare)(file*);                             // return from a function that has no frame (no open)
  void (*boot)(file*);                             // entry stub: call main
  void (*link)(const file*, byte* boot, const byte* main); // point a call at its function
  // call C code at func, or through the import slot at [acc], as os does:
  // pop argc pushed arguments, the first fixed of them named; bit i of real:
  // argument i is a real, bit RETS: the result is. Returns the call to func
  // for link to repoint, NULL through [acc]
  byte* (*ffi)(file*, const byte* func, u32i argc, u32i fixed, u32i real, osys os);
  // called by boot: start(main, argc, argv, 0, 0, 0), argc and argv from the
  // initial stack; bionic: start(stack, 0, main, structors), structors all 0.
  // start NULL: through the import slot whose address is in acc
  void (*crt)(file*, const byte* main, const byte* start, clib c);
  void (*flag)(file*, u32i op);                    // acc = 1 when the comparison op of a TEST calc held, else 0
  void (*cond)(file*, u32i op, bool hold);         // branch when it held (hold) or failed, patched by back
  // keep a left operand in acc while the right one is computed: pushed, with
  // room for lift to make it a move to scratch k when no call came between,
  // or, k from HELD up, to the register of locals k - HELD across calls
  void (*keep)(file*);
  void (*lift)(const file*, byte* keep, u32i k);
  void (*take)(file*, u32i k);                     // tmp = acc, acc = scratch k
  void (*pass)(file*, u32i i);                     // argument register i = acc
  // an entry for C code of os into the function that follows (argc
  // parameters, real as for ffi), nothing when os passes arguments as otic
  // does. otic passes a real's bits as it does a long's
  void (*hail)(file*, osys os, u32i argc, u32i real);
  // to op= by, each a local's register or an operand above, 64-bit when
  // WIDE: op is = (a constant sign extended), + - & | ^, or a comparison,
  // which sets the flags only
  void (*rop)(file*, u32i op, s32i to, s32i by, s32i n);
  void (*stow)(file*, u32i r, s32i slot, bool back); // slot = register r of a local, or back
  // facc = the integer in acc (on 64 bits when wide) as a real, when real;
  // else acc = the real in facc truncated to a 64-bit integer
  void (*turn)(file*, bool real, bool wide);
  // acc += by * scale, by a local's register or tmp, sign extended from 32
  // bits unless wide; scale 1, 2, 4 or 8. tmp may change
  void (*step)(file*, s32i by, u32i scale, bool wide);
  // reals, 64-bit IEEE, in registers of their own: facc and ftmp (ACC and
  // TMP), and r from 0 for real local r. to op= by: op is =, + - * /, or a
  // comparison (| REAL | TEST), which sets the flags only and may change acc
  // and tmp
  void (*fop)(file*, u32i op, s32i to, s32i by);
  void (*fimm)(file*, s32i to, u64i bits);             // to = the real of these bits, acc may change
  void (*fbit)(file*, bool in, s32i f, s32i x);        // in: f = the bits of x, an operand of rop, else x = those of f
  void (*fslot)(file*, bool store, s32i f, s32i slot); // f = slot, or slot = f
  // slot = the register of real local r, or back, as C code keeps it: size 8
  // or 16
  void (*fstow)(file*, u32i r, s32i slot, bool back, u32i size);
  void (*fat)(file*, bool store, s32i f);              // f = [acc], or [acc] = f
  // keep, lift, take and pull for facc (real scratch k; from HELD up, the
  // register of locals k - HELD); ftake and fpull set ftmp = facc first. acc
  // may change
  void (*fkeep)(file*);
  void (*flift)(const file*, byte* keep, u32i k);
  void (*ftake)(file*, u32i k);
  void (*fpull)(file*);
  chip  cpu;   // each form maps it to its own machine id
  u32i  regs;  // registers for locals, kept by the functions called
  u32i  fregs; // registers for real locals, kept by otic's functions and by C
               // code, but on x86-64 outside Windows (core saves them around a call)
  // spare registers (locals from regs up, real locals from fregs up): kept by
  // no function and used only by calls (and integer division, those of bit j
  // of sdiv), so a function that calls nothing keeps locals there unsaved.
  // Spare j is argument register sarg + j when j is below sargs
  u32i  spare, fspare, sarg, sargs, sdiv;
} emit;
#define FREG 16 // registers of locals from FREG up are those of real locals, r - FREG

#define VARY (~0u) // meta.size of a function from an object
typedef void* (*poke)(void*, size_t);
typedef void  (*pack)(mode*);
typedef file* (*grab)(const char*); // a source file, NUL terminated, NULL when it cannot be opened

typedef struct {
  file* (*pack)(mode*);                    // lay text/data out as an output image
  void  (*take)(mode*, file*);             // load a relocatable object, named by mode.name
  osys  os;
} form;

// base and list (struct) are types, data a local, glob a global, call a
// function, refs a pointer type, many an array type, pend an archive index
// name whose member is not pulled yet, dlib a shared library named by load
// (name the string, addr the handle once loaded). bitf, a bit-field type: type
// its integer type, size the bytes of the unit it is accessed in, slot its
// first bit there, longs its width. proc, a function pointer's target type:
// type its result, slot its parameters (-1 when unknown), longs and more as
// for a call; no value (size 0)
typedef enum { base, list, data, call, glob, refs, many, pend, dlib, bitf, proc } what;
typedef struct meta meta;

// static libraries: none when load is NULL. pull gets only names load put in
// a table (pend)
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
  bool  lots; // a function of the source or an import that shares its name with others (overloads)
  bool  fore; // a function a forward call was bound to, by its name
  byte  longs; // a function of the source: bit i when parameter i is 64 bits
  byte  reals; // a function or a function type: bit i when parameter i is a real
  u32i  unit; // the unit that defines it
  u32i  die;  // a type: its number in the debug information (-g), 0 until it has one
  meta** args; // a function of the source or an import: the types of its size parameters
};

// metas in chunks of PART that never move (names and types point at them):
// pin is meta i, cell the same, making its chunk when new (n so far), for i
// at most one past the last
#define PART 1024
meta* cell(const mode*, meta*** p, u32i* n, u32i i);
static inline meta* pin(meta* const* p, u32i i) { return p[i / PART] + i % PART; }

// a call to a function not seen yet, or to C code in an unpulled archive
// member (ext, through ffi), patched when the run ends
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
// alignment of data and of bss after it (bss and the slots where bssat and
// slotat say, when set: an image that grows); lay makes room for size bytes in
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
  // the debug sections (-g) of an image with text, data and bss at those
  // addresses, in out
  void  (*dbug)(mode*, u64i text, u64i data, u64i bss, dwarf* out);
} core;


// -g: recorded by the pass (dwarf.c), written by a form after the image.
// Text offsets are from the start of text; a type is a number (meta.die),
// its DIE in tys, its place in offs
typedef struct { u32i at, unit, line; } dwrow;      // a row of the line table
typedef struct { u32i lo, hi, up; } dwblk;          // a block: its code, and the block around it plus 1 (0: the function)
typedef struct { item* name; u32i type, unit, line, block; s32i slot; bool parm; } dwvar; // a local, at slot from rbp or x29
typedef struct { u32i lo, hi; } dwend;              // an epilogue: its code
typedef struct {
  item* name; const meta* func; u32i type, unit, line, lo, hi; bool own; // func->name: its symbol, which an overload's is not name
  u32i  vars, nvars, blks, nblks, ends, nends;      // its locals, blocks and epilogues, in the arrays
} dwfun;
typedef struct { item* name; u32i type, unit, line, slot, size; bool own; } dwglb; // a global: its data offset (| ZERO)
typedef struct { u32i at, id; } dwfix;              // a reference in tys to type id, patched when written
typedef struct { byte* p; u32i n, c; } blob;        // bytes from mode.heap: n of c in use

typedef struct {
  dwrow* rows; u32i nrow, crow;
  dwblk* blks; u32i nblk, cblk;
  dwvar* vars; u32i nvar, cvar;
  dwend* ends; u32i nend, cend;
  dwfun* funs; u32i nfun, cfun;
  dwglb* glbs; u32i nglb, cglb;
  blob   tys;                     // the DIEs of the types, as they are made
  u32i*  offs; u32i ids, coff;    // offs[id]: where in tys the DIE of type id is, ids made so far
  dwfix* fixs; u32i nfix, cfix;
  u32i*  open; u32i deep, copen;  // the blocks being compiled, innermost last
  u32i   vars0, blks0, ends0;     // the first local, block and epilogue of the function being compiled
} dbg;

struct dwarf { blob info, abbr, line, frame, ranges; };

// a file a form writes beside its image (-g: a Mach-O debug bundle), its path
// from mode.heap
typedef struct { char* path; file* data; } sidecar;

// what a source without main makes: nothing (running: it fails), a shared
// library, or a relocatable object (static)
enum { lb_none, lb_dyno, lb_relo };

// what a # gave: the bits of a number or a real (per type), or a char*
// result's bytes in text, NUL terminated, from mode.heap
typedef struct { meta* type; u64i bits; char* text; u32i size; } yield;

struct mode {
  const char* name;
  const char* outp;
  u32i  unit; // the unit being parsed
  u32i  unitn, unitc;
  unit* units; // in join order, the root first
  meta  *bvoid, *bbool, *bbyte, *bchar, *bnu16, *bint, *blong, *breal; // for tget: a source may reuse their names
  unit  names; // each spelling in any unit's table: its first item in join
               // order, the rest chained by item.next
  emit  arch;
  form  form;
  ario  ario;
  core  core; // for the form

  file* text;
  byte* main; // entry of main in text, NULL in a library
  byte  libs; // what a source without main makes
  clib  clib;
  byte* boot; // what the boot stub calls: main, or crt
  byte* hail; // main's entry for C code (emit.hail), main when it has none
  file* data;
  u64i  zero; // bss bytes
  u32i  fore; // the import slots fore bytes before data, for a form that wants them
              // in the file (PE's import address table); 0: after bss
  u32i  most; // largest alignment of data or code an object asks for

  poke  hold;
  poke  drop;
  // realloc that frees at size 0 (returning NULL) and fails rather than
  // return NULL otherwise
  poke  heap;
  pack  show;
  grab  scan;
  bool  (*have)(const item*); // running: whether the host has shared library s
  // #: eval runs the C-ABI function at entry in an image of the code so far,
  // as host machine code, giving its result bits (a real's too). NULL for
  // another target: mirror then builds and runs a host mode for the same
  // source, whose tape of yields this one takes in order
  u64i  (*eval)(mode*, byte* entry, bool real);
  mode* (*mirror)(mode*);
  yield* tape;     // the results of the # run so far, in order
  u32i  tapen, tapec, taken; // taken: the next one a replay takes
  bool  shaded;    // the tape is the mirror's
  bool  hidden;    // the locals of the function are out of scope for a #
  u32i  layers;    // sources a # generated, one inside another
  u32i  dry;       // nesting of overload arguments compiled for their types only: a # there is not run
  u32i  ripe;      // bytes of data that finished declarations have laid
  u32i  big;       // 1 + the data offset of the last long constant read from data (huge)
  u64i  bssat, slotat; // set while docking an image: where bss and the import slots are

  u32i  top, piles;
  meta** pile;     // the names in scope, in chunks (cell): top of them, piles chunks
  u32i  frame;     // the first name of the function being compiled in pile, ~0u outside one
  u32i  held;      // left operands kept: the next one goes to scratch held
  u32i  dials;     // calls emitted: a kept operand stays on the stack across one
  s32i  slot;      // frame bytes in use
  s32i  deep;      // frame bytes the function needs
  s32i  lits;      // frame bytes the compound literals of the block keep
  u32i  regn;      // registers the function keeps locals in, saved in its first slots
  u32i  fregn;     // registers it keeps real locals in, saved in the slots after those
  u32i  lend;      // the first of them lent to left operands kept across calls, up to regn
  u32i  busy;      // bit r: register r holds a local in scope (FREG + r: real register r)
  bool  leaf;      // the function calls nothing, as sift saw it: its locals may take the spare registers
  bool  bare;      // and they all have registers: no frame, no open
  bool  spoil;     // a call, a division or a frame slot proved sift wrong: the function is compiled again, framed, with no spares
  item* pick[SIFT]; // the names of the locals given a register
  u32i  picks;
  lexed* lexes;    // the tokens of the body being compiled, lexed by sift: chop replays them
  u32i  lexn, lexc, lexi, lexu; // lexi: the next one, lexu: their unit
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
  const char* dirs[16]; // where to look for libraries (-p)
  u32i  hookn, hookc;
  hook* hooks;
  bool  debug;     // -g: write debug information
  const char* cwd; // the directory the paths are relative to, NULL when unknown
  dbg*  dbg;       // what the pass recorded for it
  sidecar side[2]; // the files the form wants written beside its image
  u32i  sides;
};

void  make(mode*);
meta* build(mode*); // the source parsed, its units and libraries joined, calls bound: all but the image
void  dock(mode*, u64i at, u64i org); // lay data out at text + at, bss after it, text at org
u64i  gap(const mode*); // alignment of data and of bss after it
bool  alike(const void* x, const void* y, u64i size);
u32i  lay(mode*, u32i size, bool zero, u32i align);

#define home(self) ((self)->units + (self)->unit)
#define flaw(self, fmt, ...) \
  fail("%s:%llu: " fmt, home(self)->path, (unsigned long long)home(self)->line __VA_OPT__(,) __VA_ARGS__)
#define emit(self, call, ...) (self->arch.call(self->text __VA_OPT__(,) __VA_ARGS__))
#define nick(x) (int)(x)->size, (x)->name

// core.c, for link.c
// whether a call by name call that found fn picks an overload: fn is one, and
// call is not its symbol (dub), which calls it alone
#define plural(fn, call) ((fn)->lots && ((fn)->name->unit || !akin((fn)->name, call)))
meta* mint(mode*, meta x);
u32i  wide(const meta*);
u32i  quote(const item* s, byte* out);
void  bind(const mode*, byte* at, const item* name, const meta* func, u32i argc, bool ext);
bool  join(mode*, const char* path, u64i size, file* code, byte kind);
bool  land(mode*, char* path, u64i size, file* code, byte kind);

// dwarf.c: each does nothing without mode.debug
void  dwarf_line(mode*);                         // a statement starts here
void  dwarf_trim(mode*, const byte* mark);       // text rewound to mark
void  dwarf_open(mode*, u64i line);                // a function begins, called at line
void  dwarf_enter(mode*);
void  dwarf_leave(mode*);
void  dwarf_var(mode*, item* name, meta* type, s32i slot, bool parm, u64i line);
void  dwarf_done(mode*, const byte* from);       // an epilogue from there to here
void  dwarf_func(mode*, const meta* func, u64i line, const byte* lo); // its body is compiled
void  dwarf_glob(mode*, item* name, meta* type, u32i slot, bool own, u64i line);
void  dwarf_pack(mode*, u64i text, u64i data, u64i bss, dwarf* out);

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

