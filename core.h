// vim: ts=2:sw=2
#pragma once
#include <stdio.h>      /* FILE, printf     */
#include <stdlib.h>     /* malloc, free     */
#include <stdint.h>     /* uint32_t...      */

#define fail(...) (printf(__VA_ARGS__), exit(1))

typedef struct hash hash;
typedef struct item item;
struct item {
  char* key;
  int   len;
  void* val;
  item* next;
};

typedef void (*vent)();
typedef void (*txt)(void* dat);
typedef struct {
  vent  prep;
  vent  halt;
  vent  jump;
  vent  show;
  txt   text;
} emit;

typedef struct word word;
typedef struct {
  char* name;
  word* list;
  hash* pile;
  emit  arch;
} mode;

void  init(mode*  self);

void  hnew(hash** map);
item* hget(hash** map, char* key, long len);
#define sget(h, k) hget(h, k, sizeof(k)-1)
void  hrid(hash** map);

void  make(mode*);

