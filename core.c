// vim: ts=2:sw=2
#include "core.h"

enum { tend, ttag, tnum };
#define TEXT (1 << 16)
#define DATA (1 << 16)
#define tok1(x) ((x)+=1, tokd((x)[-1]))
#define tok2(x) ((x)+=2, tokd((x)[-2], (x)[-1]))
#define tok3(x) ((x)+=3, tokd((x)[-3], (x)[-2], (x)[-1]))

byte* copy(byte* dst, void* src, u64i size) {
  for(byte* s = src; size--; *dst++ = *s++);
  return dst;
}

u64i  slen(char* s) {
  u64i n = 0;
  while(s[n]) n++;
  return n;
}

void* swell(void* p, u32i n, u32i* cap, u64i size, u32i least) {
  if(n < *cap) return p;
  *cap = *cap * 2 + least;
  if(!(p = realloc(p, *cap * size))) fail("out of memory\n");
  return p;
}

bool  alike(void* x, void* y, u64i size) {
  byte *a = x, *b = y;
  for(; size && *a == *b; size--, a++, b++);
  return !size;
}

// string hash, computed once for every table a name is looked up in
u64i  clue(char* mark, u64i size) {
  u64i clue = 0x01;
  for(u64i i = 0; i < size; clue += (clue << 5) + (clue >> 27) + mark[i++]);
  return clue;
}

// the slot of the name in the table, or the free slot where it goes; NULL
// when 10 probes fail. The step is odd: it reaches every slot
static item** hpos(unit* self, char* mark, u64i size, u64i clue) {
  u64i step = (clue >> 16) | 1;
  for(u64i i = 0; i < 10 && self->size; i++) {
    item** at = self->pool + ((clue + i * step) & (self->size - 1));
    if(!*at || ((*at)->size == size && alike((*at)->name, mark, size))) return at;
  }
  return NULL;
}

item* hsee(unit* self, char* mark, u64i size, u64i clue) {
  item** at = hpos(self, mark, size, clue);
  return at? *at: NULL;
}

// double the table, again until each item is within 10 probes
static void  hgrow(unit* self) {
  item** pool = self->pool;
  u32i   size = self->size, i;
  self->size = size? size: 32;
  do {
    self->size *= 2;
    self->pool = calloc(self->size, sizeof(item*));
    if(!self->pool) fail("out of memory\n");
    for(i = 0; i < size; i++) {
      if(!pool[i]) continue;
      item** at = hpos(self, pool[i]->name, pool[i]->size, clue(pool[i]->name, pool[i]->size));
      if(!at) break;
      *at = pool[i];
    }
    if(i < size) free(self->pool);
  } while(i < size);
  free(pool);
}

// the table grows when 3/4 full or when 10 probes fail. Items are allocated
// apart from it and never move: metas, words and fixes point to them
static item* hput(unit* self, char* mark, u64i size, u64i k) {
  static item* heap;
  static u32i  left;
  item** at = hpos(self, mark, size, k);
  if(at && *at) return *at;
  while(!at || 4 * (self->used + 1) > 3 * self->size) {
    hgrow(self);
    at = hpos(self, mark, size, k);
  }
  if(!left) {
    heap = calloc(left = 256, sizeof(item));
    if(!heap) fail("out of memory\n");
  }
  self->used++;
  *at = heap + --left;
  **at = (item){.size = size, .name = mark};
  return *at;
}

// a new item also joins the items of its spelling, in join order, so that
// find visits only the units that have the name
item* hget(mode* self, u32i at, char* mark, u64i size) {
  u64i  k = clue(mark, size);
  unit* u = self->units + at;
  u32i  used = u->used;
  item* s = hput(u, mark, size, k);
  if(u->used == used) return s;
  item** p = (item**)&hput(&self->names, mark, size, k)->data;
  for(; *p && (*p)->unit < at; p = &(*p)->next);
  s->unit = at;
  s->next = *p;
  return *p = s;
}

