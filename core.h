// vim: ts=2:sw=2
#pragma once
#include <stdio.h>  /* FILE, printf */
#include <stdlib.h> /* malloc, free */
#include <stdint.h> /* uintXX_t,... */

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

#define fail(...) (printf(__VA_ARGS__), exit(1))
#define POOL (1 << 13)

// hash
typedef struct item item;
struct item {
  char* key;
  int   len;
  void* val;
  item* next;
};
typedef item hash[POOL];

item* hget(hash map, char* key, long len);
#define sget(self, k) hget(self->pool, k, sizeof(k)-1)
void  hrid(hash map);

typedef struct {
  //char*  name;
  char*  head;
  char*  body;
  char*  tail;
  size_t size;
  size_t line;
  char   data[1];
} file;

typedef struct {
  byte* head;
  byte* back;
  byte* tail;
  size_t size;
  byte  data[1];
} buff;

// arch
typedef void (*flow)(buff*, ...);
typedef struct {
  flow  move;
  flow  when;
  flow  jump;
  flow  args;
  flow  echo;
  flow  done;
  flow  back;
} emit;

// core
typedef void* (*poke)(void*, size_t);
typedef void  (*pack)(buff*);

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
  item* name;
  byte* addr;
  u32i  size; // in case of list it is count of elements
  what  kind;
  meta* link; // data link to its type
};

typedef struct {
  char* name;
  file* code;
  word  talk; // last
  hash  pool;
  emit  arch;
  buff  data;
  poke  hold;
  poke  drop;
  pack  exec;

  byte  top;
  meta  decl[256];
} mode;

void  make(mode*);
file* scan(char*);

void  elfs(mode*);

