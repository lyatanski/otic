// vim: ts=2:sw=2
#include <stdio.h>      /* FILE, printf */
#include <stdlib.h>     /* malloc, free */

#define fail(...) (printf(__VA_ARGS__), exit(1))

typedef struct {
  char* key;
  int   len;
  void* val;
} item;

typedef struct {
  item* cask;
  int   size;
  int   used;
} hash;

void hget(hash* map, char* key);
void hput(hash* map, char* key, void* dat);
void hdel(hash* map, char* key);

unsigned long pile(unsigned long full, unsigned char c);