void chop(mode* self) {
  s64i  ival = 0;
  s64i  fval = 0;
  u32i  kind = tend;
  byte* c = self->code->body;

  pass: switch(*(self->code->body = c)) {
    case '\n': self->line++; // fallthrough
    case ' ': case '\f': case '\t': case '\v': case '\r': c++; goto pass;
    case '#': case ',': case ';': case ':': case '?': case '~': case '@':
    case '(': case ')': case '[': case ']': case '{': case '}':
      kind = tok1(c); break;
    case '=': case '!': case '*': case '%': case '^':
      kind = c[1] == '='? tok2(c): tok1(c); break;
    case '&':
      kind = c[1] == '=' || c[1] == '&'? tok2(c): tok1(c); break;
    case '|':
      kind = c[1] == '=' || c[1] == '|'? tok2(c): tok1(c); break;
    case '+':
      kind = c[1] == '=' || c[1] == '+'? tok2(c): tok1(c); break;
    case '-':
      kind = c[1] == '=' || c[1] == '-' || c[1] == '>'? tok2(c): tok1(c); break;
    case '<':
      kind = c[1] == '<' && c[2] == '='? tok3(c):
             c[1] == '<' || c[1] == '='? tok2(c): tok1(c); break;
    case '>':
      kind = c[1] == '>' && c[2] == '='? tok3(c):
             c[1] == '>' || c[1] == '='? tok2(c): tok1(c); break;
    case '/':
      switch(c[1]) {
        case '/': while(*c && *c != '\n') c++; goto pass;
        case '*':
          for(c += 2; *c && (c[0] != '*' || c[1] != '/'); c++)
            self->line += *c == '\n';
          if(!*c) flaw(self, "unterminated comment\n");
          c += 2; goto pass;
        default: kind = (c[1] == '='? tok2(c): tok1(c));
      }
      break;
    //case '\'':
    //  kind = '"'; c+=2; break;
    case '"':
      for(c++; *c != '"'; c++) {
        if(!*c || *c == '\n') flaw(self, "unterminated string\n");
        if(*c == '\\') c++;
        //if(*c == '\\') switch (*++c) {
        //  case 'a': '\a'; break;
        //  case 'b': '\b'; break;
        //  case 'f': '\f'; break;
        //  case 'n': '\n'; break;
        //  case 't': '\t'; break;
        //  case 'v': '\v'; break;
        //  case 'r': '\r'; break;
        //  default: break;
        //}
      }
      c++;
      kind = '"'; break;
    case '.':
      // '.', '...' or number
      if     (c[1] == '.' && c[2] == '.') { kind = tok3(c); break; }
      else if(c[1] <  '0' || c[1] >  '9') { kind = tok1(c); break; }
      // else fall trough to number
    case '0' ... '9':
      while(*c >= '0' && *c <= '9') ival = ival * 10 + *c++ - '0';
      if(*c == '.')
        while(*c++ && (*c >= '0' && *c <= '9')) fval = fval * 10 + *c - '0';
      else if(*c == 'B' || *c == 'b')
        while(*c++ && (*c == '0' || *c == '1')) ival = ival *  2 + *c - '0';
      else if(*c == 'O' || *c == 'o')
        while(*c++ && (*c >= '0' && *c <= '7')) ival = ival *  8 + *c - '0';
      else if(*c == 'X' || *c == 'x')
        while(*c++) {
          if     (*c >= '0' && *c <= '9') ival = ival * 16 + *c - '0';
          else if(*c >= 'A' && *c <= 'F') ival = ival * 16 + *c - 'A' + 10;
          else if(*c >= 'a' && *c <= 'f') ival = ival * 16 + *c - 'a' + 10;
          else break;
        }
      kind = tnum; break;
    case '_': case 'a' ... 'z': case 'A' ... 'Z':
      while((*c >= 'a' && *c <= 'z') || (*c >= 'A' && *c <= 'Z')
         || (*c >= '0' && *c <= '9') || *c == '_') c++;
      kind = ttag; break;
    case '\0': break;
    default: flaw(self, "unexpected character '%c'\n", *c);
  }

  if(kind == '"' || kind == ttag) {
    self->talk.hash = hget(self, self->unit, (char*)self->code->body, c - self->code->body);
  }
  else
    self->talk.ival = kind == tnum? ival: 0;
  self->talk.kind = kind;
  self->code->body = c;
}

#define same(self, name) (\
  (self)->talk.kind == ttag &&\
  (self)->talk.hash == sget((self), (name)))

bool  twin(mode* self, u32i  kind) {
  return (self->talk.kind == kind)? chop(self), 1 : 0;
}

bool  skip(mode* self, int   kind) {
  if(!twin(self, kind))
    flaw(self, "expected %x, got %x\n", kind, self->talk.kind);
  return 1;
}

int rank(int op) {
  switch (op) {
    default:                                                      return  0;
    case tokd('|', '|'):                                          return  1;
    case tokd('&', '&'):                                          return  2;
    case '|':                                                     return  3;
    case '^':                                                     return  4;
    case '&':                                                     return  5;
    case tokd('=', '='): case tokd('!', '='):                     return  6;
    case '<': case tokd('<', '='): case '>': case tokd('>', '='): return  7;
    case tokd('<', '<'): case tokd('>', '>'):                     return  8;
    case '+': case '-':                                           return  9;
    case '*': case '/': case '%':                                 return 10;
  }
}

// types: NULL is an int value
meta* mint(mode* self, meta x) {
  if(self->defn == sizeof self->defs / sizeof *self->defs)
    flaw(self, "too many types\n");
  self->defs[self->defn] = x;
  return self->defs + self->defn++;
}

// a pointer or array type of size bytes over type, made once
meta* knot(mode* self, what kind, meta* type, u32i size) {
  for(meta* x = self->defs; x < self->defs + self->defn; x++)
    if(x->kind == kind && x->type == type && x->size == size) return x;
  return mint(self, (meta){.kind = kind, .type = type, .size = size});
}

// pointer to type
meta* href(mode* self, meta* type) { return knot(self, refs, type, 8); }

// what a pointer points to or an array holds, NULL for the rest
meta* elem(meta* t) { return t && (t->kind == refs || t->kind == many)? t->type: NULL; }
u32i  wide(meta* t) { return elem(t) || (t && t->kind == base && t->size == 8)? WIDE: 0; }
u32i  span(meta* t) { return t? t->size: 4; }
u32i  lean(meta* t) { return t->kind == many? lean(t->type): t->kind == list? (u32i)t->slot: t->size; }

// deferred operand: code is emitted only when the value must be in acc
typedef enum { vacc, vnum, vloc, vglo, vmem } vkind;
// ival: value, frame slot or data offset; vmem: its address is in acc
typedef struct { vkind kind; s32i ival; meta* type; } oper;
#define cnst(n) ((oper){vnum, (n), NULL})
#define acc(t)  ((oper){vacc, 0, (t)})

// size bytes of data, or of bss when zero: their offset
u32i  lay(mode* self, u32i size, bool zero, u32i align) {
  if(zero) {
    self->zero = ((self->zero + align - 1) & -align) + size;
    return (self->zero - size) | ZERO;
  }
  file* d = self->data;
  u32i  off = ((d->body - d->head) + align - 1) & -align;
  if(off + size > (u64i)(d->tail - d->head)) fail("data section overflow\n");
  d->body = d->head + off + size;
  return off;
}

