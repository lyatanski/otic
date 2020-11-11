// vim: ts=2:sw=2
#include <stdio.h>      /* FILE, printf */
#include <stdlib.h>     /* malloc, free */
#include <stdint.h>     /* uint32_t...  */

#define fail(...) (printf(__VA_ARGS__), exit(1))

typedef struct file file;
typedef struct word word;
typedef struct node node;

typedef struct hash hash;
typedef struct item item;
typedef struct item {
  char* key;
  int   len;
  void* val;
  item* next;
} item;

void  hnew(hash** map);
item* hget(hash** map, char* key, long len);
#define sget(h, k) hget(h, k, sizeof(k))
void  hrid(hash** map);

file* read(const char* name);
void drop(file* f);
word* chop(file* f, hash** h);
void sift(word* list, hash** map);

