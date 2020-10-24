// vim: ts=2:sw=2
#include <stdio.h>      /* FILE, printf...   */
#include <stdlib.h>     /* malloc, free, rand */
#include <string.h>     /* memcmp, strlen */
#include <stdarg.h>     /* va_list, va_start, va_arg, va_end */

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

