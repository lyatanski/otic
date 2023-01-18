// vim: ts=2:sw=2
#pragma once
#include <stdio.h>  /* FILE, printf */
#include <stdlib.h> /* malloc, free */
#include <stdint.h> /* uint32_t,... */

typedef uint64_t u64i;
typedef  int64_t s64i;
typedef uint32_t u32i;
typedef  int32_t s32i;
typedef uint16_t u16i;
typedef  int16_t s16i;
typedef  uint8_t byte;
typedef  uint8_t bool;
//typedef     char char;

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
  uint8_t* head;
  uint8_t* back;
  uint8_t* tail;
  uint8_t* mend;
  size_t   size;
  uint8_t  data[1];
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
typedef void  (*pack)(void*, size_t);

typedef struct file file;

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

  uint8_t top;
  meta  decl[256];
};

void  make(mode*);
void  show(void*, size_t);