// acc = address of data at off
void  spot(mode* self, u32i off) {
  if(self->seats == sizeof self->aims / sizeof *self->aims)
    flaw(self, "too many global references\n");
  self->aims[self->seats++] = (seat){self->text->body, off};
  emit(self, glob);
}

// the bytes of string literal s and a NUL, into out when given: their count
u32i  quote(item* s, byte* out) {
  u32i n = 0;
  for(char* c = s->name + 1; c < s->name + s->size - 1; c++, n++) {
    char v = *c;
    if(v == '\\') v = (v = *++c) == 'n'? '\n': v == 't'? '\t': v == 'r'? '\r': v == '0'? 0: v;
    if(out) out[n] = v;
  }
  if(out) out[n] = 0;
  return n + 1;
}

oper  load(mode* self, oper x) {
  meta* t = x.type;
  if(t && t->kind == many) { // an array is the address of its first element
    if(x.kind == vloc) emit(self, addr, x.ival);
    if(x.kind == vglo) spot(self, x.ival);
    return acc(href(self, t->type));
  }
  if(x.kind == vacc || x.kind == vnum) {
    if(x.kind == vnum) emit(self, move, x.ival);
    return acc(t);
  }
  if(t && (!t->size || t->kind == list)) flaw(self, "not a value\n");
  if(x.kind == vloc) emit(self, load, x.ival, span(t));
  if(x.kind == vglo) spot(self, x.ival);
  if(x.kind != vloc) emit(self, read, span(t));
  return acc(t);
}

// &x
oper  refr(mode* self, oper x) {
  if(x.kind == vloc) emit(self, addr, x.ival);
  if(x.kind == vglo) spot(self, x.ival);
  if(x.kind == vnum || x.kind == vacc) flaw(self, "cannot take the address\n");
  return acc(href(self, x.type));
}

// *x
oper  star(mode* self, oper x) {
  meta* t = elem(x.type);
  if(!t) flaw(self, "not a pointer\n");
  load(self, x);
  return (oper){vmem, 0, t};
}

// x.name
oper  memb(mode* self, oper x) {
  meta* m = x.type && x.type->kind == list? x.type->type: NULL;
  if(!m || self->talk.kind != ttag) flaw(self, "expected a member\n");
  for(; m && !akin(m->name, self->talk.hash); m = m->hide); // m->name may be another unit's
  if(!m) flaw(self, "no member '%.*s'\n", nick(self->talk.hash));
  chop(self);
  if(x.kind != vmem) x.ival += m->slot;
  else if(m->slot) emit(self, calc, '+' | WIDE, 1, m->slot);
  x.type = m->type;
  return x;
}

// rewind text to mark: calls in the dropped code are checked but not patched
void  trim(mode* self, byte* mark) {
  self->text->body = mark;
  for(u32i i = self->fixs; i-- && self->link[i].at >= mark; ) self->link[i].at = NULL;
  while(self->seats && self->aims[self->seats - 1].at >= mark) self->seats--;
}

// int arithmetic as the targets do it: 32-bit wrap, shift count mod 32
s32i  fold(u32i op, s32i a, s32i b) {
  u32i x = a, y = b;
  switch(op) {
    case '+':            return x + y;
    case '-':            return x - y;
    case '*':            return x * y;
    case '/':            return (s64i)a / b; // 64-bit: INT_MIN / -1 must not trap
    case '%':            return (s64i)a % b;
    case '&':            return a & b;
    case '|':            return a | b;
    case '^':            return a ^ b;
    case tokd('<', '<'): return x << (y & 31);
    case tokd('>', '>'): return a >> (y & 31);
    case '<':            return a <  b;
    case tokd('<', '='): return a <= b;
    case '>':            return a >  b;
    case tokd('>', '='): return a >= b;
    case tokd('=', '='): return a == b;
    default:             return a != b;
  }
}

oper  expr(mode* self, int least);
void  eval(mode* self);

// add a forward call: name, called at at with argc arguments, the C way when ext
void  post(mode* self, item* name, byte* at, u32i argc, bool ext) {
  if(self->fixs == sizeof self->link / sizeof *self->link) flaw(self, "too many forward calls\n");
  self->link[self->fixs++] = (fix){name, at, argc, self->line, self->unit, ext};
}

// point the call at to func, the function name with argc arguments; a call
// the C way (ext) was checked against its prototype by cabi
void  bind(mode* self, byte* at, item* name, meta* func, u32i argc, bool ext) {
  if(!func || (func->kind != call && func->kind != pend))
    flaw(self, "no function '%.*s'\n", nick(name));
  if(!ext && func->ext) flaw(self, "'%.*s' is C code, link it before the call\n", nick(name));
  if(!ext && func->size != argc)
    flaw(self, "'%.*s' takes %d arguments, got %d\n", nick(name), func->size, argc);
  if(!func->addr && ext) flaw(self, "'%.*s' is not defined\n", nick(name));
  if(!func->addr) flaw(self, "'%.*s' is called before its declaration\n", nick(name));
  if(at) emit(self, link, at, func->addr);
}

