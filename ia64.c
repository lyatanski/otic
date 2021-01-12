#include "core.h"

typedef enum {
  eax = 0
} ia64_regs;

typedef enum {
  mov = 0xb8,
  ret = 0xc3,
  cmp = 0x83,
  jmp = 0xeb,
  jcs = 0x70,
  jcl = 0x0f80
} ia64_inst;

typedef enum {
  jo, jno, jb, jae, je, jne, jbe, ja, js, jns, jp, jnp, jl, jge, jle, jg
} ia64_cond;

#define rex(...) (*(uint8_t*)&(struct { uint8_t b:1, x:1, r:1, w:1, _:4; }){._=4, __VA_ARGS__})
#define modrm(...) (*(uint8_t*)&(struct { uint8_t rm:3, reg:3, mod:2; }){__VA_ARGS__})
#define sib(...) (*(uint8_t*)&(struct { uint8_t base:3, idx:3, ss:2; }){__VA_ARGS__})

#define BODY { *(typeof(val)*)data->tail = val; data->tail += sizeof(val); }
void ib(buff* data, uint8_t  val) BODY
void iw(buff* data, uint16_t val) BODY
void id(buff* data, uint32_t val) BODY
void io(buff* data, uint64_t val) BODY
#undef  BODY

void ia64_move(buff* data, uint32_t num) {
  ib(data, mov + eax);
  id(data, num);
}

void ia64_mend(buff* data) { *data->body = data->tail - data->body - 1; }

void ia64_jump(buff* data, uint32_t fix) {
  ib(data, jmp);
  if(fix) *data->body = data->tail - data->body;
  data->body = data->tail++;
}

void ia64_when(buff* data, uint32_t cond) {
  ib(data, cmp);
  ib(data, modrm(.mod=3, .reg=7, .rm=eax));
  ib(data, 0);

  ib(data, jcs + je);
  data->body = data->tail++;
}

void ia64_emit() {
  uint8_t exp[] = { 0xB8, 0x03, 0x00, 0x00, 0x00, 0x83, 0xF8, 0x00, 0x74, 0x07, 0xB8, 0x04, 0x00, 0x00, 0x00, 0xEB, 0x05, 0xB8, 0x08, 0x00, 0x00, 0x00, 0xB8, 0x2A, 0x00, 0x00, 0x00, 0xC3 };
  //uint8_t exp[] = { 0xB8, 0x03, 0x00, 0x00, 0x00, 0x83, 0xF8, 0x00, 0x74, 0x05, 0xB8, 0x04, 0x00, 0x00, 0x00, 0xB8, 0x2A, 0x00, 0x00, 0x00, 0xC3 };
  buff* b;
  b = malloc(sizeof(buff)+1024);
  b->tail = b->head = b->data;
  b->size = 0;

  printf("i386\n");

  ia64_move(b, 3);
  ia64_when(b, 0);

  // for earch instruction in then
  ia64_move(b, 4);

  if(1) {
    ia64_jump(b, 1);

    // for earch instruction in else
    ia64_move(b, 8);
  }
  ia64_mend(b);

  ia64_move(b, 42);
  ib(b, ret);

  int size = b->tail - b->head;
  if(size != sizeof(exp))
    printf("encoding size mismatch, got %d, exp %ld\n", size, sizeof(exp));
  for(int i = 0; i < size; i++)
    if(b->head[i] != exp[i]) printf("---difference in %d, got %2.2x, exp %2.2x\n", i, b->head[i], exp[i]);
  for(int i = 0; i < sizeof(exp); i++)
    printf("%2.2d ", i);
  printf("\n");
  for(int i = 0; i < sizeof(exp); i++)
    printf("%2.2x ", exp[i]);
  printf("\n");
  for(int i = 0; i < size; i++)
    printf("%2.2x ", b->head[i]);
  printf("\n");

  free(b);
}

void ia64_text(void* data) {
  buff b;
  b.tail = b.head = data;

  //mov eax, 42
  ib(&b, mov+eax);
  id(&b, 42);

  //ret
  ib(&b, ret);
}

emit ia64 = {
  .show = ia64_emit,
  .text = ia64_text,

  .move = ia64_move,
  .when = ia64_when,
  .jump = ia64_jump,
  .back = ia64_mend,
};

