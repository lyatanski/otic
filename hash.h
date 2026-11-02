// vim: ts=2:sw=2
#pragma once
// the symbol tables, included by core.h after the integer types

typedef struct unit unit;
typedef struct mode mode;

// a name in the table of a unit, and what it means there
typedef struct item item;
struct item {
  u64i  size;
  char* name;
  void* data;
  item* next; // the same spelling in the table of a later unit (mode.names)
  u32i  unit; // the unit whose table it is in
};

u64i  clue(char* mark, u64i size);
item* hsee(unit*, char* mark, u64i size, u64i clue); // NULL when not in
item* hget(mode*, u32i at, char* mark, u64i size);   // in unit at, made when not in