// a call to C code, its argc arguments pushed: through the slot of an import,
// or straight to a function of an object. One in an archive member not pulled
// yet is asked for, and the call bound when the member is in. C leaves the
// bytes of a result past its size undefined: an int is cut to 32 bits, a char
// or short masked
oper  cabi(mode* self, item* name, meta* func, u32i argc) {
  if(func->size != VARY && (argc < func->size || (argc > func->size && !func->more)))
    flaw(self, "'%.*s' takes %d arguments, got %d\n", nick(name), func->size, argc);
  meta* body = func->lib || func->addr? func: find(self, 0, name); // past a prototype
  if(body && body->kind == pend) ask(self, body);
  bool  wait = !func->lib && (!body || !body->addr);
  if(func->lib) spot(self, port(self, func));
  byte* at = emit(self, ffi, func->lib? NULL: wait? self->text->head: body->addr,
                  argc, func->more? func->size: argc, self->form.os);
  if(wait) post(self, name, at, argc, 1);
  u32i n = span(func->type);
  if(n && n < 4) emit(self, calc, '&', 1, (1 << n * 8) - 1);
  if(n == 4) emit(self, cast, 0);
  return acc(func->type);
}

// name(args): each argument is pushed on 64 bits, then popped into its
// register; syscall(number, args) goes to the kernel
oper  dial(mode* self, item* name) {
  bool  trap = name == sget(self, "syscall");
  meta* func = find(self, self->unit, name);
  u32i  argc = 0;
  for(; !twin(self, ')'); argc++) {
    if(argc) skip(self, ',');
    if(argc == 6u + trap) flaw(self, "more than 6 arguments\n");
    if(!wide(load(self, expr(self, 1)).type)) emit(self, cast, 1); // a long parameter
    emit(self, push);
  }
  if(trap) {
    if(!argc) flaw(self, "syscall without a number\n");
    emit(self, trap, argc);
    return acc(NULL);
  }
  if(func && (func->kind == pend || (func->kind == call && func->ext)))
    return cabi(self, name, func, argc);
  for(u32i i = argc; i--; ) emit(self, args, i);
  byte* at = self->text->body;
  emit(self, dial);
  if(func) bind(self, at, name, func, argc, 0);
  else post(self, name, at, argc, 0);
  return acc(func? func->type: NULL);
}

// x before its right operand is parsed: a value in acc is pushed after the mark
byte* keep(mode* self, oper* x) {
  if(x->kind == vmem) *x = load(self, *x);
  byte* mark = self->text->body;
  if(x->kind == vacc) emit(self, push);
  return mark;
}

oper  pair(mode* self, u32i op, oper x, oper y, byte* mark);

oper  atom(mode* self) {
  u32i op = self->talk.kind;
  oper x;
  switch(op) {
    case tnum: x = cnst(self->talk.ival); chop(self); return x;
    case '"': {
      u32i off = lay(self, self->talk.hash->size - 1, 0, 8);
      quote(self->talk.hash, self->data->head + off);
      chop(self);
      spot(self, off);
      x = acc(href(self, self->rune));
      break;
    }
    case ttag: {
      item* name = self->talk.hash;
      chop(self);
      if(twin(self, '(')) { x = dial(self, name); break; }
      meta* var = find(self, self->unit, name);
      if(var && var->kind == pend) { x = (oper){vglo, WANT | ask(self, var), NULL}; break; }
      if(!var || (var->kind != data && var->kind != glob))
        flaw(self, "no variable '%.*s'\n", nick(name));
      x = (oper){var->kind == data? vloc: vglo, var->slot, var->type};
      if(var->ext) { spot(self, port(self, var)); emit(self, read, 8); x = (oper){vmem, 0, var->type}; }
      break;
    }
    case '(':  chop(self); x = expr(self, 1); skip(self, ')'); break;
    case '&':  chop(self); return refr(self, atom(self));
    case '*':  chop(self); return star(self, atom(self));
    case '-': case '!': case '~': {
      chop(self);
      x = atom(self);
      if(x.kind == vnum)
        return cnst(op == '-'? (s32i)-(u32i)x.ival: op == '!'? !x.ival: ~x.ival);
      x = load(self, x);
      u32i w = wide(x.type);
      if(op == '-') { emit(self, swap); emit(self, move, 0); emit(self, calc, '-' | w, 0, 0); }
      else emit(self, calc, (op == '!'? tokd('=', '='): '^') | w, 1, op == '!'? 0: -1);
      return acc(op == '!' || !w? NULL: x.type);
    }
    default:
      flaw(self, "expected expression, got %x\n", op);
  }
  for(;;) { // postfix: x[i] is *(x + i), x.m, x->m is (*x).m
    if(twin(self, '[')) {
      byte* mark = keep(self, &x);
      oper  i = expr(self, 1);
      meta* t = x.type && x.type->kind == many? x.type->type: NULL;
      skip(self, ']');
      if(t && i.kind == vnum && (x.kind == vloc || x.kind == vglo))
        x = (oper){x.kind, x.ival + i.ival * (s32i)t->size, t};
      else x = star(self, pair(self, '+', x, i, mark));
    }
    else if(twin(self, '.')) x = memb(self, x);
    else if(twin(self, tokd('-', '>'))) x = memb(self, star(self, x));
    else return x;
  }
}

// x && y, x || y: y runs only when x does not decide, the result is 0 or 1
oper  both(mode* self, u32i op, oper x, int r) {
  bool  any  = op == tokd('|', '|'); // the value of x that decides
  byte* mark = self->text->body;
  oper  y;
  if(x.kind == vnum) {
    y = expr(self, r + 1);
    if(!x.ival != any) { trim(self, mark); return cnst(any); } // y is dead
    if(y.kind == vnum) return cnst(!!y.ival);
    y = load(self, y);
    emit(self, calc, tokd('!', '=') | wide(y.type), 1, 0);
    return acc(NULL);
  }
  load(self, x);
  emit(self, when, any);
  mark = self->text->body;
  y = load(self, expr(self, r + 1));
  emit(self, calc, tokd('!', '=') | wide(y.type), 1, 0);
  emit(self, jump);
  emit(self, back, mark);
  mark = self->text->body;
  emit(self, move, any);
  emit(self, back, mark);
  return acc(NULL);
}

