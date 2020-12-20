#include "core.h"
#include <stdio.h>

void emit_arm8() {
  printf("armv8\n");
}

emit arm8 = {.show = emit_arm8};

