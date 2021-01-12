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

void ia64_mend(buff* data) { *data->back = data->tail - data->back - 1; }
void ia64_done(buff* data) { ib(data, ret); }

void ia64_jump(buff* data, uint32_t fix) {
  ib(data, jmp);
  if(fix) *data->back = data->tail - data->back;
  data->back = data->tail++;
}

void ia64_when(buff* data, uint32_t cond) {
  ib(data, cmp);
  ib(data, modrm(.mod=3, .reg=7, .rm=eax));
  ib(data, 0);

  ib(data, jcs + je);
  data->back = data->tail++;
}

emit ia64 = {
  .move = ia64_move,
  .when = ia64_when,
  .jump = ia64_jump,
  .done = ia64_done,
  .back = ia64_mend,
};