// an int in acc on 64 bits for a WIDE op, times scale
void  grow(mode* self, oper x, s32i scale, u32i w) {
  if(!w || wide(x.type)) return;
  emit(self, cast, 1);
  if(scale != 1) emit(self, calc, '*' | WIDE, 1, scale);
}

// x op y, y parsed after mark; x was pushed there when it was in acc.
// pointer + n moves n elements, pointer - pointer counts them; void* by bytes
oper  pair(mode* self, u32i op, oper x, oper y, byte* mark) {
  meta *ex = elem(x.type), *ey = elem(y.type), *t = NULL; // t: type of the result
  s32i  sx = 1, sy = 1;
  u32i  w = wide(x.type) | wide(y.type), r = rank(op);
  bool  test = r == 6 || r == 7;
  if(ex || ey) {
    if(!test && !(op == '+' && !(ex && ey)) && !(op == '-' && ex))
      flaw(self, "invalid pointer arithmetic\n");
    if(!test && !(ex && ey)) {
      t = href(self, ex? ex: ey);
      if(ex) sy = ex->size? ex->size: 1; else sx = ey->size? ey->size: 1;
    }
  }
  else if(w && !test) t = wide(x.type)? x.type: y.type; // long

  bool trap = (op == '/' || op == '%') && y.kind == vnum && !y.ival; // left to run time
  if(x.kind == vnum && y.kind == vnum && !trap)
    return cnst(fold(op, x.ival, y.ival));
  if(x.kind == vnum) { x.ival *= sx; sx = 1; }
  if(y.kind == vnum) { y.ival *= sy; sy = 1; }

  if(y.kind == vnum || (y.kind == vloc && (y.type->kind == base || y.type->kind == refs) && (!w || wide(y.type)))) {
    self->text->body = mark; // y emitted nothing: drop the push
    grow(self, load(self, x), sx, w);
    if(y.kind == vnum) emit(self, calc, op | w, 1, y.ival);
    else { emit(self, peek, y.ival, span(y.type)); emit(self, calc, op | w, 0, 0); }
  }
  else {
    grow(self, load(self, y), sy, w);
    if(x.kind == vacc) emit(self, pull);
    else { emit(self, swap); x = load(self, x); }
    grow(self, x, sx, w);
    emit(self, calc, op | w, 0, 0);
  }
  if(ex && ey && op == '-' && ex->size > 1) emit(self, calc, '/', 1, ex->size);
  return acc(t);
}

// the operator of an assignment: '=' or the op of op=, 0 when it is none
u32i  sets(u32i kind) {
  switch(kind) {
    case '=': return '=';
    case tokd('+', '='): case tokd('-', '='): case tokd('*', '='): case tokd('/', '='):
    case tokd('%', '='): case tokd('&', '='): case tokd('|', '='): case tokd('^', '='):
      return kind & 0xff;
    case tokd('<', '<', '='): case tokd('>', '>', '='):
      return kind & 0xffff;
    default: return 0;
  }
}

// x = y, x op= y: right associative, the value is left in acc
oper  give(mode* self, u32i op, oper x) {
  meta* t = x.type;
  if(x.kind == vnum || x.kind == vacc || (t && (t->kind == many || t->kind == list)))
    flaw(self, "cannot assign\n");
  if(x.kind == vglo) { spot(self, x.ival); x.kind = vmem; }
  if(x.kind == vmem) emit(self, push); // the address, op= reads through it
  byte* mark = op == '='? self->text->body: keep(self, &x);
  oper  y = expr(self, 1);
  y = load(self, op == '='? y: pair(self, op, x, y, mark));
  if(!wide(t) != !wide(y.type)) emit(self, cast, !!wide(t));
  if(x.kind == vloc) emit(self, stor, x.ival, span(t));
  else { emit(self, pull); emit(self, fill, span(t)); }
  if(span(t) < 4) emit(self, calc, '&', 1, (1 << span(t) * 8) - 1); // as it was stored
  return acc(t);
}

// precedence climbing over binary operators of rank >= least
oper  expr(mode* self, int least) {
  oper x = atom(self);
  u32i op = sets(self->talk.kind);
  if(op && least <= 1) { chop(self); return give(self, op, x); }
  for(u32i r; (r = rank(op = self->talk.kind)) >= (u32i)least; ) {
    chop(self);
    if(op == tokd('&', '&') || op == tokd('|', '|')) { x = both(self, op, x, r); continue; }

    byte* mark = keep(self, &x); // y may need acc
    x = pair(self, op, x, expr(self, r + 1), mark);
  }
  return x;
}

// value of a full expression in acc
void  eval(mode* self) {
  load(self, expr(self, 1));
}

#define seed(self, ...) {\
  meta x = { __VA_ARGS__ }; \
  if(self->top == sizeof self->pile / sizeof *self->pile) \
    flaw(self, "too many names\n"); \
  x.unit = self->unit; \
  x.hide = x.name->data; \
  x.name->data = self->pile+self->top; \
  self->pile[self->top++] = x; }

#define crux(slef, str, ...) \
  seed((self), .kind = base, .name = sget((self), (str)), __VA_ARGS__)

// leave a scope: the names declared since top mean what they did before
void  bury(mode* self, u32i top) {
  while(self->top > top) {
    meta* x = self->pile + --self->top;
    x->name->data = x->hide;
  }
}

// fresh frame slots for size bytes
s32i  room(mode* self, u32i size) {
  self->slot += (size + 7) & -8;
  if(self->deep < self->slot) self->deep = self->slot;
  return -self->slot;
}

