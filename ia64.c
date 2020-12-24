#include "core.h"
#include <stdio.h>

void emit_i386() {
  printf("i386\n");
}

emit i386 = {.show = emit_i386};

