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

item* match(item* x, char* name, u64i size) {
  if(!x->name) {
    x->name = name;
    x->size = size;
    return x;
  }
  if(x->size != size) return NULL;
  for(char* p = x->name; size && *p++==*name++; size--);
  return !size? x: NULL;
}

#define sget(self, k) hget(self->pool, k, sizeof(k)-1)
item* hget(hash self, char* mark, u64i size) {
  u64i  clue = 0x01;
  item* seek = NULL;

  // calculate string hash
  for(u64i i = 0; i < size; clue += (clue << 5) + (clue >> 27) + mark[i++]);

  // lookup free spot or the same string
  for(u64i i = 1; !seek && i <= 10; i++)
    seek = match(self + ((i * clue) & (POOL - 1)), mark, size);

  if(!seek) fail("hash pool full at '%.*s'\n", (int)size, mark);
  return seek;
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
          if(!*c) fail("line %ld: unterminated comment\n", self->line);
          c += 2; goto pass;
        default: kind = (c[1] == '='? tok2(c): tok1(c));
      }
      break;
    //case '\'':
    //  kind = '"'; c+=2; break;
    case '"':
      for(c++; *c != '"'; c++) {
        if(!*c || *c == '\n') fail("line %ld: unterminated string\n", self->line);
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
    default: fail("line %ld: unexpected character '%c'\n", self->line, *c);
  }

  if(kind == '"' || kind == ttag) {
    self->talk.hash = hget(self->pool, (char*)self->code->body, c - self->code->body);
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
    fail("line %ld expected %x, got %x\n", self->line, kind, self->talk.kind);
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


#define emit(self, call, ...) (self->arch.call(self->text __VA_OPT__(,) __VA_ARGS__))
#define nick(x) (int)(x)->size, (x)->name

// types: NULL is an int value
meta* mint(mode* self, meta x) {
  if(self->defn == sizeof self->defs / sizeof *self->defs)
    fail("line %ld: too many types\n", self->line);
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
#define ZERO    (1u << 31) // a data offset in bss

// size bytes of data, or of bss when zero: their offset
u32i  lay(mode* self, u32i size, bool zero) {
  if(zero) {
    self->zero = ((self->zero + 7) & -8) + size;
    return (self->zero - size) | ZERO;
  }
  file* d = self->data;
  u32i  off = ((d->body - d->head) + 7) & -8;
  if(off + size > (u64i)(d->tail - d->head)) fail("data section overflow\n");
  d->body = d->head + off + size;
  return off;
}

// acc = address of data at off
void  spot(mode* self, u32i off) {
  if(self->seats == sizeof self->aims / sizeof *self->aims)
    fail("line %ld: too many global references\n", self->line);
  self->aims[self->seats++] = (seat){self->text->body, off};
  emit(self, glob);
}

void  dock(mode* self, u64i at) {
  u64i bss = at + ((self->data->body - self->data->head + 15) & -16);
  for(seat* s = self->aims; s < self->aims + self->seats; s++)
    emit(self, site, s->at, (s->off & ZERO? bss + (s->off & ~ZERO): at + s->off)
                            - (s->at - self->text->head));
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
  if(t && (!t->size || t->kind == list)) fail("line %ld: not a value\n", self->line);
  if(x.kind == vloc) emit(self, load, x.ival, span(t));
  if(x.kind == vglo) spot(self, x.ival);
  if(x.kind != vloc) emit(self, read, span(t));
  return acc(t);
}

// &x
oper  refr(mode* self, oper x) {
  if(x.kind == vloc) emit(self, addr, x.ival);
  if(x.kind == vglo) spot(self, x.ival);
  if(x.kind == vnum || x.kind == vacc) fail("line %ld: cannot take the address\n", self->line);
  return acc(href(self, x.type));
}

// *x
oper  star(mode* self, oper x) {
  meta* t = elem(x.type);
  if(!t) fail("line %ld: not a pointer\n", self->line);
  load(self, x);
  return (oper){vmem, 0, t};
}

// x.name
oper  memb(mode* self, oper x) {
  meta* m = x.type && x.type->kind == list? x.type->type: NULL;
  if(!m || self->talk.kind != ttag) fail("line %ld: expected a member\n", self->line);
  for(; m && m->name != self->talk.hash; m = m->hide);
  if(!m) fail("line %ld: no member '%.*s'\n", self->line, nick(self->talk.hash));
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

// point the call at to the function name with argc arguments
void  bind(mode* self, byte* at, item* name, u32i argc) {
  meta* func = name->data;
  if(!func || func->kind != call)
    fail("line %ld: no function '%.*s'\n", self->line, nick(name));
  if(func->size != argc)
    fail("line %ld: '%.*s' takes %d arguments, got %d\n", self->line, nick(name), func->size, argc);
  if(at) emit(self, link, at, func->addr);
}

// name(args): each argument is pushed on 64 bits, then popped into its
// register; syscall(number, args) goes to the kernel
oper  dial(mode* self, item* name) {
  bool trap = name == sget(self, "syscall");
  u32i argc = 0;
  for(; !twin(self, ')'); argc++) {
    if(argc) skip(self, ',');
    if(argc == 6u + trap) fail("line %ld: more than 6 arguments\n", self->line);
    if(!wide(load(self, expr(self, 1)).type)) emit(self, cast, 1); // a long parameter
    emit(self, push);
  }
  if(trap) {
    if(!argc) fail("line %ld: syscall without a number\n", self->line);
    emit(self, trap, argc);
    return acc(NULL);
  }
  for(u32i i = argc; i--; ) emit(self, args, i);
  byte* at = self->text->body;
  emit(self, dial);
  if(name->data) bind(self, at, name, argc);
  else if(self->fixs == sizeof self->link / sizeof *self->link)
    fail("line %ld: too many forward calls\n", self->line);
  else self->link[self->fixs++] = (fix){name, at, argc, self->line};
  return acc(name->data? ((meta*)name->data)->type: NULL);
}

// x before its right operand is parsed: a value in acc is pushed after the mark
byte* keep(mode* self, oper* x) {
  if(x->kind == vmem) *x = load(self, *x);
  byte* mark = self->text->body;
  if(x->kind == vacc) emit(self, push);
  return mark;
}

oper  pair(mode* self, u32i op, oper x, oper y, byte* mark);

oper  unit(mode* self) {
  u32i op = self->talk.kind;
  oper x;
  switch(op) {
    case tnum: x = cnst(self->talk.ival); chop(self); return x;
    case '"': {
      u32i off = lay(self, self->talk.hash->size - 1, 0);
      quote(self->talk.hash, self->data->head + off);
      chop(self);
      spot(self, off);
      x = acc(href(self, self->rune));
      break;
    }
    case ttag: {
      item* name = self->talk.hash;
      meta* var = name->data;
      chop(self);
      if(twin(self, '(')) { x = dial(self, name); break; }
      if(!var || (var->kind != data && var->kind != glob))
        fail("line %ld: no variable '%.*s'\n", self->line, nick(name));
      x = (oper){var->kind == data? vloc: vglo, var->slot, var->type};
      break;
    }
    case '(':  chop(self); x = expr(self, 1); skip(self, ')'); break;
    case '&':  chop(self); return refr(self, unit(self));
    case '*':  chop(self); return star(self, unit(self));
    case '-': case '!': case '~': {
      chop(self);
      x = unit(self);
      if(x.kind == vnum)
        return cnst(op == '-'? (s32i)-(u32i)x.ival: op == '!'? !x.ival: ~x.ival);
      x = load(self, x);
      u32i w = wide(x.type);
      if(op == '-') { emit(self, swap); emit(self, move, 0); emit(self, calc, '-' | w, 0, 0); }
      else emit(self, calc, (op == '!'? tokd('=', '='): '^') | w, 1, op == '!'? 0: -1);
      return acc(op == '!' || !w? NULL: x.type);
    }
    default:
      fail("line %ld: expected expression, got %x\n", self->line, op);
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
      fail("line %ld: invalid pointer arithmetic\n", self->line);
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
    fail("line %ld: cannot assign\n", self->line);
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
  oper x = unit(self);
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
    fail("line %ld: too many names\n", self->line); \
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

#define type(self) (same(self, "struct") || ((self)->talk.kind == ttag &&\
  (self)->talk.hash->data && (((meta*)(self)->talk.hash->data)->kind == base ||\
  ((meta*)(self)->talk.hash->data)->kind == list)))

// a type name, after an optional struct
meta* sort(mode* self) {
  if(same(self, "struct")) chop(self);
  meta* type = self->talk.kind == ttag? self->talk.hash->data: NULL;
  if(!type || (type->kind != base && type->kind != list)) {
    if(self->talk.kind == ttag)
      fail("line %ld: expected type, got '%.*s'\n", self->line, nick(self->talk.hash));
    fail("line %ld: expected type, got %x\n", self->line, self->talk.kind);
  }
  chop(self);
  return type;
}

// the stars before a declared name: pointers to type, the name into *name
meta* term(mode* self, meta* type, item** name) {
  while(twin(self, '*')) type = href(self, type);
  if(self->talk.kind != ttag)
    fail("line %ld: expected name, got %x\n", self->line, self->talk.kind);
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
    fail("line %ld: array size must be a positive constant\n", self->line);
  type = dims(self, type);
  return knot(self, many, type, n.ival * type->size);
}

// the declared type of name with its array sizes, which must have a size
meta* full(mode* self, meta* type, item* name) {
  type = dims(self, type);
  if(!type->size) fail("line %ld: '%.*s' has no size\n", self->line, nick(name));
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
      if((i + 1) * t->size > type->size) fail("line %ld: too many initializers\n", self->line);
      seal(self, t, at? at + i * t->size: NULL, slot + i * t->size);
    }
    if(!at) bits(self, slot + i * t->size, NULL, type->size - i * t->size); // data is zero
  }
  else if(t && t->size == 1 && self->talk.kind == '"') {
    u32i n = quote(self->talk.hash, NULL);
    if(n > type->size) fail("line %ld: string too long\n", self->line);
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
      fail("line %ld: expected a constant\n", self->line);
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

// struct name? { type declarator (, declarator)* ; ... } ;
// a member is aligned to its size, the struct to its widest member
void  bulk(mode* self) {
  meta  anon = {.kind = list}, *type = &anon, **next = &anon.type;
  if(self->talk.kind == ttag) { // seeded first: members may point to it
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
  else if(same(self, "for")) {
    // init; cond: jz end; jmp body; step: jmp cond; body: jmp step; end:
    byte* out = NULL;
    chop(self);
    skip(self, '(');
    if(type(self)) vars(self);
    else if(!twin(self, ';')) { expr(self, 1); skip(self, ';'); }
    byte* cond = self->text->body;
    if(self->talk.kind != ';') {
      eval(self);
      emit(self, when, 0);
      out = self->text->body;
    }
    skip(self, ';');
    byte* step = cond;
    if(self->talk.kind != ')') {
      emit(self, jump);
      step = self->text->body;
      expr(self, 1);
      emit(self, loop, cond);
      emit(self, back, step);
    }
    skip(self, ')');
    iter(self, step);
    if(out) emit(self, back, out);
  }
  else if(same(self, "break")) {
    chop(self); skip(self, ';');
    if(!self->cont) fail("line %ld: break outside a loop\n", self->line);
    if(self->ends == sizeof self->quit / sizeof *self->quit)
      fail("line %ld: too many breaks\n", self->line);
    emit(self, jump);
    self->quit[self->ends++] = self->text->body;
  }
  else if(same(self, "continue")) {
    chop(self); skip(self, ';');
    if(!self->cont) fail("line %ld: continue outside a loop\n", self->line);
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

void  decl(mode* self) {
  // declaration begins with a type, or defines a struct:
  // "struct" name? "{" decl "}" ";"
  if(same(self, "struct")) {
    chop(self);
    meta* type = self->talk.kind == ttag? self->talk.hash->data: NULL;
    if(!type || type->kind != list) { bulk(self); return; }
  }

  item* name;
  meta* base = sort(self);
  meta* type = term(self, base, &name);

  // type name "(" args ")" "{" stmt "}"     // callable decl
  if(twin(self, '(')) {
    seed(self, .name=name, .type=type, .kind=call);
    meta* func = self->func = self->pile + self->top - 1;
    u32i  top  = self->top;
    self->slot = self->deep = 0;
    while(!twin(self, ')')) {
      item* name;
      meta* type = term(self, sort(self), &name);
      seed(self, .name=name, .type=type, .kind=data, .slot=room(self, 8));
      twin(self, ',');
    }
    func->size = self->top - top;
    if(func->size > 6) fail("line %ld: more than 6 parameters\n", self->line);
    if(self->talk.kind != '{') fail("line %ld: expected function body\n", self->line);
    func->addr = self->text->body;
    emit(self, open);
    for(u32i i = 0; i < func->size; i++) emit(self, parm, i, self->pile[top + i].slot);
    stmt(self);
    emit(self, move, 0); emit(self, done); // implicit return 0
    emit(self, size, func->addr, (self->deep + 15) & -16);
    bury(self, top);
    return;
  }

  // globals: type name ([n])* (= init)? (, declarator)* ;
  for(type = full(self, type, name);; type = noun(self, base, &name)) {
    u32i off = lay(self, type->size, self->talk.kind != '=');
    if(twin(self, '=')) seal(self, type, self->data->head + off, 0);
    seed(self, .name=name, .type=type, .kind=glob, .slot=off);
    if(!twin(self, ',')) break;
  }
  skip(self, ';');
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

  crux(self, "void", .size=0);
  crux(self, "bool", .size=1);
  crux(self, "byte", .size=1);
  crux(self, "char", .size=1);
  self->rune = self->pile + self->top - 1;
  crux(self, "nu16", .size=2);
  crux(self, "int",  .size=4);
  crux(self, "long", .size=8);

  chop(self);
  do {
    decl(self);
  } while(self->talk.kind);

  for(fix* f = self->link; f < self->link + self->fixs; f++) {
    self->line = f->line;
    bind(self, f->at, f->name, f->argc);
    if(wide(((meta*)f->name->data)->type)) // its result was taken as an int
      fail("line %ld: '%.*s' does not return an int, define it before the call\n",
           self->line, nick(f->name));
  }

  meta* func = sget(self, "main")->data;
  if(!func || func->kind != call) fail("no main function\n");
  self->main = func->addr;
  emit(self, link, self->text->head, self->main);

  self->show(self);
  self->drop(self->text, TEXT);
  free(self->data);
}
