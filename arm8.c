#include "core.h"
#include <stdio.h>

typedef union {
  struct { uint32_t rd :5, i16:16, hw :2, op :6, opc:2, sf :1; };
  struct { uint32_t op4:5, rn : 5, op3:6, op2:5, opc:4, op :7; } br;
  uint32_t n;
} i;

void emit_arm8() {
  printf("armv8\n");
  //mov  w0, #0x2a
  printf("%x\n", (i){.op=0b100101, .opc=2, .i16=42}.n);
  //ret
  printf("%x\n", 0xd65f03c0);
}

emit arm8 = {.show = emit_arm8};

