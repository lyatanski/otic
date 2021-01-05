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

// arch
typedef void (*vent)();
typedef void (*txt)(void* dat);
typedef struct {
  vent  prep;
  vent  halt;
  vent  jump;
  vent  show;
  txt   text;
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
typedef struct {
  char* name;
  file* code;
  word  talk;
  hash  pool;
  emit  arch;
} mode;

//void  init(mode*);
void  make(mode*);