// the type the word names, NULL when it names none
meta* ilk(mode* self) {
  meta* x = self->talk.kind == ttag? find(self, self->unit, self->talk.hash): NULL;
  return x && (x->kind == base || x->kind == list)? x: NULL;
}

#define type(self) (same(self, "struct") || ilk(self))

// a type name, after an optional struct
meta* sort(mode* self) {
  if(same(self, "struct")) chop(self);
  meta* type = ilk(self);
  if(!type) {
    if(self->talk.kind == ttag)
      flaw(self, "expected type, got '%.*s'\n", nick(self->talk.hash));
    flaw(self, "expected type, got %x\n", self->talk.kind);
  }
  chop(self);
  return type;
}

// the stars before a declared name: pointers to type, the name into *name
meta* term(mode* self, meta* type, item** name) {
  while(twin(self, '*')) type = href(self, type);
  if(self->talk.kind != ttag)
    flaw(self, "expected name, got %x\n", self->talk.kind);
  *name = self->talk.hash;
  chop(self);
  return type;
}

// [n][m] after a declared name: n arrays of m of type
meta* dims(mode* self, meta* type) {
  if(!twin(self, '[')) return type;
  oper n = expr(self, 1);
  skip(self, ']');
  if(n.kind != vnum || n.ival <= 0)
    flaw(self, "array size must be a positive constant\n");
  type = dims(self, type);
  return knot(self, many, type, n.ival * type->size);
}

// the declared type of name with its array sizes, which must have a size
meta* full(mode* self, meta* type, item* name) {
  type = dims(self, type);
  if(!type->size) flaw(self, "'%.*s' has no size\n", nick(name));
  return type;
}

// a declarator after base: *name and its full type
meta* noun(mode* self, meta* base, item** name) {
  meta* type = term(self, base, name);
  return full(self, type, *name);
}

// size bytes at slot from src in stores of 4, 2 or 1 bytes, or zeros when
// src is NULL
void  bits(mode* self, s32i slot, byte* src, u32i size) {
  if(!src && size) emit(self, move, 0);
  for(u32i n; size; slot += n, size -= n) {
    n = size >= 8 && !src? 8: size >= 4? 4: size >= 2? 2: 1;
    if(src) { s32i v = 0; copy((byte*)&v, src, n); src += n; emit(self, move, v); }
    emit(self, stor, slot, n);
  }
}

// the initializer of type: { a list }, the rest zero, a string for a char
// array, or a value; a global's is a constant written to at, a local's is
// stored to slot (at is NULL)
void  seal(mode* self, meta* type, byte* at, s32i slot) {
  meta* t = type->kind == many? type->type: NULL;
  if(t && twin(self, '{')) {
    u32i i = 0;
    for(; !twin(self, '}'); i++) {
      if(i) skip(self, ',');
      if((i + 1) * t->size > type->size) flaw(self, "too many initializers\n");
      seal(self, t, at? at + i * t->size: NULL, slot + i * t->size);
    }
    if(!at) bits(self, slot + i * t->size, NULL, type->size - i * t->size); // data is zero
  }
  else if(t && t->size == 1 && self->talk.kind == '"') {
    u32i n = quote(self->talk.hash, NULL);
    if(n > type->size) flaw(self, "string too long\n");
    if(at) quote(self->talk.hash, at);
    else {
      byte b[n];
      quote(self->talk.hash, b);
      bits(self, slot, b, n);
      bits(self, slot + n, NULL, type->size - n);
    }
    chop(self);
  }
  else if(at) {
    oper x = expr(self, 1);
    if(x.kind != vnum || t || type->kind == list)
      flaw(self, "expected a constant\n");
    s64i v = x.ival;
    copy(at, &v, type->size);
  }
  else give(self, '=', (oper){vloc, slot, type});
}

// type declarator (= init)? (, declarator (= init)?)* ;
void  vars(mode* self) {
  meta* base = sort(self);
  do {
    item* name;
    meta* type = noun(self, base, &name);
    s32i  slot = room(self, type->size);
    seed(self, .name=name, .type=type, .kind=data, .slot=slot);
    if(twin(self, '=')) seal(self, type, NULL, slot);
  } while(twin(self, ','));
  skip(self, ';');
}

// fail when a global or function is defined again. prev, what the name meant
// before, is of this unit, or, unless the new one is static (own), of another
// that makes it public, but not as a weak definition. Nor may another unit
// define it as public when the new one is
void  once(mode* self, meta* prev, item* name, bool own) {
  meta* x = own? NULL: find(self, 0, name);
  if((prev && (prev->kind == glob || prev->kind == call)
      && (prev->unit == self->unit || (!own && !prev->weak)))
     || (x && (x->kind == glob || x->kind == call) && heft(x) == 4))
    flaw(self, "'%.*s' is already defined\n", nick(name));
}

// struct name? { type declarator (, declarator)* ; ... } ;
// a member is aligned to its size, the struct to its widest member
void  bulk(mode* self) {
  meta  anon = {.kind = list}, *type = &anon, **next = &anon.type;
  if(self->talk.kind == ttag) { // seeded first: members may point to it
    once(self, find(self, self->unit, self->talk.hash), self->talk.hash, 0);
    seed(self, .name=self->talk.hash, .kind=list);
    type = self->pile + self->top - 1;
    next = &type->type;
    chop(self);
  }
  skip(self, '{');
  u32i size = 0, align = 1;
  while(!twin(self, '}')) {
    meta* base = sort(self);
    do {
      item* name;
      meta* t = noun(self, base, &name);
      u32i  a = lean(t);
      size = (size + a - 1) & -a;
      *next = mint(self, (meta){.name = name, .type = t, .slot = size});
      next = &(*next)->hide;
      size += t->size;
      if(align < a) align = a;
    } while(twin(self, ','));
    skip(self, ';');
  }
  skip(self, ';');
  type->size = (size + align - 1) & -align;
  type->slot = align;
}

