// vim: ts=2:sw=2
#pragma once
#include <stdio.h>  /* FILE, printf */
#include <stdlib.h> /* malloc, free, exit */
#include <stdint.h> /* standard data types */
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

// hash
#define POOL (1 << 13)
typedef struct {
  u64i  size;
  char* name;
  void* data;
} item;
typedef item hash[POOL];

typedef struct {
  byte*  body;
  byte*  tail;
  byte   head[1];
} file;

// arch
typedef enum { cpu_ia64, cpu_arm8 } chip;
#define WIDE (1 << 24) // calc op flag: on 64 bits, for pointers and long
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
  chip  cpu;  // target, each form maps it to its own machine id
} emit;

// core
typedef struct mode mode;
typedef void* (*poke)(void*, size_t);
typedef void  (*pack)(mode*);

// container
typedef struct {
  file* (*pack)(mode*); // lay text/data out as an output image
} form;

typedef struct {
  u32i  kind;
  union {
    item* hash;
    u64i  ival;
    u64i  fval;
  };
} word;

// base and list (struct) are types, data a local, glob a global, call a function,
// refs a pointer type, many an array type
typedef enum { base, list, data, call, glob, refs, many } what;
typedef struct meta meta;
struct meta {
  meta* type; // data link to its type, what refs points to, what many holds,
              // the first member of a list
  meta* hide; // what the name meant before, restored when the scope ends; the
              // next member of a list
  item* name;
  byte* addr;
  u32i  size; // bytes of a type, parameters of a call
  s32i  slot; // frame offset of a local, data offset of a global, offset of a
              // member, alignment of a list
  what  kind;
};

// call to a function not seen yet, patched when the run ends
typedef struct {
  item* name;
  byte* at;   // NULL when the call was dropped as dead code
  u32i  argc;
  u64i  line;
} fix;

// address of a global (data offset, or bss offset | ZERO), patched by dock
typedef struct {
  byte* at;
  u32i  off;
} seat;

struct mode {
  char* name;
  char* outp; // output
  file* code; // source
  u64i  line;
  word  talk; // last
  hash  pool;
  emit  arch;
  form  form;

  file* text; // binary code
  byte* main; // entry of main in text
  file* data; // binary data
  u64i  zero; // zero initialized data (bss) bytes
  meta* rune; // char: what a string literal points to

  poke  hold;
  poke  drop;
  pack  show;

  u32i  top;
  meta  pile[256];
  s32i  slot; // frame bytes in use
  s32i  deep; // frame bytes the function needs
  meta* func; // the function being compiled
  byte* cont; // continue target, NULL outside loops
  u32i  ends; // pending break jumps
  byte* quit[64];
  u32i  fixs;
  fix   link[256];
  u32i  seats;
  seat  aims[1024];
  u32i  defn;
  meta  defs[512]; // pointer, array and struct types, members
};

void  make(mode*);
void  dock(mode*, u64i at); // lay data out at text + at, bss after it

