// vim: ts=2:sw=2
#pragma once
#include <stdio.h>      /* FILE, printf     */
#include <stdlib.h>     /* malloc, free     */
#include <stdint.h>     /* uint32_t...      */

#define fail(...) (printf(__VA_ARGS__), exit(1))

// hash
typedef struct hash hash;
typedef struct item item;
struct item {
  char* key;
  int   len;
  void* val;
  item* next;
};

void  hnew(hash** map);
item* hget(hash** map, char* key, long len);
#define sget(h, k) hget(h, k, sizeof(k)-1)
void  hrid(hash** map);

typedef enum {
  unit,
  _val, _add, _sub, _mul, _div, _mod,
  when, jump, call, proc, give,
  ops,
} spec;

typedef union {
  item*    hash;
  int64_t  ival;
  uint64_t fval;
} form;


typedef struct node node;
struct node {
  spec  kind;
  node* then;
  node* next;
  form  data;
};

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
typedef struct word word;
typedef struct {
  char* name;
  word* list;
  hash* pile;
  node* head;
  node* tail;
  emit  arch;
} mode;

void  init(mode*);
void  make(mode*);