void  stmt(mode* self);

// loop body: continue jumps back to again, break past the loop
void  iter(mode* self, byte* again) {
  byte* cont = self->cont;
  u32i  ends = self->ends;
  self->cont = again;
  stmt(self);
  emit(self, loop, again);
  while(self->ends > ends) emit(self, back, self->quit[--self->ends]);
  self->cont = cont;
}

void  stmt(mode* self) {
  u32i  top  = self->top;
  s32i  slot = self->slot;
  if(twin(self, '{')) {
    while(!twin(self, '}')) stmt(self);
  }
  else if(twin(self, ';')) {
  }
  else if(type(self)) {
    vars(self);
    return; // the names stay in the enclosing scope
  }
  else if(same(self, "if")) {
    byte* mark = NULL;
    chop(self);
    skip(self, '('); eval(self); skip(self, ')');
    emit(self, when, 0);
    mark = self->text->body; // end of the branch, the arch patches backwards
    stmt(self);
    if(same(self, "else")) {
      chop(self);
      emit(self, jump);
      emit(self, back, mark);
      mark = self->text->body;
      stmt(self);
    }
    emit(self, back, mark);
  }
  else if(same(self, "while")) {
    byte* cond = self->text->body;
    chop(self);
    skip(self, '('); eval(self); skip(self, ')');
    emit(self, when, 0);
    byte* out = self->text->body;
    iter(self, cond);
    emit(self, back, out);
  }
  else if(same(self, "break")) {
    chop(self); skip(self, ';');
    if(!self->cont) flaw(self, "break outside a loop\n");
    if(self->ends == sizeof self->quit / sizeof *self->quit)
      flaw(self, "too many breaks\n");
    emit(self, jump);
    self->quit[self->ends++] = self->text->body;
  }
  else if(same(self, "continue")) {
    chop(self); skip(self, ';');
    if(!self->cont) flaw(self, "continue outside a loop\n");
    emit(self, loop, self->cont);
  }
  else if(same(self, "return")) {
    chop(self);
    if(self->talk.kind != ';') {
      meta* t = self->func->type;
      if(!wide(t) != !wide(load(self, expr(self, 1)).type)) emit(self, cast, !!wide(t));
    }
    skip(self, ';');
    emit(self, done);
  }
  else {
    expr(self, 1); skip(self, ';');
  }
  bury(self, top);
  self->slot = slot;
}

// x, declared without a body or as extern, is an import from the library of
// the last load, or of "c". prev, what the name meant before, may be the same
// import, which keeps its slot, or C code of an object or an archive, which x
// gives a prototype: the calls go to the definition (cabi)
void  hire(mode* self, meta* x, meta* prev) {
  x->ext = 1;
  if(x->kind == call && prev && (prev->kind == pend || (prev->kind == call && prev->ext && !prev->lib)))
    return;
  x->lib = self->from? self->from: rack(self, sget(self, "\"c\""));
  if(!prev || !prev->ext || prev->addr || prev->kind != x->kind || prev->lib != x->lib
     || prev->type != x->type || prev->size != x->size || prev->more != x->more)
    once(self, prev, x->name, x->own);
  else x->slot = prev->slot;
}

// the string after link or load
item* bond(mode* self) {
  item* s = self->talk.hash;
  if(self->talk.kind != '"') flaw(self, "expected a string\n");
  chop(self);
  return s;
}

void  decl(mode* self) {
  // load "name" ";": a shared library, the bodyless functions after it in this
  // source file are imported from it
  if(same(self, "load")) {
    chop(self);
    self->from = rack(self, bond(self));
    skip(self, ';');
    return;
  }
  // link "name" ";": a source, an object or an archive, linked here once
  if(same(self, "link")) {
    chop(self);
    haul(self, bond(self));
    skip(self, ';');
    return;
  }
  // static: the names stay in their source file; extern: a global of a library
  bool own = same(self, "static");
  if(own) chop(self);
  bool ext = same(self, "extern");
  if(ext) chop(self);

  // declaration begins with a type, or defines a struct:
  // "struct" name? "{" decl "}" ";"
  if(same(self, "struct")) {
    chop(self);
    meta* type = ilk(self);
    if(!type || type->kind != list) { bulk(self); return; }
  }

  item* name;
  meta* base = sort(self);
  meta* type = term(self, base, &name);

  // type name "(" args ")" "{" stmt "}"     // callable decl
  // type name "(" args ")" ";"               // import
  if(twin(self, '(')) {
    meta* prev = find(self, self->unit, name);
    seed(self, .name=name, .type=type, .kind=call, .own=own);
    meta* func = self->func = self->pile + self->top - 1;
    u32i  top  = self->top;
    bool  anon = 0;
    self->slot = self->deep = 0;
    for(; !twin(self, ')'); func->size++, twin(self, ',')) {
      if(twin(self, tokd('.', '.', '.'))) { func->more = 1; skip(self, ')'); break; }
      meta* type = sort(self);
      while(twin(self, '*')) type = href(self, type);
      if(!type->size && !func->size && twin(self, ')')) break; // (void)
      if(self->talk.kind != ttag) { anon = 1; continue; } // an import may leave it out
      seed(self, .name=self->talk.hash, .type=type, .kind=data, .slot=room(self, 8));
      chop(self);
    }
    if(func->size > 6) flaw(self, "more than 6 parameters\n");
    if(twin(self, ';')) {
      hire(self, func, prev);
      bury(self, top);
      return;
    }
    once(self, prev, name, own);
    if(anon) flaw(self, "a parameter without a name\n");
    if(func->more) flaw(self, "only an import can be variadic\n");
    if(self->talk.kind != '{') flaw(self, "expected function body\n");
    func->addr = self->text->body;
    emit(self, open);
    for(u32i i = 0; i < func->size; i++) emit(self, parm, i, self->pile[top + i].slot);
    stmt(self);
    emit(self, move, 0); emit(self, done); // implicit return 0
    emit(self, size, func->addr, (self->deep + 15) & -16);
    bury(self, top);
    reel(self); // between functions: members go after text
    return;
  }

  // globals: type name ([n])* (= init)? (, declarator)* ;
  for(type = full(self, type, name);; type = noun(self, base, &name)) {
    if(ext) {
      meta* prev = find(self, self->unit, name);
      seed(self, .name=name, .type=type, .kind=glob, .own=own);
      hire(self, self->pile + self->top - 1, prev);
      if(!twin(self, ',')) break;
      continue;
    }
    once(self, find(self, self->unit, name), name, own);
    u32i off = lay(self, type->size, self->talk.kind != '=', 8);
    if(twin(self, '=')) seal(self, type, self->data->head + off, 0);
    seed(self, .name=name, .type=type, .kind=glob, .slot=off, .own=own);
    if(!twin(self, ',')) break;
  }
  skip(self, ';');
}

