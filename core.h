// vim: ts=2:sw=2
#pragma once
#include <stdio.h>      /* FILE, printf     */
#include <stdlib.h>     /* malloc, free     */
#include <stdint.h>     /* uint32_t...      */

#define POOL (1 << 13)
#define fail(...) (printf(__VA_ARGS__), exit(1))

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
  uint8_t* body;
  uint8_t* tail;
  uint8_t* mend;
  size_t   size;
  uint8_t  data[1];
} buff;

// arch
typedef void (*vent)();
typedef void (*flow)(buff*, uint32_t);
typedef void (*mend)(buff*);
typedef void (*txt)(void* dat);
typedef struct {
  vent  prep;
  vent  halt;
  vent  show;
  txt   text;

  flow  move;
  flow  when;
  flow  jump;
  mend  back;
} emit;

// core
typedef struct file file;
typedef struct {
  int   kind;
  union {
    item*    hash;
    uint64_t ival;
    uint64_t fval;
  };
} word;
struct mode{
  char* name;
  file* code;
  word  talk;
  hash  pool;
  emit  arch;
};

//void  init(mode*);
void  make(mode*);

