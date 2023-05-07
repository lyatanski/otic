// vim: ts=2:sw=2
#pragma once
#include <stdio.h>  /* FILE, printf */
#include <stdlib.h> /* malloc, free, exit */
#include <stdint.h> /* standard data types */

typedef uint64_t u64i;
typedef  int64_t s64i;
typedef uint32_t u32i;
typedef  int32_t s32i;
typedef uint16_t u16i;
typedef  int16_t s16i;
typedef  uint8_t byte;
typedef  uint8_t bool;
//typedef     char char;
//typedef     void void;

#define warn(...) (printf(__VA_ARGS__))
#define fail(...) (printf(__VA_ARGS__), exit(1))

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
typedef void (*flow)(file*, ...);
typedef struct {
  flow  move;
  flow  when;
  flow  jump;
  flow  args;
  flow  echo;
  flow  done;
  flow  back;
  flow  calc;
} emit;

// core
typedef void* (*poke)(void*, size_t);
typedef void  (*pack)(file*);

typedef struct {
  u32i  kind;
  union {
    item* hash;
    u64i  ival;
    u64i  fval;
  };
} word;

typedef enum { base, list, data, call } what;
typedef struct meta meta;
struct meta {
  meta* type; // data link to its type
  item* name;
  byte* addr;
  u32i  size; // in case of list it is count of elements
  what  kind;
};

typedef struct {
  char* name;
  file* code; // source
  u64i  line;
  word  talk; // last
  hash  pool;
  emit  arch;

  file* text; // binary code
  file* data; // binary data
  u64i  zero; // zero initialized data

  poke  hold;
  poke  drop;
  pack  show;

  byte  top;
  meta  pile[256];
} mode;

void  make(mode*);
void  elfs(mode*);