// the file at path, read in code, into the pass as a unit of kind unless it
// is in already: 1 when it was not. The unit joins the list as it starts, so
// a cycle back into it sees what it has defined so far. An object is taken,
// the symbol index of an archive read, a source parsed: when it ends, its
// forward calls to functions now known are bound. Names point into code,
// which is kept
bool  join(mode* self, char* path, u64i size, file* code, byte kind) {
  for(unit* u = self->units + 1; kind != un_memb && u < self->units + self->unitn; u++)
    if(u->kind != un_memb && alike(u->path, path, size + 1)) { free(code); return 0; }
  file* last = self->code;
  u64i  line = self->line;
  word  talk = self->talk;
  u32i  fixs = self->fixs, from = self->from, back = self->unit;
  self->unit = nest(self, (unit){.path = path, .code = code, .kind = kind});
  self->from = 0;
  if(kind == un_arch) ario_load(self, code);
  else if(kind != un_code) {
    if(!self->form.take) fail("%s: no objects in this form\n", path);
    self->form.take(self, code);
  } else {
    self->code = code;
    self->line = 1;
    for(chop(self); self->talk.kind; ) decl(self);
    tie(self, fixs, 0);
  }
  self->unit = back;
  self->code = last;
  self->line = line;
  self->talk = talk;
  self->from = from;
  return 1;
}

// path, size bytes from malloc, tidy'd and joined as a unit of kind with
// code, read when NULL: 0, path still the caller's, when it cannot be read.
// Freed when the file is in already
bool  land(mode* self, char* path, u64i size, file* code, byte kind) {
  size = tidy(path, size);
  if(!code && !(code = look(path))) return 0;
  if(!join(self, path, size, code, kind)) free(path);
  return 1;
}

// the file at name, named on the command line, as a unit of kind; read when
// code is NULL
void  lead(mode* self, char* name, file* code, byte kind) {
  u64i  size = slen(name);
  char* path = malloc(size + 1);
  if(!path) fail("out of memory\n");
  copy((byte*)path, name, size + 1);
  land(self, path, size, code? code: scan(name), kind);
}

void  make(mode* self) {
  // unit = callable
  //      | variable
  //      | typedecl

  self->line = 1;
  self->text = (file*)self->hold(0, TEXT);
  self->text->body = self->text->head;
  self->text->tail = (byte*)self->text + TEXT;
  self->data = calloc(1, sizeof(file) + DATA);
  if(!self->data) fail("out of memory\n");
  self->data->body = self->data->head;
  self->data->tail = self->data->head + DATA;

  emit(self, boot);

  self->unit = nest(self, (unit){.path = self->name, .kind = un_root});
  crux(self, "void", .size=0);
  crux(self, "bool", .size=1);
  crux(self, "byte", .size=1);
  crux(self, "char", .size=1);
  self->rune = self->pile + self->top - 1;
  crux(self, "nu16", .size=2);
  crux(self, "int",  .size=4);
  crux(self, "long", .size=8);

  // objects and archives on the command line come before the source, which
  // calls them like any function defined before it
  for(u32i i = 0; i < self->objn; i++)
    lead(self, self->objs[i].path, self->objs[i].code, self->objs[i].kind);
  lead(self, self->name, NULL, un_code);
  reel(self);

  // a C library linked in starts first: its __libc_start_main calls main and
  // exits through the library, which flushes stdio
  meta* libc = find(self, 0, sget(self, "__libc_start_main"));
  if(libc && libc->kind == pend) {
    ask(self, libc);
    reel(self);
    libc = find(self, 0, libc->name);
  }
  tie(self, 0, 1);

  meta* func = find(self, 0, sget(self, "main"));
  if(!func || func->kind != call || !func->addr) fail("no main function\n");
  self->main = self->boot = func->addr;
  if(libc && libc->kind == call && libc->ext && libc->addr) {
    self->boot = self->text->body;
    emit(self, crt, self->main, libc->addr);
  }
  emit(self, link, self->text->head, self->boot);

  self->show(self);
  self->drop(self->text, TEXT);
  free(self->data);
}
