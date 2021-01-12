#include "core.h"

#define eax 0

#define mov    0xb8
#define ret    0xc3
#define cmp    0x83
#define jmp    0xeb
#define jcs    0x70
#define jcl    0x0f80
#define    jo  0x0
#define    jno 0x1
#define    jb  0x2
#define    jae 0x3
#define    je  0x4
#define    jne 0x5
#define    jbe 0x6
#define    ja  0x7
#define    js  0x8
#define    jns 0x9
#define    jp  0xa
#define    jnp 0xb
#define    jl  0xc
#define    jge 0xd
#define    jle 0xe
#define    jg  0xf

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
  uint8_t* exe = data;
  int size = 0;

  //mov eax, 42
  exe[size++] = mov + eax;
  //id(exe+size, 42);

  //ret
  exe[size++] = ret;
}

emit ia64 = {
  .show = ia64_emit,
  .text = ia64_text,

  .move = ia64_move,
  .when = ia64_when,
  .jump = ia64_jump,
  .back = ia64_mend,
};

