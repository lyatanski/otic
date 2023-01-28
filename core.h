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
#define sget(h, k) hget(h, k, sizeof(k)-1)
void  hrid(hash map);

typedef struct mode mode;
typedef struct {
  byte* head;
  byte* back;
  byte* tail;
  byte* mend;
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
  //char*  name;
  char*  head;
  char*  body;
  char*  tail;
  size_t size;
  size_t line;
  char   data[1];
} file;

typedef struct {
  int   kind;
  union {
    item*    hash;
    uint64_t ival;
    uint64_t fval;
  };
} word;

typedef enum { variable, function, typedecl, _struct, _union } name_kind;
typedef struct {
  name_kind kind;
  uint8_t   _const   :1, _static  :1, _extern  :1,
            _signed  :2, _inline  :1, _thread  :1, _pointer :1;
  item*     type;
  item*     next; // for function parameters, alt names elem, prop
  uint8_t*  code; // for function
  uint32_t  size; // for total size
  //bool      used; // indicates unused variables
  //uint32_t  more; // for element count, no need for next as they will be following in the array
} meta;

typedef struct {
  u32i  name; // hash index
  u32i  kind; // one of: type, list, suit, data, call
  u32i  link; // data link to its type
  u32i  addr;
  u32i  size; // in case of list it is count of elements
} symb;

struct mode {
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
};

void  make(mode*);
file* scan(char*);

void  elfs(mode*);

