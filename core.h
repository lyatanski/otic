// vim: ts=2:sw=2
#pragma once
#include <stdio.h>      /* FILE, printf     */
#include <stdlib.h>     /* malloc, free     */
#include <stdint.h>     /* uint32_t...      */

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
typedef void (*flow)(buff*, uint32_t);
typedef void (*mend)(buff*);
typedef struct {
  flow  move;
  flow  when;
  flow  jump;
  flow  args;
  flow  call;
  mend  done;
  mend  back;
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
  union {
    struct {
      item*    type;
      item*    next; // for function parameters, alt names elem, prop
      uint8_t* code; // for function
      uint32_t size; // for array
    };
    struct {
      uint8_t  _const   :1, _signed  :2, _static  :1,
               _extern  :1, _inline  :1, _thread  :1, _pointer :1;
      uint16_t _void    :1, _bool    :1, _char    :1,
               _short   :1, _long    :2, _number  :1,
               _float   :1, _double  :1;
    };
  };
} meta;

struct mode{
  char* name;
  file* code;
  word  talk;
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

