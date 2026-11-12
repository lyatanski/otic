// vim: ts=2:sw=2
#include "core.h"

enum { tend, ttag, tnum, treal };
// text is held at its most, the pages touched as it fills: code points into
// it. Data starts at DATA and doubles as needed, up to below WANT: what points
// into it is an offset
#define CODE (1u << 30)
#define DATA (1u << 16)
#define tok1(x) ((x)+=1, tokd((x)[-1]))
#define tok2(x) ((x)+=2, tokd((x)[-2], (x)[-1]))
#define tok3(x) ((x)+=3, tokd((x)[-3], (x)[-2], (x)[-1]))
// a string literal in the table of the unit being parsed
#define sget(self, k) hget(self, (self)->unit, k, sizeof(k)-1)
// what the type name k means in the unit being parsed, as a declaration sees it
#define tget(self, k) find(self, (self)->unit, sget(self, k))

// the character an escape \v stands for: \n, \t, \r, \0, else v itself
char  esc(char v) { return v == 'n'? '\n': v == 't'? '\t': v == 'r'? '\r': v == '0'? 0: v; }

void chop(mode* self) {
  s64i  ival = 0;
  u64i  fval = 0;
  u32i  kind = tend;
  byte* c = home(self)->code->body;

  pass: switch(*(home(self)->code->body = c)) {
    case '\n': home(self)->line++; // fallthrough
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
            home(self)->line += *c == '\n';
          if(!*c) flaw(self, "unterminated comment\n");
          c += 2; goto pass;
        default: kind = (c[1] == '='? tok2(c): tok1(c));
      }
      break;
    case '\'': // a character is its number
      c++;
      if(*c == '\\' && c[1]) ival = (byte)esc(*++c);
      else if(*c && *c != '\n' && *c != '\'') ival = (byte)*c;
      else flaw(self, "expected one character between ' and '\n");
      if(*++c != '\'') flaw(self, "expected one character between ' and '\n");
      c++;
      kind = tnum; break;
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
    case '0': case '1': case '2': case '3': case '4':
    case '5': case '6': case '7': case '8': case '9':
      while(*c >= '0' && *c <= '9') ival = ival * 10 + *c++ - '0';
      // a real: digits with a . or an exponent, its value rounded by the host
      if(*c == '.' || ((*c == 'e' || *c == 'E') && ((c[1] >= '0' && c[1] <= '9')
         || ((c[1] == '+' || c[1] == '-') && c[2] >= '0' && c[2] <= '9')))) {
        char*  end;
        double v = strtod((char*)home(self)->code->body, &end); // from the token's start
        copy((byte*)&fval, &v, sizeof v);
        c = (byte*)end;
        kind = treal; break;
      }
      if(*c == 'B' || *c == 'b')
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
    case '\0': break;
    default: // a name: a letter or _, then letters, digits and _
      if(!((*c >= 'a' && *c <= 'z') || (*c >= 'A' && *c <= 'Z') || *c == '_'))
        flaw(self, "unexpected character '%c'\n", *c);
      while((*c >= 'a' && *c <= 'z') || (*c >= 'A' && *c <= 'Z')
         || (*c >= '0' && *c <= '9') || *c == '_') c++;
      kind = ttag; break;
  }

  if(kind == '"' || kind == ttag) {
    home(self)->talk.hash = hget(self, self->unit, (char*)home(self)->code->body, c - home(self)->code->body);
  }
  else
    home(self)->talk.ival = kind == tnum? (u64i)ival: kind == treal? fval: 0;
  home(self)->talk.kind = kind;
  home(self)->code->body = c;
}

#define same(self, name) (\
  home(self)->talk.kind == ttag &&\
  home(self)->talk.hash == sget((self), (name)))

bool  twin(mode* self, u32i  kind) {
  return (home(self)->talk.kind == kind)? chop(self), 1 : 0;
}

