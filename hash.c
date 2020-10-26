// vim: ts=2:sw=2
#include "core.h"

#define INIT (1<<6)
#define MARK 70
#define BARE 50

unsigned long pile(unsigned long all, unsigned char new)
{
    return all ? (all * 0x100000001b3) ^ new : 0xcbf29ce484222325;
}

void hget(hash* map, char* key)
{
}

void hput(hash* map, char* key, void* dat)
{
  if (!map->cask) {
    map->cask = malloc(INIT * sizeof(item));
    map->size = INIT;
  } else if ((map->used * 100) / map->size >= MARK) {
    //rehash(map);
  }
}

void hdel(hash* map, char* key)
{
}