bool  skip(mode* self, int   kind) {
  if(!twin(self, kind))
    flaw(self, "expected %x, got %x\n", kind, home(self)->talk.kind);
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

// types: every value has one, never NULL
meta* mint(mode* self, meta x) {
  meta* m = cell(self, &self->defs, &self->defp, self->defn++);
  *m = x;
  return m;
}

// what the type x is made of, hashed: its kind, the type it is over, its
// size, and slot, longs and more (a function's parameters, a bit-field's bits)
static u64i  sire(const meta* x) {
  u64i h = (u64i)(uintptr_t)x->type ^ (u64i)x->kind << 56 ^ (u64i)x->size << 24;
  h = (h ^ (u64i)(u32i)x->slot << 32 ^ (u64i)x->longs << 8 ^ (u64i)x->reals << 16 ^ x->more) * 0x9e3779b97f4a7c15ull;
  return h ^ h >> 31;
}

// the slot of the type made of what x is, or the free one where it goes
static meta** lode(const mode* self, const meta* x) {
  u64i  mask = self->madec - 1;
  for(u64i i = sire(x) & mask;; i = (i + 1) & mask) {
    meta* y = self->made[i];
    if(!y || (y->kind == x->kind && y->type == x->type && y->size == x->size && y->slot == x->slot
              && y->longs == x->longs && y->reals == x->reals && y->more == x->more)) return self->made + i;
  }
}

// the pointer, array, function or bit-field type made of what x is, made
// once. The table doubles when half full
meta* sole(mode* self, meta x) {
  if(2 * (self->maden + 1) > self->madec) {
    meta** was = self->made;
    u32i   n = self->madec;
    self->madec = n? 2 * n: 64;
    self->made = zero(self, self->madec * sizeof(meta*));
    for(u32i i = 0; i < n; i++) if(was[i]) *lode(self, was[i]) = was[i];
    self->heap(was, 0);
  }
  meta** at = lode(self, &x);
  if(!*at) { *at = mint(self, x); self->maden++; }
  return *at;
}

// a pointer or array type of size bytes over type, made once
meta* knot(mode* self, what kind, meta* type, u32i size) {
  return sole(self, (meta){.kind = kind, .type = type, .size = size});
}

// pointer to type
meta* href(mode* self, meta* type) { return knot(self, refs, type, 8); }

// the type of a function returning type, of argc parameters (-1: unknown),
// the 64-bit ones in longs, the reals in reals, variadic when more, made once
meta* mold(mode* self, meta* type, s32i argc, byte longs, byte reals, bool more) {
  return sole(self, (meta){.kind = proc, .type = type, .slot = argc, .longs = longs, .reals = reals, .more = more});
}

// a pointer to a function type, NULL for the rest
meta* aimp(meta* t) { return t && t->kind == refs && t->type->kind == proc? t->type: NULL; }

// what a pointer points to or an array holds, NULL for the rest
meta* elem(const meta* t) { return t && (t->kind == refs || t->kind == many)? t->type: NULL; }
u32i  wide(const meta* t) { return elem(t) || ((t->kind == base || t->kind == bitf) && t->size == 8)? WIDE: 0; }
u32i  span(const meta* t) { return t->size; }
// the type real
#define flot(self, t) ((t) == tget(self, "real"))
u32i  lean(meta* t) { return t->kind == many? lean(t->type): t->kind == list? (u32i)t->slot: t->size; }

// deferred operand: code is emitted only when the value must be in acc
typedef enum { vacc, vnum, vloc, vglo, vmem, vcmp, vreg, vflo } vkind;
// ival: value, frame slot, data offset or register of a local; vmem: its
// address is in acc; vcmp: the op of a comparison whose result is in the
// flags, for a branch on it; vflo: a real constant, its bits in fval, which
// shares its place with ival
typedef struct { vkind kind; union { s32i ival; u64i fval; }; meta* type; } oper;
#define cnst(self, n) ((oper){.kind = vnum, .ival = (n), .type = tget(self, "int")})
#define acc(t)  ((oper){.kind = vacc, .type = (t)})

// size bytes of data, or of bss when zero: their offset. Data may move: keep
// the offset, not an address into it
u32i  lay(mode* self, u32i size, bool zero, u32i align) {
  if(zero) {
    self->zero = ((self->zero + align - 1) & ~(u64i)(align - 1)) + size;
    if(self->zero >= ZERO) fail("bss section overflow\n");
    return (u32i)(self->zero - size) | ZERO;
  }
  file* d = self->data;
  u64i  off = ((u64i)(d->body - d->head) + align - 1) & ~(u64i)(align - 1), cap = d->tail - d->head;
  if(off + size >= WANT) fail("data section overflow\n");
  if(off + size > cap) {
    u64i  was = cap;
    while(cap < off + size) cap *= 2;
    self->data = d = self->heap(d, sizeof(file) + cap);
    for(u64i i = was; i < cap; d->head[i++] = 0);
    d->tail = d->head + cap;
  }
  d->body = d->head + off + size;
  return (u32i)off;
}

// acc = address of data at off
void  spot(mode* self, u32i off) {
  self->aims = swell(self, self->aims, self->seats, &self->seatc, sizeof(seat), 64);
  self->aims[self->seats++] = (seat){self->text->body, off};
  emit(self, glob);
}

// the bytes of string literal s and a NUL, into out when given: their count
u32i  quote(const item* s, byte* out) {
  u32i n = 0;
  for(const char* c = s->name + 1; c < s->name + s->size - 1; c++, n++) {
    char v = *c;
    if(v == '\\') v = esc(*++c);
    if(out) out[n] = v;
  }
  if(out) out[n] = 0;
  return n + 1;
}

// the bits of bit-field f in its unit
u64i  ones(const meta* f) { return (f->longs < 64? ((u64i)1 << f->longs) - 1: ~(u64i)0) << f->slot; }

// acc, the unit of bit-field f, to the value of f: shifted down, sign extended
// unless its type is narrower than int (a char is not)
oper  bite(mode* self, meta* f) {
  u32i  w = wide(f), n = w? 64: 32;
  if(f->size < 4) {
    if(f->slot) emit(self, calc, tokd('>', '>'), 1, f->slot);
    emit(self, calc, '&', 1, (1 << f->longs) - 1);
  }
  else {
    if(n - f->slot - f->longs) emit(self, calc, tokd('<', '<') | w, 1, n - f->slot - f->longs);
    if(n - f->longs) emit(self, calc, tokd('>', '>') | w, 1, n - f->longs);
  }
  return acc(f->type);
}

// acc &= the bits of bit-field f: a mask that is no 32-bit constant is made in
// acc and moved to tmp
void  mask(mode* self, const meta* f) {
  u64i  m = ones(f);
  u32i  w = wide(f);
  if(!w || (s64i)(s32i)m == (s64i)m) { emit(self, calc, '&' | w, 1, (s32i)m); return; }
  emit(self, push);
  emit(self, move, -1);
  emit(self, cast, 1);
  emit(self, calc, tokd('<', '<') | WIDE, 1, f->longs);
  emit(self, calc, '^' | WIDE, 1, -1);
  emit(self, calc, tokd('<', '<') | WIDE, 1, f->slot);
  emit(self, pull);
  emit(self, calc, '&' | WIDE, 0, 0);
}

// acc into bit-field f of x, a local, or in memory with its address pushed:
// unit ^= (unit ^ acc << slot) & ones. The value is what f holds then
oper  sow(mode* self, oper x, meta* f) {
  u32i  w = wide(f), n = f->size;
  if(f->slot) emit(self, calc, tokd('<', '<') | w, 1, f->slot);
  if(x.kind == vloc) {
    emit(self, peek, x.ival, n);
    emit(self, calc, '^' | w, 0, 0);
    mask(self, f);
    emit(self, peek, x.ival, n);
    emit(self, calc, '^' | w, 0, 0);
    emit(self, stor, x.ival, n);
  }
  else {
    emit(self, pull); // tmp = acc << slot, acc = the address
    emit(self, push);
    emit(self, push);
    emit(self, read, n);
    emit(self, calc, '^' | w, 0, 0);
    mask(self, f);
    emit(self, pull); // tmp = the bits that change, acc = the address
    emit(self, read, n);
    emit(self, calc, '^' | w, 0, 0);
    emit(self, pull);
    emit(self, fill, n);
  }
  return bite(self, f);
}

// the value of a constant, an int or a real
double  sum(oper x) {
  double v = x.ival;
  if(x.kind == vflo) copy((byte*)&v, &x.fval, sizeof v);
  return v;
}

// a real constant of value v
oper  flo(mode* self, double v) {
  oper x = {.kind = vflo, .type = tget(self, "real")};
  copy((byte*)&x.fval, &v, sizeof v);
  return x;
}

oper  load(mode* self, oper x) {
  meta* t = x.type;
  if(x.kind == vcmp) { emit(self, flag, x.ival); return acc(tget(self, "int")); }
  if(x.kind == vflo) { // 0.0 is a move, another one is read from data
    if(!x.fval) { emit(self, move, 0); return acc(t); }
    u32i off = lay(self, 8, 0, 8);
    copy(self->data->head + off, &x.fval, 8);
    x = (oper){.kind = vglo, .ival = (s32i)off, .type = t};
  }
  if(x.kind == vreg) { emit(self, rop, '=' | wide(t), ACC, x.ival, 0); return acc(t); }
  if(t->kind == many) { // an array is the address of its first element
    if(x.kind == vloc) emit(self, addr, x.ival);
    if(x.kind == vglo) spot(self, x.ival);
    return acc(href(self, t->type));
  }
  if(x.kind == vacc || x.kind == vnum) {
    if(x.kind == vnum) emit(self, move, x.ival);
    return acc(t);
  }
  if(!t->size || t->kind == list) flaw(self, "not a value\n");
  if(x.kind == vloc) emit(self, load, x.ival, span(t));
  if(x.kind == vglo) spot(self, x.ival);
  if(x.kind != vloc) emit(self, read, span(t));
  return t->kind == bitf? bite(self, t): acc(t);
}

// &x
oper  refr(mode* self, oper x) {
  if(x.type->kind == bitf) flaw(self, "cannot take the address of a bit-field\n");
  if(x.kind == vloc) emit(self, addr, x.ival);
  if(x.kind == vglo) spot(self, x.ival);
  if(x.kind == vnum || x.kind == vacc || x.kind == vcmp || x.kind == vflo) flaw(self, "cannot take the address\n");
  if(x.kind == vreg) flaw(self, "cannot take the address of a local in a register\n"); // sift keeps such a local in the frame
  return acc(href(self, x.type));
}

// *x
oper  star(mode* self, oper x) {
  meta* t = elem(x.type);
  if(!t) flaw(self, "not a pointer\n");
  load(self, x);
  return (oper){.kind = vmem, .type = t};
}

// the member of the struct type that the name at hand names
meta* mate(mode* self, meta* type) {
  meta* m = type && type->kind == list? type->type: NULL;
  if(!m || home(self)->talk.kind != ttag) flaw(self, "expected a member\n");
  for(; m && !akin(m->name, home(self)->talk.hash); m = m->hide); // m->name may be another unit's
  if(!m) flaw(self, "no member '%.*s'\n", nick(home(self)->talk.hash));
  chop(self);
  return m;
}

// x.name
oper  memb(mode* self, oper x) {
  meta* m = mate(self, x.type);
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

// the ops a register takes (emit.rop): not a shift, nor * / %
#define plain(op) (rank(op) != 8 && rank(op) != 10)
// what a register may hold: an integer or a pointer
#define fits(self, t) ((t)->kind == refs || ((t)->kind == base && (t)->size && !flot(self, t)))

// add a forward call: name, called at at with argc arguments, the C way when ext
void  post(mode* self, item* name, byte* at, u32i argc, bool ext) {
  self->link = swell(self, self->link, self->fixs, &self->fixc, sizeof(fix), 64);
  self->link[self->fixs++] = (fix){name, at, argc, home(self)->line, self->unit, ext};
}

// point the call at to func, the function name with argc arguments; a call
// the C way (ext) was checked against its prototype by cabi
void  bind(const mode* self, byte* at, const item* name, const meta* func, u32i argc, bool ext) {
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
// or short masked. The arguments that are reals are in real (ffi)
oper  cabi(mode* self, item* name, meta* func, u32i argc, u32i real) {
  if(func->size != VARY && (argc < func->size || (argc > func->size && !func->more)))
    flaw(self, "'%.*s' takes %d arguments, got %d\n", nick(name), func->size, argc);
  meta* body = func->lib || func->addr? func: find(self, 0, name); // past a prototype
  if(body && body->kind == pend) ask(self, body);
  bool  wait = !func->lib && (!body || !body->addr);
  if(func->lib) spot(self, port(self, func));
  byte* at = emit(self, ffi, func->lib? NULL: wait? self->text->head: body->addr,
                  argc, func->more? func->size: argc, real | flot(self, func->type) << RETS, self->form.os);
  if(wait) post(self, name, at, argc, 1);
  u32i n = span(func->type);
  if(n && n < 4) emit(self, calc, '&', 1, (1 << n * 8) - 1);
  if(n == 4) emit(self, cast, 0);
  return acc(func->type);
}

// acc, the int x as load left it, sign extended to 64 bits: a constant not
// below 0 is already, its move zero extends
void  sext(mode* self, oper x) {
  if(x.kind != vnum || x.ival < 0) emit(self, cast, 1);
}

// a number: an integer or a real, not a pointer, an array nor a struct
#define digit(t) ((t)->kind == base || (t)->kind == bitf)

// x as a value of type t: an integer made a real when t is the real, a real
// an integer when t is one, truncated to 64 bits, then cut to 32 unless t is
// wide. A constant stays one when it can
oper  bend(mode* self, oper x, meta* t) {
  meta* u = x.type->kind == bitf? x.type->type: x.type;
  bool  to = flot(self, t), from = flot(self, u);
  if(to == from) return x;
  if(!digit(to? u: t)) flaw(self, "a real is not a pointer, an array or a struct\n");
  if(x.kind == vnum) return flo(self, x.ival);
  if(x.kind == vflo && sum(x) > -2147483649.0 && sum(x) < 2147483648.0) return cnst(self, (s32i)sum(x));
  x = load(self, x);
  emit(self, turn, to, wide(x.type) != 0);
  if(to) return acc(tget(self, "real"));
  if(!wide(t)) emit(self, cast, 0);
  return acc(t->kind == bitf? t->type: t);
}

// x as a condition: a real is compared with 0, in the flags, so -0.0 is false
oper  truth(mode* self, oper x) {
  if(!flot(self, x.type)) return x;
  if(x.kind == vflo) return cnst(self, sum(x) != 0);
  u32i  op = tokd('!', '=') | WIDE | REAL;
  load(self, x);
  emit(self, swap);
  emit(self, move, 0);
  emit(self, flop, op | TEST);
  return (oper){.kind = vcmp, .ival = (s32i)op, .type = tget(self, "int")};
}

// the argument i of a function, or of a pointer to one, of type f, made the
// type of its parameter when that is a real or the argument is: in real
// when it is one then
oper  suit(mode* self, const meta* f, u32i i, oper x, u32i* real) {
  bool  known = f && i < 6 && (f->kind == proc? f->slot >= 0 && i < (u32i)f->slot: f->size != VARY && i < f->size);
  if(known && f->reals >> i & 1) x = bend(self, x, tget(self, "real"));
  else if(known && flot(self, x.type)) x = bend(self, x, f->longs >> i & 1? tget(self, "long"): tget(self, "int"));
  if(flot(self, x.type)) *real |= 1u << i;
  return x;
}

// name(args): each argument is pushed on 64 bits, then popped into its
// register, the last one moved there from acc. A constant, or a local in a
// register, waits (late) and is moved to its register once the others are
// computed: the one in acc before only such ones is not pushed. C code and
// syscall(number, args), which goes to the kernel, pop them all
oper  dial(mode* self, item* name) {
  bool  trap = name == sget(self, "syscall");
  meta* func = find(self, self->unit, name);
  bool  ffi = func && (func->kind == pend || (func->kind == call && func->ext));
  u32i  argc = 0, late = 0, real = 0;
  s32i  last = -1; // the argument in acc
  byte *at = NULL, *past = NULL; // around its push
  oper  x[7];
  self->dials++;
  for(; !twin(self, ')'); argc++) {
    if(argc) skip(self, ',');
    if(argc == 6u + trap) flaw(self, "more than 6 arguments\n");
    oper  v = x[argc] = suit(self, func && func->kind == call? func: NULL, argc, expr(self, 1), &real);
    bool  ext = !func || func->kind != call || func->ext || func->longs >> argc & 1; // a long parameter, or one not known yet
    if(!trap && !ffi && (v.kind == vnum || (v.kind == vreg && (wide(v.type) || !ext)))) { late |= 1u << argc; continue; }
    if(!wide(load(self, v).type) && ext) sext(self, v);
    last = argc;
    at = self->text->body;
    if(home(self)->talk.kind == ',') emit(self, push);
    past = self->text->body;
  }
  if(last >= 0 && self->text->body == past) self->text->body = at; // nothing came after: it stays in acc
  else last = -1;
  if(argc && (trap || ffi)) emit(self, push); // they pop all of them
  if(trap) {
    if(!argc) flaw(self, "syscall without a number\n");
    emit(self, trap, argc);
    return acc(tget(self, "int"));
  }
  if(ffi) return cabi(self, name, func, argc, real);
  if(last >= 0) emit(self, pass, last);
  for(s32i i = argc; i--; ) {
    if(late >> i & 1)
      emit(self, rop, '=' | (x[i].kind == vnum? WIDE: wide(x[i].type)), ARG - i, x[i].kind == vnum? NUM: x[i].ival, x[i].ival);
    else if(i != last) emit(self, args, i);
  }
  at = self->text->body;
  emit(self, dial);
  if(func) bind(self, at, name, func, argc, 0);
  else post(self, name, at, argc, 0);
  return acc(func? func->type: tget(self, "int"));
}

s32i  room(mode* self, u32i size);

// the address of function func, named name: a pointer to its type, called
// the C way (ring). That of an import is read from its slot, one in text is
// pc-relative: an otic function's entry for C code, when it has one
oper  hand(mode* self, item* name, meta* func) {
  meta* t = href(self, mold(self, func->type, func->size == VARY? -1: (s32i)func->size, func->longs, func->reals, func->more));
  if(func->lib) { spot(self, port(self, func)); emit(self, read, 8); return acc(t); }
  const meta* body = func->addr? func: find(self, 0, name); // past a prototype
  if(!body || body->kind != call || !body->addr)
    flaw(self, "'%.*s' is not defined yet: define or link it before taking its address\n", nick(name));
  byte* at = self->text->body;
  emit(self, glob);
  emit(self, site, at, (body->hail? body->hail: body->addr) - at);
  return acc(t);
}

// x(args), x a pointer to a function or what one points to: called the C way,
// which an otic function follows as well, or its entry for C code. x is kept in a frame
// slot while the arguments are pushed, each sign extended to 64 bits, then the
// call goes through the slot
oper  ring(mode* self, oper x) {
  meta* f = x.type && x.type->kind == proc && x.kind == vmem? x.type: aimp(x.type);
  if(!f) flaw(self, "not a function\n");
  if(f != x.type) load(self, x); // else its address is in acc
  s32i  slot = room(self, 8);
  u32i  argc = 0, real = 0;
  emit(self, stor, slot, 8);
  self->dials++;
  for(; !twin(self, ')'); argc++) {
    if(argc) { skip(self, ','); emit(self, push); }
    if(argc == 6) flaw(self, "more than 6 arguments\n");
    oper  y = suit(self, f, argc, expr(self, 1), &real);
    if(!wide(load(self, y).type)) sext(self, y);
  }
  if(f->slot >= 0 && (argc < (u32i)f->slot || (argc > (u32i)f->slot && !f->more)))
    flaw(self, "the function takes %d arguments, got %d\n", f->slot, argc);
  if(argc) emit(self, push);
  emit(self, addr, slot);
  emit(self, ffi, NULL, argc, f->more? (u32i)f->slot: argc, real | flot(self, f->type) << RETS, self->form.os);
  u32i n = span(f->type);
  if(n && n < 4) emit(self, calc, '&', 1, (1 << n * 8) - 1);
  if(n == 4) emit(self, cast, 0);
  return acc(f->type);
}

// where the right operand of a pair starts, past the keep of the left one,
// and the calls emitted before it
typedef struct { byte* at; byte* past; u32i dials; } kept;

// x before its right operand is parsed: a value in acc is kept at the mark
kept  keep(mode* self, oper* x) {
  if(x->kind == vmem || x->kind == vcmp) *x = load(self, *x);
  kept  mark = {self->text->body, NULL, self->dials};
  if(x->kind == vacc) { emit(self, keep); self->held++; }
  mark.past = self->text->body;
  return mark;
}

oper  pair(mode* self, u32i op, oper x, oper y, kept mark);
oper  lit(mode* self);
meta* sort(mode* self);
meta* coil(mode* self, meta* type, item** name);
meta* dims(mode* self, meta* type);
oper  atom(mode* self);

// the type the word names, NULL when it names none
meta* ilk(mode* self) {
  meta* x = home(self)->talk.kind == ttag? find(self, self->unit, home(self)->talk.hash): NULL;
  return x && (x->kind == base || x->kind == list)? x: NULL;
}

#define type(self) (same(self, "struct") || ilk(self))

// sizeof's operand: (a type name) with its pointers and arrays, a string
// literal, else a unary expression whose code is dropped. Its bytes
s32i  gist(mode* self) {
  if(home(self)->talk.kind == '"') {
    s32i n = (s32i)quote(home(self)->talk.hash, NULL);
    chop(self);
    return n;
  }
  meta* t;
  if(home(self)->talk.kind == '(') { // a type name or a parenthesized operand: one token decides
    byte* at = home(self)->code->body;
    u64i  line = home(self)->line;
    word  talk = home(self)->talk;
    chop(self);
    if(type(self)) {
      item* name;
      t = dims(self, coil(self, sort(self), &name));
      if(name) flaw(self, "expected a type name, got '%.*s'\n", nick(name));
      skip(self, ')');
      goto sized;
    }
    home(self)->code->body = at, home(self)->line = line, home(self)->talk = talk;
  }
  byte* mark = self->text->body;
  t = atom(self).type;
  trim(self, mark);
sized:
  if(!t->size || t->kind == proc) flaw(self, "sizeof of a type without a size\n");
  return (s32i)t->size;
}

oper  atom(mode* self) {
  u32i op = home(self)->talk.kind;
  oper x;
  switch(op) {
    case tnum: x = cnst(self, (s32i)home(self)->talk.ival); chop(self); return x;
    case treal: x = (oper){.kind = vflo, .fval = home(self)->talk.fval, .type = tget(self, "real")}; chop(self); return x;
    case '"': {
      u32i off = lay(self, (u32i)(home(self)->talk.hash->size - 1), 0, 8);
      quote(home(self)->talk.hash, self->data->head + off);
      chop(self);
      spot(self, off);
      x = acc(href(self, tget(self, "char")));
      break;
    }
    case ttag: {
      if(same(self, "sizeof")) { chop(self); return cnst(self, gist(self)); }
      item* name = home(self)->talk.hash;
      chop(self);
      meta* var = find(self, self->unit, name);
      bool  value = var && (var->kind == data || var->kind == glob); // a pointer called below
      if(!value && twin(self, '(')) { x = dial(self, name); break; }
      if(var && var->kind == call) { x = hand(self, name, var); break; }
      if(var && var->kind == pend) { x = (oper){.kind = vglo, .ival = WANT | ask(self, var), .type = tget(self, "int")}; break; }
      if(!var || (var->kind != data && var->kind != glob))
        flaw(self, "no variable '%.*s'\n", nick(name));
      x = (oper){.kind = var->kind != data? vglo: var->slot < 0? vloc: vreg, .ival = var->slot, .type = var->type};
      if(var->ext) { spot(self, port(self, var)); emit(self, read, 8); x = (oper){.kind = vmem, .type = var->type}; }
      break;
    }
    case '(':
      chop(self);
      if(type(self)) { x = lit(self); break; }
      x = expr(self, 1);
      skip(self, ')');
      break;
    case '&': { // &f is f
      chop(self);
      meta* f = home(self)->talk.kind == ttag? find(self, self->unit, home(self)->talk.hash): NULL;
      if(f && f->kind == call) {
        item* name = home(self)->talk.hash;
        chop(self);
        if(home(self)->talk.kind == '(') flaw(self, "cannot take the address\n");
        return hand(self, name, f);
      }
      return refr(self, atom(self));
    }
    case '*':  chop(self); return star(self, atom(self));
    case '-': case '!': case '~': {
      chop(self);
      x = atom(self);
      if(flot(self, x.type)) { // -x is -0.0 - x, !x is x == 0.0
        if(op == '~') flaw(self, "invalid operation on reals\n");
        if(x.kind == vflo) {
          if(op == '!') return cnst(self, sum(x) == 0);
          x.fval ^= (u64i)1 << 63;
          return x;
        }
        load(self, x);
        emit(self, swap);
        if(op == '-') load(self, flo(self, -0.0));
        else emit(self, move, 0);
        emit(self, flop, (op == '-'? '-': tokd('=', '=')) | WIDE | REAL);
        return acc(op == '-'? tget(self, "real"): tget(self, "int"));
      }
      if(x.kind == vnum)
        return cnst(self, op == '-'? (s32i)(0 - (u32i)x.ival): op == '!'? !x.ival: ~x.ival);
      x = load(self, x);
      u32i w = wide(x.type);
      if(op == '-') { emit(self, swap); emit(self, move, 0); emit(self, calc, '-' | w, 0, 0); }
      else emit(self, calc, (op == '!'? tokd('=', '='): '^') | w, 1, op == '!'? 0: -1);
      return acc(op == '!' || !w? tget(self, "int"): x.type);
    }
    default:
      flaw(self, "expected expression, got %x\n", op);
  }
  for(;;) { // postfix: x[i] is *(x + i), x.m, x->m is (*x).m, x(args) a call through x
    if(twin(self, '(')) x = ring(self, x);
    else if(twin(self, '[')) {
      kept  mark = keep(self, &x);
      oper  i = expr(self, 1);
      meta* t = x.type->kind == many? x.type->type: NULL;
      skip(self, ']');
      if(t && i.kind == vnum && (x.kind == vloc || x.kind == vglo))
        x = (oper){.kind = x.kind, .ival = x.ival + i.ival * (s32i)t->size, .type = t};
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
  x = truth(self, x);
  if(x.kind == vnum) {
    y = truth(self, expr(self, r + 1));
    if(!x.ival != any) { trim(self, mark); return cnst(self, any); } // y is dead
    if(y.kind == vnum) return cnst(self, !!y.ival);
    if(y.kind == vcmp) return load(self, y);
    y = load(self, y);
    emit(self, calc, tokd('!', '=') | wide(y.type), 1, 0);
    return acc(tget(self, "int"));
  }
  if(x.kind == vcmp) emit(self, cond, x.ival, any);
  else { load(self, x); emit(self, when, any); }
  mark = self->text->body;
  y = truth(self, expr(self, r + 1));
  if(y.kind == vcmp) load(self, y); // 0 or 1 already
  else emit(self, calc, tokd('!', '=') | wide(load(self, y).type), 1, 0);
  emit(self, jump);
  emit(self, back, mark);
  mark = self->text->body;
  emit(self, move, any);
  emit(self, back, mark);
  return acc(tget(self, "int"));
}

// the int x, loaded in acc, on 64 bits for a WIDE op, times scale
void  grow(mode* self, oper x, s32i scale, u32i w) {
  if(!w || wide(x.type)) return;
  sext(self, x);
  if(scale != 1) emit(self, calc, '*' | WIDE, 1, scale);
}

// x op y as pair does it, one of them a real, the other made one: + - * /,
// or a comparison left in the flags. Constants are folded by the host
oper  rpair(mode* self, u32i op, oper x, oper y, kept mark) {
  u32i  r = rank(op);
  bool  test = r == 6 || r == 7;
  if((!test && op != '+' && op != '-' && op != '*' && op != '/') || !digit(x.type) || !digit(y.type))
    flaw(self, "invalid operation on reals\n");
  if((x.kind == vnum || x.kind == vflo) && (y.kind == vnum || y.kind == vflo)) {
    double a = sum(x), b = sum(y);
    switch(op) {
      case '+':            return flo(self, a + b);
      case '-':            return flo(self, a - b);
      case '*':            return flo(self, a * b);
      case '/':            return flo(self, a / b);
      case '<':            return cnst(self, a <  b);
      case tokd('<', '='): return cnst(self, a <= b);
      case '>':            return cnst(self, a >  b);
      case tokd('>', '='): return cnst(self, a >= b);
      case tokd('=', '='): return cnst(self, a == b);
      default:             return cnst(self, a != b);
    }
  }
  u32i  k = x.kind == vacc? --self->held: 0; // the scratch of x, when kept
  if(y.kind == vnum) y = flo(self, y.ival);
  if(x.kind == vnum) x = flo(self, x.ival);
  y = load(self, y);
  if(!flot(self, y.type)) emit(self, turn, 1, wide(y.type) != 0);
  if(x.kind == vacc && k < HELD && self->dials == mark.dials) {
    emit(self, lift, mark.at, k);
    emit(self, take, k);
  }
  else if(x.kind == vacc) emit(self, pull);
  else { emit(self, swap); x = load(self, x); }
  if(!flot(self, x.type)) emit(self, turn, 1, wide(x.type) != 0);
  emit(self, flop, op | WIDE | REAL | (test? TEST: 0));
  if(test) return (oper){.kind = vcmp, .ival = (s32i)(op | WIDE | REAL), .type = tget(self, "int")};
  return acc(tget(self, "real"));
}

// x op y, y parsed after mark; x was kept there when it was in acc: in a
// scratch register unless a call came between. A comparison is left in the
// flags. pointer + n moves n elements, pointer - pointer counts them; void* by bytes
oper  pair(mode* self, u32i op, oper x, oper y, kept mark) {
  if(flot(self, x.type) || flot(self, y.type)) return rpair(self, op, x, y, mark);
  meta *ex = elem(x.type), *ey = elem(y.type), *t = tget(self, "int"); // t: type of the result
  s32i  sx = 1, sy = 1;
  u32i  w = wide(x.type) | wide(y.type), r = rank(op);
  bool  test = r == 6 || r == 7;
  if(ex || ey) {
    if(!test && ((!(op == '+' && !(ex && ey)) && !(op == '-' && ex))
                 || (ex && ex->kind == proc) || (ey && ey->kind == proc)))
      flaw(self, "invalid pointer arithmetic\n");
    if(!test && !(ex && ey)) {
      t = href(self, ex? ex: ey);
      if(ex) sy = ex->size? ex->size: 1; else sx = ey->size? ey->size: 1;
    }
  }
  else if(w && !test) t = wide(x.type)? x.type: y.type; // long
  if(t->kind == bitf) t = t->type;

  bool trap = (op == '/' || op == '%') && y.kind == vnum && !y.ival; // left to run time
  if(x.kind == vnum && y.kind == vnum && !trap)
    return cnst(self, fold(op, x.ival, y.ival));
  if(x.kind == vnum) { x.ival *= sx; sx = 1; }
  if(y.kind == vnum) { y.ival *= sy; sy = 1; }
  u32i  k = x.kind == vacc? --self->held: 0; // the scratch of x, when kept
  u32i  c = op | w | (test? TEST: 0);

  bool  slot = y.kind == vloc && (y.type->kind == base || y.type->kind == refs); // not a compound literal
  if(y.kind == vnum || ((slot || y.kind == vreg) && (!w || wide(y.type)) && self->text->body == mark.past)) {
    self->text->body = mark.at; // y emitted nothing: drop the keep
    if(test && x.kind == vreg && !slot && (!w || wide(x.type))) // a register is compared where it is
      emit(self, rop, c, x.ival, y.kind == vnum? NUM: y.ival, y.ival);
    else {
      load(self, x);
      grow(self, x, sx, w);
      if(y.kind == vnum) emit(self, calc, c, 1, y.ival);
      else if(slot) { emit(self, peek, y.ival, span(y.type)); emit(self, calc, c, 0, 0); }
      else if(plain(op)) emit(self, rop, c, ACC, y.ival, 0);
      else { emit(self, rop, '=' | w, TMP, y.ival, 0); emit(self, calc, c, 0, 0); }
    }
  }
  else {
    load(self, y);
    grow(self, y, sy, w);
    if(x.kind == vreg && sx == 1 && (!w || wide(x.type)) && (op == '+' || (r >= 3 && r <= 6)))
      emit(self, rop, c, ACC, x.ival, 0); // the same either way round: the register is the right operand
    else {
      if(x.kind == vacc && k < HELD && self->dials == mark.dials) {
        emit(self, lift, mark.at, k);
        emit(self, take, k);
      }
      else if(x.kind == vacc) emit(self, pull);
      else { emit(self, swap); load(self, x); }
      grow(self, x, sx, w);
      emit(self, calc, c, 0, 0);
    }
  }
  if(test) return (oper){.kind = vcmp, .ival = op | w, .type = tget(self, "int")};
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
  meta* t = x.type, *f = t->kind == bitf? t: NULL;
  if(f) t = f->type; // the value is of its integer type
  if(x.kind == vnum || x.kind == vacc || x.kind == vcmp || x.kind == vflo
     || t->kind == many || t->kind == list || t->kind == proc)
    flaw(self, "cannot assign\n");
  if(x.kind == vreg) { // in the register itself when it takes op; the value stays there
    kept  mark = keep(self, &x); // which keeps nothing
    oper  y = expr(self, 1);
    meta* e = elem(t);
    u32i  w = wide(t);
    if(op != '=' && (!plain(op) || elem(y.type) || flot(self, y.type) || (e && (rank(op) != 9 || e->kind == proc)))) {
      y = pair(self, op, x, y, mark);
      op = '=';
    }
    y = bend(self, y, t); // a register holds no real
    s32i  by = NUM, n = op != '=' && e && e->size? e->size: 1; // pointer += n moves n elements
    if(y.kind == vnum) y.ival *= n;
    else if(y.kind == vreg && n == 1 && (!w || wide(y.type))) by = y.ival;
    else {
      if(!wide(load(self, y).type) && w) emit(self, cast, 1);
      if(n != 1) emit(self, calc, '*' | WIDE, 1, n);
      by = ACC;
    }
    emit(self, rop, op | w, x.ival, by, y.ival);
    if(span(t) < 4) emit(self, rop, '&', x.ival, NUM, (1 << span(t) * 8) - 1); // as a slot would hold it
    return x;
  }
  if(x.kind == vglo) { spot(self, x.ival); x.kind = vmem; }
  byte* at = self->text->body;
  if(x.kind == vmem) emit(self, push); // the address, op= reads through it
  kept  mark = op == '='? (kept){0}: keep(self, &x);
  byte* past = self->text->body;
  oper  y = expr(self, 1);
  if(op != '=') y = pair(self, op, x, y, mark);
  y = bend(self, y, t);
  bool  slot = y.kind == vloc && (y.type->kind == base || y.type->kind == refs);
  if(x.kind == vmem && op == '=' && !f && self->text->body == past
     && (y.kind == vnum || ((slot || y.kind == vreg) && (!wide(t) || wide(y.type))))) {
    self->text->body = at; // y emitted nothing: the address stays in acc, y goes to tmp
    if(slot) emit(self, peek, y.ival, span(y.type) < span(t)? span(y.type): span(t));
    else emit(self, rop, '=' | wide(t), TMP, y.kind == vnum? NUM: y.ival, y.ival);
    emit(self, fill, span(t));
  }
  else {
    if(!wide(t) != !wide(load(self, y).type)) { if(wide(t)) sext(self, y); else emit(self, cast, 0); }
    if(f) return sow(self, x, f);
    if(x.kind == vloc) emit(self, stor, x.ival, span(t));
    else { emit(self, pull); emit(self, fill, span(t)); }
  }
  if(span(t) < 4) emit(self, calc, '&', 1, (1 << span(t) * 8) - 1); // as it was stored
  return acc(t);
}

// precedence climbing over binary operators of rank >= least
oper  expr(mode* self, int least) {
  oper x = atom(self);
  u32i op = sets(home(self)->talk.kind);
  if(op && least <= 1) { chop(self); return give(self, op, x); }
  for(u32i r; (r = rank(op = home(self)->talk.kind)) >= (u32i)least; ) {
    chop(self);
    if(op == tokd('&', '&') || op == tokd('|', '|')) { x = both(self, op, x, r); continue; }

    kept  mark = keep(self, &x); // y may need acc
    x = pair(self, op, x, expr(self, r + 1), mark);
  }
  return x;
}

// (condition): a branch taken when it fails, patched by back. A comparison
// branches on its flags
void  gate(mode* self) {
  skip(self, '(');
  oper  x = truth(self, expr(self, 1));
  skip(self, ')');
  if(x.kind == vreg) { // compared with 0 where it is
    emit(self, rop, tokd('!', '=') | TEST | wide(x.type), x.ival, NUM, 0);
    x = (oper){.kind = vcmp, .ival = tokd('!', '='), .type = tget(self, "int")};
  }
  if(x.kind == vcmp) emit(self, cond, x.ival, 0);
  else { load(self, x); emit(self, when, 0); }
}

#define seed(self, ...) {\
  meta x = { __VA_ARGS__ }; \
  meta* y = cell((self), &(self)->pile, &(self)->piles, (self)->top++); \
  x.unit = self->unit; \
  x.hide = x.name->data; \
  x.name->data = y; \
  *y = x; }

#define crux(self, str, ...) \
  seed((self), .kind = base, .name = sget((self), (str)), __VA_ARGS__)

// leave a scope: the names declared since top mean what they did before
void  bury(mode* self, u32i top) {
  while(self->top > top) {
    meta* x = pin(self->pile, --self->top);
    x->name->data = x->hide;
    if(x->kind == data && x->slot >= 0) self->busy &= ~(1u << x->slot);
  }
}

// fresh frame slots for size bytes
s32i  room(mode* self, u32i size) {
  self->slot += (size + 7) & -8;
  if(self->deep < self->slot) self->deep = self->slot;
  return -self->slot;
}

// where a local named name, of type t and size bytes, lives: the register
// sift gave the name, when t fits one and no local in scope has it, else
// fresh frame slots
s32i  stay(mode* self, const item* name, const meta* t, u32i size) {
  u32i  r = name->pick - 1;
  if(!name->pick || !fits(self, t) || self->busy >> r & 1) return room(self, size);
  self->busy |= 1u << r;
  return (s32i)r;
}

meta* bulk(mode* self, item* name);

// a type name: struct name, or a type name with an optional struct before,
// or struct name? { ... }, which defines one
meta* sort(mode* self) {
  if(same(self, "struct")) {
    chop(self);
    item* name = home(self)->talk.kind == ttag? home(self)->talk.hash: NULL;
    if(name) chop(self);
    if(home(self)->talk.kind == '{') return bulk(self, name);
    if(!name) flaw(self, "expected a struct name or {\n");
    meta* type = find(self, self->unit, name);
    if(!type || (type->kind != list && type->kind != base))
      flaw(self, "no struct '%.*s'\n", nick(name));
    return type;
  }
  meta* type = ilk(self);
  if(!type) {
    if(home(self)->talk.kind == ttag)
      flaw(self, "expected type, got '%.*s'\n", nick(home(self)->talk.hash));
    flaw(self, "expected type, got %x\n", home(self)->talk.kind);
  }
  chop(self);
  return type;
}

meta* coil(mode* self, meta* type, item** name);

// (parameters) after a declarator: the type of a function returning type. The
// names of the parameters may be left out, they are not declared
meta* plan(mode* self, meta* type) {
  u32i  argc = 0;
  byte  longs = 0, reals = 0;
  bool  more = 0;
  skip(self, '(');
  for(; !twin(self, ')'); argc++, twin(self, ',')) {
    if(twin(self, tokd('.', '.', '.'))) { more = 1; skip(self, ')'); break; }
    item* n;
    const meta* t = coil(self, sort(self), &n);
    if(!n && !t->size && !argc && twin(self, ')')) break; // (void)
    if(argc < 6 && wide(t)) longs |= 1 << argc;
    if(argc < 6 && flot(self, t)) reals |= 1 << argc;
  }
  if(argc > 6) flaw(self, "more than 6 parameters\n");
  return mold(self, type, argc, longs, reals, more);
}

// a declarator of type: pointers to type with the name into *name, NULL when
// there is none, or one of a pointer to a function, (*name[n]...)(parameters),
// with its arrays of pointers
meta* coil(mode* self, meta* type, item** name) {
  while(twin(self, '*')) type = href(self, type);
  *name = NULL;
  if(!twin(self, '(')) {
    if(home(self)->talk.kind == ttag) { *name = home(self)->talk.hash; chop(self); }
    return type;
  }
  u32i  refn = 0, dimn = 0;
  s32i  dim[8];
  while(twin(self, '*')) refn++;
  if(!refn) flaw(self, "expected a pointer to a function\n");
  if(home(self)->talk.kind == ttag) { *name = home(self)->talk.hash; chop(self); }
  while(twin(self, '[')) {
    oper n = expr(self, 1);
    skip(self, ']');
    if(n.kind != vnum || n.ival <= 0) flaw(self, "array size must be a positive constant\n");
    if(dimn == sizeof dim / sizeof *dim) flaw(self, "too many array sizes\n");
    dim[dimn++] = n.ival;
  }
  skip(self, ')');
  type = plan(self, type);
  while(refn--) type = href(self, type);
  while(dimn--) type = knot(self, many, type, dim[dimn] * type->size);
  if(home(self)->talk.kind == '(' || home(self)->talk.kind == '[')
    flaw(self, "a function cannot return a function or an array\n");
  return type;
}

// a declarator of type that names: its type, the name into *name
meta* term(mode* self, meta* type, item** name) {
  type = coil(self, type, name);
  if(!*name) flaw(self, "expected name, got %x\n", home(self)->talk.kind);
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
meta* full(mode* self, meta* type, const item* name) {
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
void  bits(mode* self, s32i slot, const byte* src, u32i size) {
  if(!src && size) emit(self, move, 0);
  for(u32i n; size; slot += n, size -= n) {
    n = size >= 8 && !src? 8: size >= 4? 4: size >= 2? 2: 1;
    if(src) { s32i v = 0; copy((byte*)&v, src, n); src += n; emit(self, move, v); }
    emit(self, stor, slot, n);
  }
}

// an array or struct being initialized, at off in the braced one, and the
// part of it at hand: member memb of a struct, element i of an array
typedef struct { meta* type; meta* memb; u32i i, off; } tier;

#define bulky(t) ((t)->kind == many || (t)->kind == list)
// a string for a char array
#define text(self, t) ((t)->kind == many && (t)->type->size == 1 && home(self)->talk.kind == '"')
#define TIERS 16

// a tier for type at off, at its first part
tier  dive(const mode* self, u32i depth, meta* type, u32i off) {
  if(depth == TIERS) flaw(self, "initializer nested too deep\n");
  return (tier){type, type->kind == list? type->type: NULL, 0, off};
}

// the part of n at hand and its offset into off, NULL past the end of n
meta* pick(tier* n, u32i* off) {
  if(n->type->kind == list) {
    if(!n->memb) return NULL;
    *off = n->off + n->memb->slot;
    return n->memb->type;
  }
  meta* t = n->type->type;
  if((n->i + 1) * t->size > n->type->size) return NULL;
  *off = n->off + n->i * t->size;
  return t;
}

void  step(tier* n) { if(n->type->kind == list) n->memb = n->memb->hide; else n->i++; }

// a designator, .name or [index], picks the part of n
void  aim(mode* self, tier* n) {
  if(twin(self, '.')) { n->memb = mate(self, n->type); return; }
  skip(self, '[');
  if(n->type->kind != many) flaw(self, "not an array\n");
  oper i = expr(self, 1);
  skip(self, ']');
  if(i.kind != vnum || i.ival < 0 || (u32i)i.ival >= n->type->size / n->type->type->size)
    flaw(self, "array index must be a constant in bounds\n");
  n->i = i.ival;
}

// the initializer of type: { a list }, the rest zero, a string for a char
// array, or a value; a global's (glob) is a constant written to data at
// offset slot, a local's is stored to frame slot, or to register slot when
// it is not below 0. A list goes over the members
// of a struct or the elements of an array in order, or from the part a
// designator chain (.name, [index]) picks; a part that is a struct or array
// takes the values that come when they are not in braces. A value or a string
// may be in braces
void  seal(mode* self, meta* type, bool glob, s32i slot) {
  const meta* t = type->kind == many? type->type: NULL;
  if(glob && bulky(type)) // as a whole, again
    for(u32i i = 0; i < type->size; i++) self->data->head[slot + i] = 0;
  bool  brace = twin(self, '{');
  if(brace && (!bulky(type) || text(self, type))) {
    seal(self, type, glob, slot);
    twin(self, ',');
    skip(self, '}');
  }
  else if(brace) {
    tier  k[TIERS] = {dive(self, 0, type, 0)};
    u32i  d = 1, end = 0, off = 0; // depth of k; the bytes before end are set
    while(!twin(self, '}')) {
      meta* p;
      u32i  c = home(self)->talk.kind;
      if(c == '.' || c == '[') { // the chain starts from the braced tier
        aim(self, k + (d = 1) - 1);
        for(; (c = home(self)->talk.kind) == '.' || c == '['; aim(self, k + d - 1)) {
          p = pick(k + d - 1, &off);
          k[d] = dive(self, d, p, off);
          d++;
        }
        skip(self, '=');
        p = pick(k + d - 1, &off);
      }
      else {
        while(!(p = pick(k + d - 1, &off)) && d > 1) step(k + --d - 1);
        if(!p) flaw(self, "too many initializers\n");
      }
      while(bulky(p) && home(self)->talk.kind != '{' && !text(self, p)) {
        k[d] = dive(self, d, p, off);
        if(!(p = pick(k + d++, &off))) flaw(self, "too many initializers\n");
      }
      u32i  to = off + (p->kind == bitf? p->size: 0); // a bit-field reads its unit
      if(!glob && end < to) bits(self, slot + end, NULL, to - end);
      seal(self, p, glob, slot + off);
      if(end < off + p->size) end = off + p->size;
      step(k + d - 1);
      if(!twin(self, ',')) { skip(self, '}'); break; }
    }
    if(!glob) bits(self, slot + end, NULL, type->size - end);
  }
  else if(text(self, type)) {
    u32i n = quote(home(self)->talk.hash, NULL);
    if(n > type->size) flaw(self, "string too long\n");
    if(glob) quote(home(self)->talk.hash, self->data->head + slot);
    else {
      byte* b = self->heap(NULL, n);
      quote(home(self)->talk.hash, b);
      bits(self, slot, b, n);
      bits(self, slot + n, NULL, type->size - n);
      self->heap(b, 0);
    }
    chop(self);
  }
  else if(glob) {
    oper x = expr(self, 1);
    if(x.kind == vnum || x.kind == vflo) x = bend(self, x, type);
    byte* at = self->data->head + slot; // after expr: a string in it moves data
    if((x.kind != vnum && x.kind != vflo) || t || type->kind == list)
      flaw(self, "expected a constant\n");
    s64i v = x.ival;
    if(x.kind == vflo) copy(at, &x.fval, 8);
    else if(type->kind == bitf) { // into the bits of its unit
      u64i u = 0;
      copy((byte*)&u, at, type->size);
      u ^= (u ^ (u64i)v << type->slot) & ones(type);
      copy(at, (byte*)&u, type->size);
    }
    else copy(at, &v, type->size);
  }
  else give(self, '=', (oper){.kind = slot < 0? vloc: vreg, .ival = slot, .type = type});
}

// (type){ init }, after the "(": a compound literal, a local that lives to the
// end of the block
oper  lit(mode* self) {
  meta* type = sort(self);
  while(twin(self, '*')) type = href(self, type);
  type = dims(self, type);
  skip(self, ')');
  if(!type->size) flaw(self, "a compound literal without a size\n");
  if(home(self)->talk.kind != '{') flaw(self, "expected a compound literal\n");
  s32i  slot = room(self, type->size);
  self->lits = self->slot;
  seal(self, type, 0, slot);
  return (oper){.kind = vloc, .ival = slot, .type = type};
}

// type declarator (= init)? (, declarator (= init)?)* ;
void  vars(mode* self) {
  meta* base = sort(self);
  if(twin(self, ';')) return; // a struct, defined
  do {
    item* name;
    meta* type = noun(self, base, &name);
    s32i  slot = stay(self, name, type, type->size);
    seed(self, .name=name, .type=type, .kind=data, .slot=slot);
    if(twin(self, '=')) seal(self, type, 0, slot);
  } while(twin(self, ','));
  skip(self, ';');
}

// fail when a global or function is defined again. prev, what the name meant
// before, is of this unit, or, unless the new one is static (own), of another
// that makes it public, but not as a weak definition. Nor may another unit
// define it as public when the new one is
void  once(mode* self, const meta* prev, item* name, bool own) {
  const meta* x = own? NULL: find(self, 0, name);
  if((prev && (prev->kind == glob || prev->kind == call)
      && (prev->unit == self->unit || (!own && !prev->weak)))
     || (x && (x->kind == glob || x->kind == call) && heft(x) == 4))
    flaw(self, "'%.*s' is already defined\n", nick(name));
}

// the type of a bit-field of integer type t, width bits from bit at in its unit
meta* nibs(mode* self, meta* t, u32i at, u32i width) {
  return sole(self, (meta){.kind = bitf, .type = t, .size = t->size, .slot = at, .longs = (byte)width});
}

// { type declarator (: width)? (, declarator (: width)?)* ; ... } after
// "struct" name?, name NULL when it has none. A member is aligned to its size,
// the struct to its widest member. A bit-field takes the bits after the one
// before unless they would cross a unit of its type, a width 0 without a name
// ends the unit. A struct without a name nor a declarator gives its members
meta* bulk(mode* self, item* name) {
  meta* type;
  if(name) { // seeded first: members may point to it
    once(self, find(self, self->unit, name), name, 0);
    seed(self, .name=name, .kind=list);
    type = pin(self->pile, self->top - 1);
  }
  else type = mint(self, (meta){.kind = list});
  meta** next = &type->type;
  skip(self, '{');
  u32i  bit = 0, align = 1; // bits laid out
  while(!twin(self, '}')) {
    meta* of = sort(self); // the type the declarators are of
    if(twin(self, ';')) { // a struct, defined: its members are this one's when it has no name
      if(of->kind != list || of->name) continue;
      u32i  a = lean(of);
      bit = (bit + 8 * a - 1) & ~(8 * a - 1);
      for(meta* m = of->type; m; m = m->hide) {
        *next = mint(self, (meta){.name = m->name, .type = m->type, .slot = bit / 8 + m->slot});
        next = &(*next)->hide;
      }
      bit += 8 * of->size;
      if(align < a) align = a;
      continue;
    }
    do {
      item* n = NULL;
      meta* t = home(self)->talk.kind == ':'? of: noun(self, of, &n);
      u32i  a = lean(t), at = bit;
      if(twin(self, ':')) {
        if(t->kind != base || !t->size || flot(self, t)) flaw(self, "a bit-field must be an integer\n");
        oper  k = expr(self, 1);
        u32i  u = 8 * t->size;
        if(k.kind != vnum || k.ival < 0 || (u32i)k.ival > u || (!k.ival && n))
          flaw(self, "bit-field width must be a constant from %d to %d\n", !!n, u);
        if(!k.ival || bit / u != (bit + k.ival - 1) / u) bit = (bit + u - 1) & ~(u - 1); // the next unit
        at = bit;
        bit += k.ival;
        if(!n) continue;
        t = nibs(self, t, at % u, k.ival);
        at = at / u * u;
      }
      else {
        at = bit = (bit + 8 * a - 1) & ~(8 * a - 1);
        bit += 8 * t->size;
      }
      *next = mint(self, (meta){.name = n, .type = t, .slot = at / 8});
      next = &(*next)->hide;
      if(align < a) align = a;
    } while(twin(self, ','));
    skip(self, ';');
  }
  type->size = ((bit + 7) / 8 + align - 1) & ~(align - 1);
  type->slot = align;
  return type;
}

void  stmt(mode* self);

#define SIFT  64         // names sift follows in a function
#define NEVER (1u << 31) // item.pick of one of them: its locals stay in the frame

// name among the names sifted: its pick, 1 plus its uses; 0 when there are
// too many
static u32i  sown(item** c, u32i* n, item* name) {
  if(!name->pick && *n < SIFT) (c[(*n)++] = name)->pick = 1;
  return name->pick;
}

// the prescan of a function at the { of its body, its parameters in scope
// from top: the tokens up to the matching } are walked, not parsed, and the
// lexer put back. It follows the names of the numbers and pointers declared
// (the parameters; a name after a type that starts a statement and its *s,
// and after a , of that statement): each use counts, 8 times per loop
// around it, and the most used names, of 3 uses at least, get a register
// each (item.pick), which the locals so named take (stay). A name whose
// address is taken anywhere in the body, as &name or within &(...), gets
// none: a use before the code for it exists is what a single pass cannot see
void  sift(mode* self, u32i top) {
  if(!self->arch.regs) return;
  unit* u = home(self);
  byte* at = u->code->body;
  u64i  line = u->line;
  word  talk = u->talk;
  item* c[SIFT];
  item *loop = sget(self, "while"), *when = sget(self, "when"), *tag = sget(self, "struct");
  item *ret = sget(self, "return"), *alt = sget(self, "else"), *len = sget(self, "sizeof");
  item *last = NULL, *decl = NULL; // the name before, when the token was one; the name just declared
  const meta* of = NULL; // the type of the declaration at hand
  u32i  n = 0, depth = 0, loops = 0, end[8], cond = 0, deep = 0, p = 0; // p: the token before
  s32i  lvl = 0; // brackets open in the declaration
  bool  ok = 1, arm = 0, stars = 0, amp = 0; // arm: a declared name may follow; amp: a unary & came before
  for(u32i i = top; ok && i < self->top; i++) {
    const meta* x = pin(self->pile, i);
    if(!(ok = sown(c, &n, x->name))) break;
    x->name->pick += 1;
    if(!fits(self, x->type)) x->name->pick |= NEVER;
  }
  do {
    u32i  k = u->talk.kind, w = 1u << 3 * (loops < 3? loops: 3);
    item* it = k == ttag && p != '.' && p != tokd('-', '>')? u->talk.hash: NULL; // not a member
    item* was = decl;
    // an operand ends before the token: a & after it is no address
    bool  val = p == tnum || p == treal || p == ')' || p == ']' || (p == ttag && last != ret && last != alt && last != len);
    bool  shut = 0; // the ) of a condition: a statement follows
    decl = NULL;
    depth += (k == '{') - (k == '}');
    if(cond) { cond += (k == '(') - (k == ')'); if(cond == 1) cond = 0, shut = 1; }
    else if(it && (it == loop || it == when)) { cond = 1; if(it == loop && loops < 8) end[loops++] = depth; }
    else if(k == ';' || k == '}') while(loops && end[loops - 1] == depth) loops--;

    const meta* m = it && !it->pick && !cond && it != ret && (p == '{' || p == '}' || p == ';')? find(self, self->unit, it): NULL;
    if(m && (m->kind == base || m->kind == list)) { of = m; arm = 1; stars = 0; lvl = 0; }
    else if(it) {
      if(arm || amp || deep) { // declared, or its address is taken
        if(!(ok = sown(c, &n, it))) break;
        if(amp || deep || !(stars || fits(self, of))) it->pick |= NEVER;
        if(arm) decl = it;
      }
      if(it->pick && (it->pick & ~NEVER) < 1u << 28) it->pick += w;
      arm = 0;
    }
    else if(k == '*' && arm) stars = 1;
    else if(k == ',' && of && !lvl) { arm = 1; stars = 0; }
    else {
      if(k == '[' && was) was->pick |= NEVER; // an array
      if(k == ';') of = NULL;
      lvl += (k == '(' || k == '[' || k == '{') - (k == ')' || k == ']' || k == '}');
      arm = 0;
    }
    if(deep) deep += (k == '(') - (k == ')');
    else if(amp && k == '(') deep = 1;
    amp = k == '&' && !val;
    if(it != tag || !it) { p = shut? ';': k; last = it; } // struct name is a type as name is
    chop(self);
  } while(depth && u->talk.kind);
  u->code->body = at, u->line = line, u->talk = talk;

  while(ok && self->regn < self->arch.regs) {
    u32i b = n;
    for(u32i j = 0; j < n; j++)
      if(c[j]->pick > 3 && c[j]->pick < NEVER && (b == n || c[j]->pick > c[b]->pick)) b = j;
    if(b == n) break;
    (self->pick[self->regn++] = c[b])->pick = 0;
  }
  while(n) c[--n]->pick = 0;
  for(u32i r = 0; r < self->regn; r++) self->pick[r]->pick = r + 1;
}

// the registers of locals back as the caller had them, from the slots they
// were saved in, and the epilogue
void  fini(mode* self) {
  for(u32i r = 0; r < self->regn; r++) emit(self, stow, r, -8 * (s32i)(r + 1), 1);
  emit(self, done);
}

// loop body: skip jumps back to again, break past the loop
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
  s32i  slot = self->slot, lits = self->lits;
  if(twin(self, '{')) {
    while(!twin(self, '}')) stmt(self);
    self->lits = lits;
  }
  else if(twin(self, ';')) {
  }
  else if(type(self)) {
    vars(self);
    return; // the names stay in the enclosing scope
  }
  else if(same(self, "when")) {
    byte* mark = NULL;
    chop(self);
    gate(self);
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
    gate(self);
    byte* out = self->text->body;
    iter(self, cond);
    emit(self, back, out);
  }
  else if(same(self, "break")) {
    chop(self); skip(self, ';');
    if(!self->cont) flaw(self, "break outside a loop\n");
    emit(self, jump);
    self->quit = swell(self, self->quit, self->ends, &self->endc, sizeof(byte*), 16);
    self->quit[self->ends++] = self->text->body;
  }
  else if(same(self, "skip")) {
    chop(self); skip(self, ';');
    if(!self->cont) flaw(self, "skip outside a loop\n");
    emit(self, loop, self->cont);
  }
  else if(same(self, "return")) {
    chop(self);
    if(home(self)->talk.kind != ';') {
      meta* t = self->func->type;
      oper  x = bend(self, expr(self, 1), t);
      if(!wide(t) != !wide(load(self, x).type)) { if(wide(t)) sext(self, x); else emit(self, cast, 0); }
    }
    skip(self, ';');
    fini(self);
  }
  else {
    expr(self, 1); skip(self, ';');
  }
  bury(self, top);
  self->slot = slot < self->lits? self->lits: slot; // the compound literals of the block stay
}

// x, declared without a body or as extern, is an import from the library of
// the last load, or of "c". prev, what the name meant before, may be the same
// import, which keeps its slot, or C code of an object or an archive, which x
// gives a prototype: the calls go to the definition (cabi)
void  hire(mode* self, meta* x, const meta* prev) {
  x->ext = 1;
  if(x->kind == call && prev && (prev->kind == pend || (prev->kind == call && prev->ext && !prev->lib)))
    return;
  x->lib = home(self)->from? home(self)->from: rack(self, sget(self, "\"c\""));
  if(!prev || !prev->ext || prev->addr || prev->kind != x->kind || prev->lib != x->lib
     || prev->type != x->type || prev->size != x->size || prev->more != x->more)
    once(self, prev, x->name, x->own);
  else x->slot = prev->slot;
}

// the string after link or load
item* bond(mode* self) {
  item* s = home(self)->talk.hash;
  if(home(self)->talk.kind != '"') flaw(self, "expected a string\n");
  chop(self);
  return s;
}

void  decl(mode* self) {
  // load "name" ";": a shared library, the bodyless functions after it in this
  // source file are imported from it
  if(same(self, "load")) {
    chop(self);
    home(self)->from = rack(self, bond(self));
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

  // declaration begins with a type, or only defines a struct:
  // "struct" name? "{" decl "}" ";"
  item* name;
  meta* base = sort(self);
  if(twin(self, ';')) return;
  meta* type = term(self, base, &name);

  // type name "(" args ")" "{" stmt "}"     // callable decl
  // type name "(" args ")" ";"               // import
  if(twin(self, '(')) {
    const meta* prev = find(self, self->unit, name);
    seed(self, .name=name, .type=type, .kind=call, .own=own);
    meta* func = self->func = pin(self->pile, self->top - 1);
    u32i  top  = self->top;
    bool  anon = 0;
    self->slot = self->deep = self->lits = self->busy = 0;
    for(; !twin(self, ')'); func->size++, twin(self, ',')) {
      if(twin(self, tokd('.', '.', '.'))) { func->more = 1; skip(self, ')'); break; }
      item* n;
      meta* of = coil(self, sort(self), &n);
      if(!n && !of->size && !func->size && twin(self, ')')) break; // (void)
      if(!n) { anon = 1; continue; } // an import may leave it out
      if(wide(of)) func->longs |= 1 << func->size;
      if(flot(self, of)) func->reals |= 1 << func->size;
      seed(self, .name=n, .type=of, .kind=data, .slot=-1); // placed once the body is sifted
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
    if(home(self)->talk.kind != '{') flaw(self, "expected function body\n");
    byte* hail = self->text->body;
    emit(self, hail, self->form.os, func->size, func->reals | flot(self, func->type) << RETS);
    func->addr = self->text->body;
    func->hail = hail != func->addr? hail: NULL;
    sift(self, top);
    self->slot = self->deep = 8 * self->regn; // the first slots save the registers of locals
    emit(self, open);
    for(u32i r = 0; r < self->regn; r++) emit(self, stow, r, -8 * (s32i)(r + 1), 0);
    for(u32i i = 0; i < func->size; i++) {
      meta* p = pin(self->pile, top + i);
      p->slot = stay(self, p->name, p->type, 8);
      if(p->slot < 0) { emit(self, parm, i, p->slot); continue; }
      emit(self, rop, '=' | wide(p->type), p->slot, ARG - (s32i)i, 0);
      if(span(p->type) < 4) emit(self, rop, '&', p->slot, NUM, (1 << span(p->type) * 8) - 1); // as a slot would hold it
    }
    stmt(self);
    emit(self, move, 0); fini(self); // implicit return 0
    emit(self, size, func->addr, (self->deep + 15) & -16);
    bury(self, top);
    while(self->regn) self->pick[--self->regn]->pick = 0;
    reel(self); // between functions: members go after text
    return;
  }

  // globals: type name ([n])* (= init)? (, declarator)* ;
  for(type = full(self, type, name);; type = noun(self, base, &name)) {
    if(ext) {
      const meta* prev = find(self, self->unit, name);
      seed(self, .name=name, .type=type, .kind=glob, .own=own);
      hire(self, pin(self->pile, self->top - 1), prev);
      if(!twin(self, ',')) break;
      continue;
    }
    once(self, find(self, self->unit, name), name, own);
    u32i off = lay(self, type->size, home(self)->talk.kind != '=', 8);
    if(twin(self, '=')) seal(self, type, 1, (s32i)off);
    seed(self, .name=name, .type=type, .kind=glob, .slot=off, .own=own);
    if(!twin(self, ',')) break;
  }
  skip(self, ';');
}

// the unit at, its file read in code: an object is taken, the symbol index of
// an archive read, a source parsed: when it ends, its forward calls to
// functions now known are bound. The unit being parsed is back after: each
// source keeps its own place (line, talk, from) in its unit. The file goes
// once read, its names have their own bytes; an archive's when the run ends
void  feed(mode* self, u32i at) {
  file* code = self->units[at].code; // nest may move the units
  byte  kind = self->units[at].kind;
  const char* path = self->units[at].path;
  u32i  fixs = self->fixs, back = self->unit;
  self->unit = at;
  if(kind == un_arch) {
    if(!self->ario.load) fail("%s: no archives in this build\n", path);
    self->ario.load(self, code);
  }
  else if(kind != un_code) {
    if(!self->form.take) fail("%s: no objects in this form\n", path);
    self->form.take(self, code);
    self->heap(code, 0);
    self->units[at].code = NULL;
  } else {
    for(chop(self); home(self)->talk.kind; ) decl(self);
    tie(self, fixs, 0);
    self->heap(code, 0);
    self->units[at].code = NULL;
  }
  self->unit = back;
}

// the file at path, read in code, into the pass as a unit of kind unless it
// is in already: 1 when it was not. The unit joins the list as it starts, so
// a cycle back into it sees what it has defined so far
bool  join(mode* self, const char* path, u64i size, file* code, byte kind) {
  for(const unit* u = self->units + 1; kind != un_memb && u < self->units + self->unitn; u++)
    if(u->kind != un_memb && alike(u->path, path, size + 1)) { self->heap(code, 0); return 0; }
  feed(self, nest(self, (unit){.path = path, .code = code, .kind = kind}));
  return 1;
}

// path, size bytes from mode.heap, tidy'd and joined as a unit of kind with
// code, read when NULL: 0, path still the caller's, when it cannot be read.
// Freed when the file is in already
bool  land(mode* self, char* path, u64i size, file* code, byte kind) {
  size = tidy(path, size);
  if(!code && !(code = self->scan(path))) return 0;
  if(!join(self, path, size, code, kind)) self->heap(path, 0);
  return 1;
}

// the file at name, named on the command line, as a unit of kind; read when
// code is NULL, fatal when it cannot be
void  lead(mode* self, const char* name, file* code, byte kind) {
  u64i  size = slen(name);
  char* path = self->heap(NULL, size + 1);
  copy((byte*)path, name, size + 1);
  if(!land(self, path, size, code, kind)) fail("cannot open: %s\n", name);
}

void  make(mode* self) {
  // unit = callable
  //      | variable
  //      | typedecl

  self->core = (core){dock, gap, lay, hget, find, heft, ask, sign, knit};
  self->text = (file*)self->hold(0, CODE);
  self->text->body = self->text->head;
  self->text->tail = (byte*)self->text + CODE;
  self->data = zero(self, sizeof(file) + DATA);
  self->data->body = self->data->head;
  self->data->tail = self->data->head + DATA;

  emit(self, boot);

  self->units[0].path = self->name; // the root, made by main
  crux(self, "void", .size=0);
  crux(self, "bool", .size=1);
  crux(self, "byte", .size=1);
  crux(self, "char", .size=1);
  crux(self, "nu16", .size=2);
  crux(self, "int",  .size=4);
  crux(self, "long", .size=8);
  crux(self, "real", .size=8);

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

  // nothing is pulled or called forward from here on: the archives and the
  // lists of asks and forward calls can go
  for(unit* u = self->units + 1; u < self->units + self->unitn; u++)
    if(u->kind == un_arch) { self->heap(u->code, 0); u->code = NULL; }
  self->askx.slot = self->heap(self->askx.slot, 0);
  self->askx.size = self->askx.used = 0;
  self->link = self->heap(self->link, 0);
  self->fixc = 0;

  // without main, a library: the boot stub stays unused at the start of text
  meta* func = find(self, 0, sget(self, "main"));
  if(func && func->kind == call && func->addr) {
    self->main = self->boot = func->addr;
    self->hail = func->hail? func->hail: func->addr;
    if(libc && libc->kind == call && libc->ext && libc->addr) {
      if(self->impn && self->libs) fail("cannot call a library from a program with a C library linked in\n");
      self->boot = self->text->body;
      emit(self, crt, self->main, libc->addr, cl_glibc);
    }
    // a Linux program written with imports starts through the shared libc
    // the same way, its __libc_start_main (bionic's __libc_init) an import
    // too (libs is set only when writing: running binds the imports itself)
    else if(self->impn && self->libs && self->form.os == os_linux) {
      item* start = self->clib == cl_bionic? sget(self, "__libc_init"): sget(self, "__libc_start_main");
      if(self->clib == cl_bionic) libc = find(self, 0, start);
      if(!libc || libc->kind != call || !libc->lib)
        libc = mint(self, (meta){.name = start, .kind = call, .ext = 1,
                                 .type = tget(self, "int"),
                                 .lib = rack(self, sget(self, "\"c\""))});
      self->boot = self->text->body;
      spot(self, port(self, libc));
      emit(self, crt, self->main, NULL, self->clib);
    }
    emit(self, link, self->text->head, self->boot);
  }
  else if(!self->libs) fail("no main function\n");

  self->show(self);
  self->drop(self->text, CODE);
  self->heap(self->data, 0);
}
