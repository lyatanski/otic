// vim: ts=2:sw=2
#include "core.h"

enum { tend, ttag, tnum, treal };
// text is reserved whole, so code points into it; data doubles up to WANT
// and moves: refer to it by offset
#define CODE (1u << 30)
#define DATA (1u << 16)
#define tok1(x) ((x)+=1, tokd((x)[-1]))
#define tok2(x) ((x)+=2, tokd((x)[-2], (x)[-1]))
#define tok3(x) ((x)+=3, tokd((x)[-3], (x)[-2], (x)[-1]))
// k: a string literal
#define sget(self, k) hget(self, (self)->unit, k, sizeof(k)-1)
// the builtin type k, even where a source redeclares its name
#define tget(self, k) ((self)->b##k)

char  esc(char v) { return v == 'n'? '\n': v == 't'? '\t': v == 'r'? '\r': v == '0'? 0: v; }

static void lex(mode* self) {
  u64i  ival = 0;
  u64i  fval = 0;
  u32i  kind = tend;
  byte* c = home(self)->code->body;

  pass: switch(*(home(self)->code->body = c)) {
    case '\n': home(self)->line++; // fallthrough
    case ' ': case '\f': case '\t': case '\v': case '\r': c++; goto pass;
    case '#': // #! line of a script
      if(c[1] == '!' && c == home(self)->code->head) { while(*c && *c != '\n') c++; goto pass; }
      kind = tok1(c); break;
    case ',': case ';': case ':': case '?': case '~': case '@':
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
    case '\'':
      c++;
      if(*c == '\\' && c[1]) ival = (byte)esc(*++c);
      else if(*c && *c != '\n' && *c != '\'') ival = (byte)*c;
      else flaw(self, "expected one character between ' and '\n");
      if(*++c != '\'') flaw(self, "expected one character between ' and '\n");
      c++;
      kind = tnum; break;
    case '"':
      for(c++; *c != '"'; c++) {
        if(!*c) flaw(self, "unterminated string\n");
        if(*c == '\\') c++;
      }
      c++;
      kind = '"'; break;
    case '.':
      if     (c[1] == '.' && c[2] == '.') { kind = tok3(c); break; }
      else if(c[1] <  '0' || c[1] >  '9') { kind = tok1(c); break; }
      // fallthrough
    case '0': case '1': case '2': case '3': case '4':
    case '5': case '6': case '7': case '8': case '9':
      while(*c >= '0' && *c <= '9') ival = ival * 10 + *c++ - '0';
      // a real, rounded by the host's strtod
      if(*c == '.' || ((*c == 'e' || *c == 'E') && ((c[1] >= '0' && c[1] <= '9')
         || ((c[1] == '+' || c[1] == '-') && c[2] >= '0' && c[2] <= '9')))) {
        char*  end;
        double v = strtod((char*)home(self)->code->body, &end);
        copy((byte*)&fval, &v, sizeof v);
        c = (byte*)end;
        kind = treal; break;
      }
      byte* d = c; // past the digits: a prefix follows when it moves
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
      if(c != d && ival <= 0xffffffff) ival = (u64i)(s64i)(s32i)ival; // 0xffffffff is -1, as 32 bits
      kind = tnum; break;
    case '\0': break;
    default:
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

// the token sift lexed starting at at, or NULL
static lexed* relex(mode* self, const byte* at) {
  u32i lo = 0, hi = self->lexn;
  while(lo < hi) {
    u32i m = lo + (hi - lo) / 2;
    if(self->lexes[m].from < at) lo = m + 1; else hi = m;
  }
  return lo < self->lexn && self->lexes[lo].from == at? self->lexes + lo: NULL;
}

// in a function body, replays sift's tokens, also after a lookahead rewinds
void chop(mode* self) {
  unit* u = home(self);
  if(self->lexn && self->unit == self->lexu) {
    lexed* t = self->lexi < self->lexn && self->lexes[self->lexi].from == u->code->body?
               self->lexes + self->lexi: relex(self, u->code->body);
    if(t) {
      u->code->body = t->to, u->line = t->line, u->talk = t->talk;
      self->lexi = (u32i)(t - self->lexes) + 1;
      return;
    }
  }
  lex(self);
}

// k: a string literal, compared by spelling, not looked up
#define same(self, k) (\
  home(self)->talk.kind == ttag &&\
  home(self)->talk.hash->size == sizeof(k) - 1 &&\
  alike(home(self)->talk.hash->name, (k), sizeof(k) - 1))

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

meta* mint(mode* self, meta x) {
  meta* m = cell(self, &self->defs, &self->defp, self->defn++);
  *m = x;
  return m;
}

static u64i  sire(const meta* x) {
  u64i h = (u64i)(uintptr_t)x->type ^ (u64i)x->kind << 56 ^ (u64i)x->size << 24;
  h = (h ^ (u64i)(u32i)x->slot << 32 ^ (u64i)x->longs << 8 ^ (u64i)x->reals << 16 ^ x->more) * 0x9e3779b97f4a7c15ull;
  return h ^ h >> 31;
}

// x's slot in made, or the free one for it
static meta** lode(const mode* self, const meta* x) {
  u64i  mask = self->madec - 1;
  for(u64i i = sire(x) & mask;; i = (i + 1) & mask) {
    meta* y = self->made[i];
    if(!y || (y->kind == x->kind && y->type == x->type && y->size == x->size && y->slot == x->slot
              && y->longs == x->longs && y->reals == x->reals && y->more == x->more)) return self->made + i;
  }
}

// derived types are interned: one meta per pointer, array, function, bit-field
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

meta* knot(mode* self, what kind, meta* type, u32i size) {
  return sole(self, (meta){.kind = kind, .type = type, .size = size});
}

meta* href(mode* self, meta* type) { return knot(self, refs, type, 8); }

// argc -1: unknown; longs, reals: bitmasks of 64-bit and real parameters
meta* mold(mode* self, meta* type, s32i argc, byte longs, byte reals, bool more) {
  return sole(self, (meta){.kind = proc, .type = type, .slot = argc, .longs = longs, .reals = reals, .more = more});
}

// the function type t points to, else NULL
meta* aimp(meta* t) { return t && t->kind == refs && t->type->kind == proc? t->type: NULL; }

meta* elem(const meta* t) { return t && (t->kind == refs || t->kind == many)? t->type: NULL; }
u32i  wide(const meta* t) { return elem(t) || ((t->kind == base || t->kind == bitf) && t->size == 8)? WIDE: 0; }
u32i  span(const meta* t) { return t->size; }
#define flot(self, t) ((t) == tget(self, real))
u32i  lean(meta* t) { return t->kind == many? lean(t->type): t->kind == list? (u32i)t->slot: t->size; }

// deferred operand: code is emitted only when the value must be in acc
typedef enum { vacc, vnum, vloc, vglo, vmem, vcmp, vreg, vflo, vfac } vkind;
// ival: value, frame slot, data offset or register (reals from FREG up);
// vmem: address in acc; vcmp: comparison op, result in the flags; vflo: real
// constant, bits in fval; vfac: real in facc. A real in acc (vacc) is its bits
typedef struct { vkind kind; union { s32i ival; u64i fval; }; meta* type; } oper;
#define cnst(self, n) ((oper){.kind = vnum, .ival = (n), .type = tget(self, int)})
#define acc(t)  ((oper){.kind = vacc, .type = (t)})
#define fac(self) ((oper){.kind = vfac, .type = tget(self, real)})

// an offset, not an address: data may move
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

// unescapes s into out when given; its size with the NUL
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

// the mask of bit-field f in its unit
u64i  ones(const meta* f) { return (f->longs < 64? ((u64i)1 << f->longs) - 1: ~(u64i)0) << f->slot; }

// acc, the unit of f, to its value: sign extended unless narrower than int
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

// a mask wider than a 32-bit constant is built in acc, via tmp
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

// acc into bit-field f of x, a local or memory with its address pushed:
// unit ^= (unit ^ acc << slot) & ones
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

double  sum(oper x) {
  double v = x.ival;
  if(x.kind == vflo) copy((byte*)&v, &x.fval, sizeof v);
  return v;
}

oper  flo(mode* self, double v) {
  oper x = {.kind = vflo, .type = tget(self, real)};
  copy((byte*)&x.fval, &v, sizeof v);
  return x;
}

// f: ACC, TMP or a local's real register
void  fput(mode* self, oper x, s32i f) {
  switch(x.kind) {
    case vfac: if(f != ACC) emit(self, fop, '=', f, ACC); return;
    case vreg: if(x.ival - FREG != f) emit(self, fop, '=', f, x.ival - FREG); return;
    case vnum: x = flo(self, x.ival); // fallthrough
    case vflo: emit(self, fimm, f, x.fval); return;
    case vloc: emit(self, fslot, 0, f, x.ival); return;
    case vglo: spot(self, x.ival); // fallthrough
    case vmem: emit(self, fat, 0, f); return;
    case vacc: emit(self, fbit, 1, f, ACC); return;
    default:   flaw(self, "not a value\n");
  }
}

oper  fload(mode* self, oper x) {
  fput(self, x, ACC);
  return fac(self);
}

// x's real register, else ftmp loaded with x
s32i  fsrc(mode* self, oper x) {
  if(x.kind == vreg) return x.ival - FREG;
  if(x.kind == vfac) return ACC;
  fput(self, x, TMP);
  return TMP;
}

oper  load(mode* self, oper x) {
  meta* t = x.type;
  if(x.kind == vcmp) { emit(self, flag, x.ival); return acc(tget(self, int)); }
  if(x.kind == vfac) { emit(self, fbit, 0, ACC, ACC); return acc(t); } // its bits
  if(x.kind == vreg && x.ival >= FREG) { emit(self, fbit, 0, x.ival - FREG, ACC); return acc(t); }
  if(x.kind == vflo) { // 0.0 is a move, another one is read from data
    if(!x.fval) { emit(self, move, 0); return acc(t); }
    u32i off = lay(self, 8, 0, 8);
    copy(self->data->head + off, &x.fval, 8);
    x = (oper){.kind = vglo, .ival = (s32i)off, .type = t};
  }
  if(x.kind == vreg) { emit(self, rop, '=' | wide(t), ACC, x.ival, 0); return acc(t); }
  if(t->kind == many) {
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

oper  refr(mode* self, oper x) {
  if(x.type->kind == bitf) flaw(self, "cannot take the address of a bit-field\n");
  if(x.kind == vloc) emit(self, addr, x.ival);
  if(x.kind == vglo) spot(self, x.ival);
  if(x.kind == vnum || x.kind == vacc || x.kind == vcmp || x.kind == vflo || x.kind == vfac) flaw(self, "cannot take the address\n");
  if(x.kind == vreg) flaw(self, "cannot take the address of a local in a register\n"); // sift keeps such a local in the frame
  return acc(href(self, x.type));
}

oper  star(mode* self, oper x) {
  meta* t = elem(x.type);
  if(!t) flaw(self, "not a pointer\n");
  load(self, x);
  return (oper){.kind = vmem, .type = t};
}

meta* mate(mode* self, meta* type) {
  meta* m = type && type->kind == list? type->type: NULL;
  if(!m || home(self)->talk.kind != ttag) flaw(self, "expected a member\n");
  for(; m && !akin(m->name, home(self)->talk.hash); m = m->hide); // m->name may be another unit's
  if(!m) flaw(self, "no member '%.*s'\n", nick(home(self)->talk.hash));
  chop(self);
  return m;
}

oper  memb(mode* self, oper x) {
  meta* m = mate(self, x.type);
  if(x.kind != vmem) x.ival += m->slot;
  else if(m->slot) emit(self, calc, '+' | WIDE, 1, m->slot);
  x.type = m->type;
  return x;
}

// calls in the dropped code are still checked, not patched
void  trim(mode* self, byte* mark) {
  dwarf_trim(self, mark);
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

// the ops emit.rop takes: not shifts, nor * / %
#define plain(op) (rank(op) != 8 && rank(op) != 10)
#define fits(self, t) ((t)->kind == refs || ((t)->kind == base && (t)->size && !flot(self, t)))

// a forward call, bound later; ext: the C way
void  post(mode* self, item* name, byte* at, u32i argc, bool ext) {
  self->link = swell(self, self->link, self->fixs, &self->fixc, sizeof(fix), 64);
  self->link[self->fixs++] = (fix){name, at, argc, home(self)->line, self->unit, ext};
}

// a call the C way (ext) was checked against its prototype by cabi
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

s32i  room(mode* self, u32i size);

// C code on x86-64 but Windows clobbers the registers of real locals: before
// the call those in scope go to fresh slots (returned, 0 if none), after it
// they are loaded back from at
s32i  ferry(mode* self, s32i at) {
  u32i  m = self->busy >> FREG, n = 0;
  if(!m || self->arch.cpu != cpu_ia64 || self->form.os == os_win) return 0;
  for(u32i b = m; b; b &= b - 1) n++;
  s32i  s = at? at: room(self, 8 * n);
  for(u32i r = 0; m >> r; r++) if(m >> r & 1) { emit(self, fslot, !at, (s32i)r, s); s += 8; }
  return at? at: s - 8 * (s32i)n;
}

// a function in an archive member not pulled yet is asked for, the call bound
// once it is in. C leaves result bytes past its size undefined: cut them.
// real: bitmask of the real arguments
oper  cabi(mode* self, item* name, meta* func, u32i argc, u32i real) {
  if(func->size != VARY && (argc < func->size || (argc > func->size && !func->more)))
    flaw(self, "'%.*s' takes %d arguments, got %d\n", nick(name), func->size, argc);
  meta* body = func->lib || func->addr? func: find(self, 0, name); // past a prototype
  if(body && body->kind == pend) ask(self, body);
  bool  wait = !func->lib && (!body || !body->addr);
  s32i  kept = ferry(self, 0);
  if(func->lib) spot(self, port(self, func));
  byte* at = emit(self, ffi, func->lib? NULL: wait? self->text->head: body->addr,
                  argc, func->more? func->size: argc, real | flot(self, func->type) << RETS, self->form.os);
  if(kept) ferry(self, kept);
  if(wait) post(self, name, at, argc, 1);
  u32i n = span(func->type);
  if(n && n < 4) emit(self, calc, '&', 1, (1 << n * 8) - 1);
  if(n == 4) emit(self, cast, 0);
  return acc(func->type);
}

// a constant not below 0 needs none: its move zero extends
void  sext(mode* self, oper x) {
  if(x.kind != vnum || x.ival < 0) emit(self, cast, 1);
}

// an integer or a real
#define digit(t) ((t)->kind == base || (t)->kind == bitf)

// between integer and real only: a real is truncated to 64 bits, then cut to
// 32 unless t is wide. A constant stays one when it can
oper  bend(mode* self, oper x, meta* t) {
  meta* u = x.type->kind == bitf? x.type->type: x.type;
  bool  to = flot(self, t), from = flot(self, u);
  if(to == from) return x;
  if(!digit(to? u: t)) flaw(self, "a real is not a pointer, an array or a struct\n");
  if(x.kind == vnum) return flo(self, x.ival);
  if(x.kind == vflo && sum(x) > -2147483649.0 && sum(x) < 2147483648.0) return cnst(self, (s32i)sum(x));
  if(to) {
    x = load(self, x);
    emit(self, turn, 1, wide(x.type) != 0);
    return fac(self);
  }
  fload(self, x);
  emit(self, turn, 0, 1);
  if(!wide(t)) emit(self, cast, 0);
  return acc(t->kind == bitf? t->type: t);
}

// real x op 0.0, in the flags
oper  nought(mode* self, oper x, u32i op) {
  s32i  a = ACC;
  if(x.kind == vreg) a = x.ival - FREG;
  else fload(self, x);
  emit(self, fimm, TMP, 0);
  emit(self, fop, op | WIDE | REAL | TEST, a, TMP);
  return (oper){.kind = vcmp, .ival = (s32i)(op | WIDE | REAL), .type = tget(self, int)};
}

// a real is compared with 0, so -0.0 is false
oper  truth(mode* self, oper x) {
  if(!flot(self, x.type)) return x;
  if(x.kind == vflo) return cnst(self, sum(x) != 0);
  return nought(self, x, tokd('!', '='));
}

// argument i converted to its parameter's type when either is a real; bit i
// of real set when it ends up one
oper  suit(mode* self, const meta* f, u32i i, oper x, u32i* real) {
  bool  known = f && i < 6 && (f->kind == proc? f->slot >= 0 && i < (u32i)f->slot: f->size != VARY && i < f->size);
  if(known && f->reals >> i & 1) x = bend(self, x, tget(self, real));
  else if(known && flot(self, x.type)) x = bend(self, x, f->longs >> i & 1? tget(self, long): tget(self, int));
  if(flot(self, x.type)) *real |= 1u << i;
  return x;
}

// overloads: told apart by the count and kinds (int, long, real) of their
// parameters; C code of an object has none. elect picks one per call, dub
// names each for the linker
#define KIN   32   // overloads of a name at most
#define SPELL 1024 // bytes of a symbol or an overload message
#define ours(m) ((m) && (m)->kind == call && !((m)->ext && !(m)->lib))

static bool  peer(const meta* f, const meta* g) {
  return f->size == g->size && f->longs == g->longs && f->reals == g->reals;
}

// 0 int, 1 long, 2 real
static u32i  sort3(const mode* self, const meta* t) { return flot(self, t)? 2: wide(t)? 1: 0; }

// the overloads unit at sees, but skip: its own, latest first, then the public
// ones of other units in join order; of the same parameters, the first
static u32i  kin(mode* self, u32i at, const item* name, meta** out, const meta* skip) {
  u32i  n = 0;
  item* first = hsee(&self->names, name->name, name->size, clue(name->name, name->size));
  for(u32i pass = 0; pass < 2; pass++)
    for(const item* s = first; s; s = s->next) {
      if(!s->unit || (u32i)(s->unit == at) == pass) continue;
      for(meta* m = s->data; m; m = m->hide) {
        if(m == skip || !ours(m) || (pass && !heft(m))) continue;
        u32i i = 0;
        while(i < n && !peer(out[i], m)) i++;
        if(i < n) continue;
        if(n == KIN) flaw(self, "more than %d overloads of '%.*s'\n", KIN, nick(name));
        out[n++] = m;
      }
    }
  return n;
}

static u32i  utter(const mode* self, char* b, u32i n, const char* s, u64i size) {
  if(n + size >= SPELL) flaw(self, "a name or message too long for an overload\n");
  copy((byte*)b + n, s, size);
  b[n + size] = 0;
  return n + (u32i)size;
}

// a pointer or array as what it points to and star, a function type with fn
static u32i  scribe(const mode* self, const meta* t, char* b, u32i n, char star) {
  if(t->kind == refs || t->kind == many) return utter(self, b, scribe(self, t->type, b, n, star), &star, 1);
  if(t->kind == bitf) return scribe(self, t->type, b, n, star);
  if(t->kind == proc) return utter(self, b, scribe(self, t->type, b, n, star), "fn", 2);
  return t->name? utter(self, b, n, t->name->name, t->name->size): utter(self, b, n, "struct", 6);
}

// name(int, real), after ", " unless n is 0
static u32i  style(const mode* self, const meta* f, const item* name, char* b, u32i n) {
  if(n) n = utter(self, b, n, ", ", 2);
  n = utter(self, b, utter(self, b, n, name->name, name->size), "(", 1);
  for(u32i i = 0; i < f->size; i++) n = scribe(self, f->args[i], b, i? utter(self, b, n, ", ", 2): n, '*');
  if(f->more) n = utter(self, b, n, f->size? ", ...": "...", f->size? 5: 3);
  return utter(self, b, n, ")", 1);
}

// f's symbol: name_type..., * as p (name_void without parameters); a public
// one claims it in the root's table. was: an import f declares again
void  dub(mode* self, meta* f, const meta* was) {
  if(!f->name->unit) return;
  char  b[SPELL];
  u32i  n = utter(self, b, 0, f->name->name, f->name->size);
  if(!f->size) n = utter(self, b, n, "_void", 5);
  for(u32i i = 0; i < f->size; i++) n = scribe(self, f->args[i], b, utter(self, b, n, "_", 1), 'p');
  item* s = hget(self, 0, b, n);
  if(!f->own) {
    const meta* m = find(self, 0, s);
    if(m && m != was && (m->kind == call || m->kind == glob)) flaw(self, "'%.*s' is already defined\n", nick(s));
    s->data = f;
  }
  f->name = s;
}

// all but func, whose body may follow, are named now (dub). A forward call by
// the bare name would now be ambiguous
void  crowd(mode* self, meta* func, meta** c, u32i n, meta* was) {
  for(u32i i = 0; i <= n; i++) {
    const meta* m = i < n? c[i]: was;
    if(m && m->fore) flaw(self, "'%.*s' is called before its definition, it cannot be overloaded\n", nick(func->name));
  }
  func->lots = 1;
  for(u32i i = 0; i < n; i++) { c[i]->lots = 1; dub(self, c[i], NULL); }
  if(was) { was->lots = 1; dub(self, was, NULL); }
}

// the arguments are compiled once for their types (mode.dry), then dropped and
// their tokens put back. Per argument the same kind fits best, then an int for
// a long, then the rest (never a real for a pointer or back). The winner is
// the one no other beats on any argument
meta* elect(mode* self, item* name) {
  meta* c[KIN], *t[6];
  u32i  n = kin(self, self->unit, name, c, NULL), argc = 0, dials = self->dials;
  u32i  size = (u32i)(self->data->body - self->data->head);
  s32i  slot = self->slot, lits = self->lits;
  byte* at = home(self)->code->body, *mark = self->text->body;
  u64i  line = home(self)->line;
  word  talk = home(self)->talk;
  if(n == 1) return c[0];
  self->dry++;
  for(; !twin(self, ')'); argc++) {
    if(argc) skip(self, ',');
    if(argc == 6) flaw(self, "more than 6 arguments\n");
    t[argc] = expr(self, 1).type;
  }
  self->dry--;
  trim(self, mark);
  file* d = self->data;
  for(u32i i = size; d->head + i < d->body; i++) d->head[i] = 0;
  d->body = d->head + size;
  self->dials = dials, self->slot = slot, self->lits = lits;
  home(self)->code->body = at, home(self)->line = line, home(self)->talk = talk;

  byte  cost[KIN][6];
  bool  fit[KIN], top[KIN];
  u32i  fits = 0, tops = 0, best = 0;
  for(u32i j = 0; j < n; j++) {
    const meta* f = c[j];
    fit[j] = argc == f->size || (f->more && argc > f->size);
    for(u32i i = 0; fit[j] && i < argc; i++) {
      const meta* p = i < f->size? f->args[i]: t[i];
      u32i  a = sort3(self, t[i]), k = sort3(self, p);
      cost[j][i] = a == k? 0: !a && k == 1 && digit(p)? 1: 2;
      if(a != k && (a == 2 || k == 2) && !(digit(t[i]) && digit(p))) fit[j] = 0;
    }
    fits += fit[j];
  }
  for(u32i j = 0; j < n; j++) {
    top[j] = fit[j];
    for(u32i k = 0; top[j] && k < n; k++)
      for(u32i i = 0; fit[k] && k != j && i < argc; i++) if(cost[j][i] > cost[k][i]) top[j] = 0;
    if(top[j]) tops++, best = j;
  }
  if(tops == 1) return c[best];
  char  b[SPELL];
  u32i  m = 0;
  if(!fits) {
    m = utter(self, b, m, "(", 1);
    for(u32i i = 0; i < argc; i++) m = scribe(self, t[i], b, i? utter(self, b, m, ", ", 2): m, '*');
    m = utter(self, b, m, ")", 1);
    char  w[SPELL];
    u32i  k = 0;
    for(u32i j = 0; j < n; j++) k = style(self, c[j], name, w, k);
    flaw(self, "no overload of '%.*s' takes %.*s: %.*s\n", nick(name), (int)m, b, (int)k, w);
  }
  for(u32i j = 0; j < n; j++) if(tops? top[j]: fit[j]) m = style(self, c[j], name, b, m);
  flaw(self, "the call of '%.*s' fits more than one overload: %.*s\n", nick(name), (int)m, b);
}

// each argument is pushed on 64 bits, then popped into its register, the last
// one from acc. A constant or a register local waits (late) until the others
// are computed. C code and syscall pop them all
oper  dial(mode* self, item* name) {
  bool  trap = name == sget(self, "syscall");
  meta* func = find(self, self->unit, name);
  if(func && plural(func, name)) func = elect(self, name);
  bool  ffi = func && (func->kind == pend || (func->kind == call && func->ext));
  u32i  argc = 0, late = 0, real = 0;
  s32i  last = -1; // the argument in acc
  byte *at = NULL, *past = NULL; // around its push
  oper  x[7];
  self->dials++;
  if(self->leaf) self->spoil = 1; // sift saw no call: the spares may hold locals
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
    return acc(tget(self, int));
  }
  if(ffi) return cabi(self, name, func, argc, real);
  if(last >= 0) emit(self, pass, last);
  for(s32i i = argc; i--; ) {
    if(late >> i & 1 && x[i].kind == vreg && x[i].ival >= FREG) emit(self, fbit, 0, x[i].ival - FREG, ARG - i); // a real's bits
    else if(late >> i & 1)
      emit(self, rop, '=' | (x[i].kind == vnum? WIDE: wide(x[i].type)), ARG - i, x[i].kind == vnum? NUM: x[i].ival, x[i].ival);
    else if(i != last) emit(self, args, i);
  }
  at = self->text->body;
  emit(self, dial);
  if(func) bind(self, at, name, func, argc, 0);
  else post(self, name, at, argc, 0);
  return acc(func? func->type: tget(self, int));
}

// its entry for C code (hail) when it has one
byte* deed(mode* self, item* name, const meta* func) {
  if(plural(func, name)) flaw(self, "'%.*s' is overloaded: name one by its symbol to take its address\n", nick(name));
  const meta* body = func->addr? func: find(self, 0, name); // past a prototype
  if(!body || body->kind != call || !body->addr)
    flaw(self, "'%.*s' is not defined yet: define or link it before taking its address\n", nick(name));
  return body->hail? body->hail: body->addr;
}

// a function pointer is called the C way (ring)
oper  hand(mode* self, item* name, meta* func) {
  if(plural(func, name)) flaw(self, "'%.*s' is overloaded: name one by its symbol to take its address\n", nick(name));
  meta* t = href(self, mold(self, func->type, func->size == VARY? -1: (s32i)func->size, func->longs, func->reals, func->more));
  if(func->lib) { spot(self, port(self, func)); emit(self, read, 8); return acc(t); }
  byte* at = self->text->body;
  byte* to = deed(self, name, func);
  emit(self, glob);
  emit(self, site, at, to - at);
  return acc(t);
}

// called the C way, which an otic function follows too (or its C entry). x
// waits in a frame slot while the arguments are pushed
oper  ring(mode* self, oper x) {
  meta* f = x.type && x.type->kind == proc && x.kind == vmem? x.type: aimp(x.type);
  if(!f) flaw(self, "not a function\n");
  if(f != x.type) load(self, x); // else its address is in acc
  s32i  slot = room(self, 8);
  u32i  argc = 0, real = 0;
  emit(self, stor, slot, 8);
  self->dials++;
  if(self->leaf) self->spoil = 1;
  for(; !twin(self, ')'); argc++) {
    if(argc) { skip(self, ','); emit(self, push); }
    if(argc == 6) flaw(self, "more than 6 arguments\n");
    oper  y = suit(self, f, argc, expr(self, 1), &real);
    if(!wide(load(self, y).type)) sext(self, y);
  }
  if(f->slot >= 0 && (argc < (u32i)f->slot || (argc > (u32i)f->slot && !f->more)))
    flaw(self, "the function takes %d arguments, got %d\n", f->slot, argc);
  if(argc) emit(self, push);
  s32i  kept = ferry(self, 0);
  emit(self, addr, slot);
  emit(self, ffi, NULL, argc, f->more? (u32i)f->slot: argc, real | flot(self, f->type) << RETS, self->form.os);
  if(kept) ferry(self, kept);
  u32i n = span(f->type);
  if(n && n < 4) emit(self, calc, '&', 1, (1 << n * 8) - 1);
  if(n == 4) emit(self, cast, 0);
  return acc(f->type);
}

// at, past: around the keep of the left operand; dials: calls before it
typedef struct { byte* at; byte* past; u32i dials; } kept;

kept  keep(mode* self, oper* x) {
  if(x->kind == vmem && flot(self, x->type)) *x = fload(self, *x);
  if(x->kind == vmem || x->kind == vcmp) *x = load(self, *x);
  kept  mark = {self->text->body, NULL, self->dials};
  if(x->kind == vacc) { emit(self, keep); self->held++; }
  if(x->kind == vfac) { emit(self, fkeep); self->held++; }
  mark.past = self->text->body;
  return mark;
}

oper  pair(mode* self, u32i op, oper x, oper y, kept mark);
oper  lit(mode* self);
meta* sort(mode* self);
meta* coil(mode* self, meta* type, item** name);
meta* dims(mode* self, meta* type);
oper  atom(mode* self);

meta* ilk(mode* self) {
  meta* x = home(self)->talk.kind == ttag? find(self, self->unit, home(self)->talk.hash): NULL;
  return x && (x->kind == base || x->kind == list)? x: NULL;
}

#define type(self) (same(self, "struct") || ilk(self))

// sizeof's operand: the code of an expression is dropped
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

void  fini(mode* self);

// # name(args): compiled as a parameterless function at the end of text, run
// by mode.eval in an image of all compiled so far (globals keep what earlier
// #s did), then dropped with what it laid in data, bss and the import slots.
// Locals are out of scope in the arguments. A mode for another target takes
// the results in order from the host mode's tape (mirror)
yield sharp(mode* self) {
  if(home(self)->talk.kind != ttag) flaw(self, "expected the name of a function after #\n");
  item* name = home(self)->talk.hash;
  meta* func = find(self, self->unit, name);
  if(!func || func->kind != call) flaw(self, "no function '%.*s'\n", nick(name));
  chop(self);
  if(!twin(self, '(')) flaw(self, "expected ( after '%.*s': only a call runs at compile time\n", nick(name));
  if(plural(func, name) && !self->dry) func = elect(self, name);
  if(self->frame != ~0u && func == self->func)
    flaw(self, "'%.*s' is being compiled, define it before running it\n", nick(name));
  meta* t = func->type;
  bool  text = t->kind == refs && t->type == tget(self, char), real = flot(self, t);
  if(!text && t->kind != base) flaw(self, "'%.*s' gives a result that cannot be used at compile time\n", nick(name));
  if(self->dry) { // typing an overloaded call's arguments: not run
    for(u32i d = 1; d; chop(self)) {
      u32i k = home(self)->talk.kind;
      if(!k) flaw(self, "expected )\n");
      d += (k == '(') - (k == ')');
    }
    return (yield){.type = t, .text = text? (char*)"": NULL};
  }

  struct { s32i slot, deep, lits; u32i busy, regn, fregn, lend, held, dials; bool leaf, bare; } was =
    {self->slot, self->deep, self->lits, self->busy, self->regn, self->fregn, self->lend, self->held, self->dials, self->leaf, self->bare};
  byte* mark = self->text->body;
  u32i  fixs = self->fixs, imps = self->impn, size = (u32i)(self->data->body - self->data->head);
  u64i  bss  = self->zero;
  bool  hide = self->frame != ~0u && !self->hidden;
  if(hide) { // a local has no meaning in a function of its own
    for(u32i i = self->top; i-- > self->frame; ) { meta* x = pin(self->pile, i); x->name->data = x->hide; }
    self->hidden = 1;
  }
  self->slot = self->deep = self->lits = 0;
  self->busy = self->regn = self->fregn = self->lend = self->held = 0;
  self->leaf = self->bare = 0;
  emit(self, hail, self->form.os, 0, (u32i)real << RETS);
  byte* addr = self->text->body;
  emit(self, open);
  dial(self, name);
  fini(self);
  emit(self, size, addr, (self->deep + 15) & -16);
  for(u32i i = fixs; i < self->fixs; i++)
    if(self->link[i].at >= mark && self->link[i].at)
      flaw(self, "'%.*s' is not defined yet, define it before the call\n", nick(self->link[i].name));

  yield r = {.type = t};
  if(self->eval) {
    r.bits = self->eval(self, mark, real);
    if(text) {
      const char* p = (const char*)(uintptr_t)r.bits;
      r.size = p? (u32i)slen(p): 0;
      r.text = self->heap(NULL, r.size + 1);
      copy((byte*)r.text, p? p: "", r.size + 1);
    }
    self->tape = swell(self, self->tape, self->tapen, &self->tapec, sizeof(yield), 16);
    self->tape[self->tapen++] = r;
  }
  else {
    if(!self->mirror) flaw(self, "cannot run code at compile time here\n");
    if(!self->shaded) {
      mode* h = self->mirror(self);
      self->tape = h->tape, self->tapen = h->tapen, self->tapec = h->tapec;
      self->shaded = 1;
    }
    if(self->taken == self->tapen || !self->tape[self->taken].text != !text)
      flaw(self, "the host build did not run this #\n");
    r = self->tape[self->taken++];
    r.type = t;
  }

  trim(self, mark);
  file* d = self->data;
  for(u32i i = size; i < (u32i)(d->body - d->head); i++) d->head[i] = 0;
  d->body = d->head + size;
  self->zero = bss;
  for(u32i i = imps; i < self->impn; i++) self->imps[i]->slot = 0;
  self->impn = imps;
  self->slot = was.slot, self->deep = was.deep, self->lits = was.lits;
  self->busy = was.busy, self->regn = was.regn, self->fregn = was.fregn, self->lend = was.lend, self->held = was.held, self->dials = was.dials;
  self->leaf = was.leaf, self->bare = was.bare;
  if(hide) {
    for(u32i i = self->frame; i < self->top; i++) { meta* x = pin(self->pile, i); x->name->data = x; }
    self->hidden = 0;
  }
  return r;
}

// v, an int constant when it fits, else a long read from data. Its slot is
// mode.big, so an initializer or a unary operator still takes it as a constant
oper  huge(mode* self, s64i v) {
  if(v == (s32i)v) return cnst(self, (s32i)v);
  u32i  off = lay(self, 8, 0, 8);
  copy(self->data->head + off, &v, 8);
  self->big = off + 1;
  return (oper){.kind = vglo, .ival = (s32i)off, .type = tget(self, long)};
}

// 1 and its value in v when x is the last long constant huge read from data
bool  great(const mode* self, oper x, s64i* v) {
  if(x.kind != vglo || (u32i)x.ival + 1 != self->big) return 0;
  copy((byte*)v, self->data->head + x.ival, 8);
  return 1;
}

oper  atom(mode* self) {
  u32i op = home(self)->talk.kind;
  oper x;
  switch(op) {
    case tnum: x = huge(self, (s64i)home(self)->talk.ival); chop(self); return x;
    case treal: x = (oper){.kind = vflo, .fval = home(self)->talk.fval, .type = tget(self, real)}; chop(self); return x;
    case '"': {
      u32i off = lay(self, (u32i)(home(self)->talk.hash->size - 1), 0, 8);
      quote(home(self)->talk.hash, self->data->head + off);
      chop(self);
      spot(self, off);
      x = acc(href(self, tget(self, char)));
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
      if(var && var->kind == pend) { x = (oper){.kind = vglo, .ival = WANT | ask(self, var), .type = tget(self, int)}; break; }
      if(!var || (var->kind != data && var->kind != glob))
        flaw(self, "no variable '%.*s'\n", nick(name));
      x = (oper){.kind = var->kind != data? vglo: var->slot < 0? vloc: vreg, .ival = var->slot, .type = var->type};
      if(var->ext) { spot(self, port(self, var)); emit(self, read, 8); x = (oper){.kind = vmem, .type = var->type}; }
      break;
    }
    case '#': {
      chop(self);
      yield y = sharp(self);
      if(y.text) flaw(self, "source text from # goes where a declaration does, not in an expression\n");
      if(flot(self, y.type)) { double v; copy((byte*)&v, &y.bits, sizeof v); return flo(self, v); }
      if(self->dry && span(y.type) == 8) return (oper){.kind = vnum, .type = tget(self, long)};
      s64i  v = (s64i)y.bits;
      switch(span(y.type)) { case 0: v = 0; break; case 1: v = (byte)v; break; case 2: v = (u16i)v; break; case 4: v = (s32i)v; }
      return huge(self, v);
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
        if(op == '!') return nought(self, x, tokd('=', '='));
        s32i  a = TMP;
        if(x.kind == vreg) a = x.ival - FREG;
        else { fload(self, x); emit(self, fop, '=', TMP, ACC); }
        emit(self, fimm, ACC, flo(self, -0.0).fval);
        emit(self, fop, '-', ACC, a);
        return fac(self);
      }
      if(x.kind == vnum)
        return cnst(self, op == '-'? (s32i)(0 - (u32i)x.ival): op == '!'? !x.ival: ~x.ival);
      s64i  v;
      if(great(self, x, &v)) // into a slot of its own: code may read the old one
        return op == '!'? cnst(self, !v): huge(self, op == '-'? (s64i)(0 - (u64i)v): ~v);
      x = load(self, x);
      u32i w = wide(x.type);
      if(op == '-') { emit(self, swap); emit(self, move, 0); emit(self, calc, '-' | w, 0, 0); }
      else emit(self, calc, (op == '!'? tokd('=', '='): '^') | w, 1, op == '!'? 0: -1);
      return acc(op == '!' || !w? tget(self, int): x.type);
    }
    default:
      flaw(self, "expected expression, got %x\n", op);
  }
  for(;;) {
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
    return acc(tget(self, int));
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
  return acc(tget(self, int));
}

void  grow(mode* self, oper x, s32i scale, u32i w) {
  if(!w) return;
  if(!wide(x.type)) sext(self, x);
  if(scale != 1) emit(self, calc, '*' | WIDE, 1, scale);
}

// a scale that step takes
#define steps(n) ((n) == 1 || (n) == 2 || (n) == 4 || (n) == 8)

// y in acc to tmp, x back in acc from where keep put it: scratch k when no
// call came between, else the register sift lent, else the stack
oper  rest(mode* self, oper x, u32i k, kept mark) {
  bool  calm = self->dials == mark.dials;
  if(x.kind == vacc && ((calm && k < HELD) || self->lend + k < self->regn)) {
    u32i s = calm && k < HELD? k: HELD + self->lend + k;
    emit(self, lift, mark.at, s);
    emit(self, take, s);
  }
  else if(x.kind == vacc) emit(self, pull);
  else { emit(self, swap); x = load(self, x); }
  return x;
}

// rest for a real x kept in facc
void  frest(mode* self, u32i k, kept mark) {
  bool  calm = self->dials == mark.dials;
  if((calm && k < HELD) || self->lend + k < self->regn) {
    u32i s = calm && k < HELD? k: HELD + self->lend + k;
    emit(self, flift, mark.at, s);
    emit(self, ftake, s);
  }
  else emit(self, fpull);
}

void  ftop(mode* self, oper x) {
  if(flot(self, x.type)) { fput(self, x, ACC); return; }
  x = load(self, x);
  emit(self, turn, 1, wide(x.type) != 0);
}

u32i  flip(u32i op) {
  u32i o = op & 0xffffff;
  o = o == '<'? '>': o == '>'? '<': o == tokd('<', '=')? tokd('>', '='): o == tokd('>', '=')? tokd('<', '='): o;
  return (op & ~0xffffffu) | o;
}

// pair with a real operand, the result in facc; constants folded by the host.
// + * and comparisons commute when x is not kept, y staying in facc
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
  u32i  k = x.kind == vacc || x.kind == vfac? --self->held: 0; // the scratch of x, when kept
  u32i  c = op | WIDE | REAL | (test? TEST: 0);
  if(y.kind == vnum) y = flo(self, y.ival);
  if(x.kind == vnum) x = flo(self, x.ival);
  bool  real = flot(self, y.type);
  if(self->text->body == mark.past && (y.kind == vflo || (real && (y.kind == vloc || y.kind == vreg || y.kind == vglo)))) {
    self->text->body = mark.at; // y emitted nothing: drop the keep
    ftop(self, x);
    emit(self, fop, c, ACC, fsrc(self, y));
  }
  else {
    if(real) fload(self, y);
    else ftop(self, y);
    bool  either = op == '+' || op == '*' || test; // the same either way round
    if(x.kind == vfac) { frest(self, k, mark); emit(self, fop, c, ACC, TMP); }
    else if(x.kind == vacc) { emit(self, fop, '=', TMP, ACC); rest(self, x, k, mark); ftop(self, x); emit(self, fop, c, ACC, TMP); }
    else if(either && flot(self, x.type)) emit(self, fop, c = flip(c), ACC, fsrc(self, x));
    else { emit(self, fop, '=', TMP, ACC); ftop(self, x); emit(self, fop, c, ACC, TMP); }
  }
  if(test) return (oper){.kind = vcmp, .ival = (s32i)(c & ~TEST), .type = tget(self, int)};
  return fac(self);
}

// a comparison is left in the flags. void* arithmetic is by bytes
oper  pair(mode* self, u32i op, oper x, oper y, kept mark) {
  if(flot(self, x.type) || flot(self, y.type)) return rpair(self, op, x, y, mark);
  meta *ex = elem(x.type), *ey = elem(y.type), *t = tget(self, int);
  if((op == '/' || op == '%' || (op == '-' && ex && ey)) && self->leaf && self->busy >> self->arch.regs & self->arch.sdiv)
    self->spoil = 1; // division takes a spare that holds a local
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
  else if(w && !test) t = wide(x.type)? x.type: y.type;
  if(t->kind == bitf) t = t->type;

  bool trap = (op == '/' || op == '%') && y.kind == vnum && !y.ival; // left to run time
  if(x.kind == vnum && y.kind == vnum && !trap)
    return cnst(self, fold(op, x.ival, y.ival));
  if(x.kind == vnum) { x.ival *= sx; sx = 1; }
  if(y.kind == vnum) { y.ival *= sy; sy = 1; }
  u32i  k = x.kind == vacc? --self->held: 0; // the scratch of x, when kept
  u32i  c = op | w | (test? TEST: 0);

  bool  slot = y.kind == vloc && (y.type->kind == base || y.type->kind == refs); // not a compound literal
  if(op == '+' && ex && !ey && y.kind != vnum && steps(sy)) { // pointer + n: n extended and scaled as it is added
    if((slot || y.kind == vreg) && self->text->body == mark.past) {
      self->text->body = mark.at; // y emitted nothing: drop the keep
      load(self, x);
      if(slot) emit(self, peek, y.ival, span(y.type));
    }
    else {
      y = load(self, y);
      rest(self, x, k, mark);
      slot = 1; // n is in tmp
    }
    emit(self, step, slot? TMP: y.ival, sy, wide(y.type) != 0);
    return acc(t);
  }
  if(y.kind == vnum || ((slot || y.kind == vreg) && (!w || wide(y.type)) && sy == 1 && self->text->body == mark.past)) {
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
      rest(self, x, k, mark);
      grow(self, x, sx, w);
      emit(self, calc, c, 0, 0);
    }
  }
  if(test) return (oper){.kind = vcmp, .ival = op | w, .type = tget(self, int)};
  if(ex && ey && op == '-' && ex->size > 1) emit(self, calc, '/', 1, ex->size);
  return acc(t);
}

// '=', op for op=, else 0
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

oper  fgive(mode* self, u32i op, oper x) {
  if(x.kind == vreg) {
    oper  y = expr(self, 1);
    if(op != '=' && op != '+' && op != '-' && op != '*' && op != '/') flaw(self, "invalid operation on reals\n");
    y = bend(self, y, x.type);
    if(op == '=') fput(self, y, x.ival - FREG);
    else emit(self, fop, op, x.ival - FREG, fsrc(self, y));
    return x;
  }
  if(x.kind == vglo) { spot(self, x.ival); x.kind = vmem; }
  byte* at = self->text->body;
  if(x.kind == vmem) emit(self, push); // the address, op= reads through it
  kept  mark = op == '='? (kept){0}: keep(self, &x);
  byte* past = self->text->body;
  oper  y = expr(self, 1);
  if(op != '=') y = pair(self, op, x, y, mark);
  y = bend(self, y, x.type);
  if(x.kind == vloc) {
    fload(self, y);
    emit(self, fslot, 1, ACC, x.ival);
    return fac(self);
  }
  if(op == '=' && self->text->body == past && (y.kind == vloc || y.kind == vreg)) {
    self->text->body = at; // y emitted nothing: the address stays in acc
    s32i f = y.kind == vreg? y.ival - FREG: ACC;
    if(f == ACC) fload(self, y);
    emit(self, fat, 1, f);
    return f == ACC? fac(self): y;
  }
  fload(self, y);
  emit(self, pull);
  emit(self, fat, 1, ACC);
  return fac(self);
}

oper  give(mode* self, u32i op, oper x) {
  meta* t = x.type, *f = t->kind == bitf? t: NULL;
  if(f) t = f->type;
  if(x.kind == vnum || x.kind == vacc || x.kind == vcmp || x.kind == vflo || x.kind == vfac
     || t->kind == many || t->kind == list || t->kind == proc)
    flaw(self, "cannot assign\n");
  if(flot(self, t)) return fgive(self, op, x);
  if(x.kind == vreg) { // in the register itself when it takes op; the value stays there
    kept  mark = keep(self, &x); // which keeps nothing
    oper  y = expr(self, 1);
    meta* e = elem(t);
    u32i  w = wide(t);
    if(op != '=' && (!plain(op) || elem(y.type) || flot(self, y.type) || (e && (rank(op) != 9 || e->kind == proc)))) {
      y = pair(self, op, x, y, mark);
      op = '=';
    }
    y = bend(self, y, t); // an integer register holds no real
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

// a branch taken when false, patched by back
void  gate(mode* self) {
  skip(self, '(');
  oper  x = truth(self, expr(self, 1));
  skip(self, ')');
  if(x.kind == vreg) { // compared with 0 where it is
    emit(self, rop, tokd('!', '=') | TEST | wide(x.type), x.ival, NUM, 0);
    x = (oper){.kind = vcmp, .ival = tokd('!', '='), .type = tget(self, int)};
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

#define crux(self, k, ...) {\
  seed((self), .kind = base, .name = sget((self), #k), __VA_ARGS__) \
  (self)->b##k = pin((self)->pile, (self)->top - 1); }

// names declared since top get back what they shadowed
void  bury(mode* self, u32i top) {
  while(self->top > top) {
    meta* x = pin(self->pile, --self->top);
    x->name->data = x->hide;
    if(x->kind == data && x->slot >= 0) self->busy &= ~(1u << x->slot);
  }
}

// a function sift left without a frame spoils
s32i  room(mode* self, u32i size) {
  if(self->bare) self->spoil = 1;
  self->slot += (size + 7) & -8;
  if(self->deep < self->slot) self->deep = self->slot;
  return -self->slot;
}

// the register sift gave name if t fits it and it is free, else frame slots
s32i  stay(mode* self, const item* name, const meta* t, u32i size) {
  u32i  r = name->pick - 1;
  if(!name->pick || !(r >= FREG? flot(self, t): fits(self, t)) || self->busy >> r & 1) return room(self, size);
  self->busy |= 1u << r;
  return (s32i)r;
}

meta* bulk(mode* self, item* name);

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

// parameter names may be left out; they are not declared
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

// *s name, or (*name[n]...)(parameters); *name NULL when there is none
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

meta* term(mode* self, meta* type, item** name) {
  type = coil(self, type, name);
  if(!*name) flaw(self, "expected name, got %x\n", home(self)->talk.kind);
  return type;
}

meta* dims(mode* self, meta* type) {
  if(!twin(self, '[')) return type;
  oper n = expr(self, 1);
  skip(self, ']');
  if(n.kind != vnum || n.ival <= 0)
    flaw(self, "array size must be a positive constant\n");
  type = dims(self, type);
  return knot(self, many, type, n.ival * type->size);
}

meta* full(mode* self, meta* type, const item* name) {
  type = dims(self, type);
  if(!type->size) flaw(self, "'%.*s' has no size\n", nick(name));
  return type;
}

meta* noun(mode* self, meta* base, item** name) {
  meta* type = term(self, base, name);
  return full(self, type, *name);
}

// zeros when src is NULL
void  bits(mode* self, s32i slot, const byte* src, u32i size) {
  if(!src && size) emit(self, move, 0);
  for(u32i n; size; slot += n, size -= n) {
    n = size >= 8 && !src? 8: size >= 4? 4: size >= 2? 2: 1;
    if(src) { s32i v = 0; copy((byte*)&v, src, n); src += n; emit(self, move, v); }
    emit(self, stor, slot, n);
  }
}

// an aggregate being initialized at off; the part at hand is memb or i
typedef struct { meta* type; meta* memb; u32i i, off; } tier;

#define bulky(t) ((t)->kind == many || (t)->kind == list)
// a string for a char array
#define text(self, t) ((t)->kind == many && (t)->type->size == 1 && home(self)->talk.kind == '"')
#define TIERS 16

tier  dive(const mode* self, u32i depth, meta* type, u32i off) {
  if(depth == TIERS) flaw(self, "initializer nested too deep\n");
  return (tier){type, type->kind == list? type->type: NULL, 0, off};
}

// NULL past the end of n
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

// a global pointer starting at &global, a global array, a string or a
// function, +- constants: a hook, which dock patches and the form relocates.
// 0, nothing parsed, when the initializer is no address
bool  cite(mode* self, s32i slot) {
  bool  amp = twin(self, '&');
  u32i  c = home(self)->talk.kind;
  item* name = c == ttag? home(self)->talk.hash: NULL;
  meta* m = name? find(self, self->unit, name): NULL;
  seat  aim;
  u32i  size = 1; // the bytes a constant added steps over, 0 for a function
  if(m && m->ext && m->kind != call && m->kind != pend)
    flaw(self, "the address of an import cannot start a global\n");
  if(!amp && c == '"') {
    aim = (seat){NULL, lay(self, (u32i)(name = home(self)->talk.hash)->size - 1, 0, 8)};
    quote(name, self->data->head + aim.off);
    chop(self);
  }
  else if(m && m->kind == call) {
    chop(self);
    if(home(self)->talk.kind == '(') flaw(self, "expected a constant\n");
    if(m->lib) flaw(self, "the address of an import cannot start a global\n");
    aim = (seat){deed(self, name, m), 0};
    size = 0;
  }
  else if(amp || (m && m->kind == glob && m->type->kind == many)) {
    oper x = atom(self); // a global, and its members and elements, is no code
    if(x.kind != vglo || x.type->kind == bitf || (!amp && x.type->kind != many))
      flaw(self, "expected a constant\n");
    aim  = (seat){NULL, (u32i)x.ival};
    size = amp? x.type->size: x.type->type->size;
  }
  else return 0;
  s64i  add = 0;
  for(u32i op; (op = home(self)->talk.kind) == '+' || op == '-'; ) {
    chop(self);
    oper k = expr(self, rank('+') + 1);
    if(k.kind != vnum || !size) flaw(self, "expected a constant\n");
    add += (op == '+'? 1: -1) * (s64i)k.ival * size;
  }
  self->hooks = swell(self, self->hooks, self->hookn, &self->hookc, sizeof(hook), 256);
  self->hooks[self->hookn++] = (hook){.site = {NULL, (u32i)slot}, .aim = aim, .add = add,
                                      .unit = self->unit, .wide = 1};
  return 1;
}

// slot: a data offset for a global (glob), else a frame slot, or a register
// when not below 0. A nested aggregate without braces takes the values that
// follow
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
  else if(glob && type->kind == refs && cite(self, slot)) return;
  else if(glob) {
    oper x = expr(self, 1);
    s64i v;
    bool  big = great(self, x, &v); // a long constant
    if(big) x = flot(self, type)? flo(self, (double)v): (oper){.kind = vnum, .type = x.type};
    if(x.kind == vnum || x.kind == vflo) x = bend(self, x, type);
    byte* at = self->data->head + slot; // after expr: a string in it moves data
    if((x.kind != vnum && x.kind != vflo) || t || type->kind == list)
      flaw(self, "expected a constant\n");
    if(!big) v = x.ival;
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

// a compound literal lives to the end of the block
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

// static: a global only the block sees, set once by constants
void  vars(mode* self) {
  u64i  line = home(self)->line;
  bool  own  = same(self, "static");
  if(own) chop(self);
  meta* base = sort(self);
  if(twin(self, ';')) return; // a struct, defined
  do {
    item* name;
    meta* type = noun(self, base, &name);
    if(own) {
      u32i off = lay(self, type->size, home(self)->talk.kind != '=', 8);
      if(twin(self, '=')) seal(self, type, 1, (s32i)off);
      seed(self, .name=name, .type=type, .kind=glob, .slot=off, .own=1);
      dwarf_glob(self, name, type, off, 1, line);
      continue;
    }
    s32i  slot = stay(self, name, type, type->size);
    seed(self, .name=name, .type=type, .kind=data, .slot=slot);
    dwarf_var(self, name, type, slot, 0, line);
    if(twin(self, '=')) seal(self, type, 0, slot);
  } while(twin(self, ','));
  skip(self, ';');
}

// a redefinition: prev of this unit, or, unless own, a public non-weak one of
// another unit; nor may another unit define it public when this one is
void  once(mode* self, const meta* prev, item* name, bool own) {
  const meta* x = own? NULL: find(self, 0, name);
  if((prev && (prev->kind == glob || prev->kind == call)
      && (prev->unit == self->unit || (!own && !prev->weak)))
     || (x && (x->kind == glob || x->kind == call) && heft(x) == 4))
    flaw(self, "'%.*s' is already defined\n", nick(name));
}

meta* nibs(mode* self, meta* t, u32i at, u32i width) {
  return sole(self, (meta){.kind = bitf, .type = t, .size = t->size, .slot = at, .longs = (byte)width});
}

static void  dupe(mode* self, const meta* type, const item* name) {
  for(meta* m = name? type->type: NULL; m; m = m->hide)
    if(m->name && akin(m->name, name)) flaw(self, "member '%.*s' given twice\n", nick(name));
}

// a member is aligned to its size, the struct to its widest. A bit-field
// follows the one before unless it would cross a unit of its type; an unnamed
// width 0 ends the unit. A struct member without a declarator inlines its members
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
    meta* of = sort(self);
    if(twin(self, ';')) { // a struct without a declarator: its members are this one's
      if(of->kind != list) continue;
      if(of == type) flaw(self, "a struct cannot hold itself\n");
      u32i  a = lean(of);
      bit = (bit + 8 * a - 1) & ~(8 * a - 1);
      for(meta* m = of->type; m; m = m->hide) {
        dupe(self, type, m->name);
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
      dupe(self, type, n);
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

// item.pick while sifting
#define NEVER (1u << 31) // its locals stay in the frame
#define FLOT  (1u << 30) // a local of the name is a real
#define INTS  (1u << 29) // a local of the name is an integer or a pointer
#define USES  (INTS - 1) // 1 plus the uses of the name

#define NEST  32         // brace depths sift tells apart: deeper ones count as this one

// 0 when there are too many names
static u32i  sown(item** c, u32i* n, item* name) {
  if(!name->pick && *n < SIFT) (c[(*n)++] = name)->pick = 1;
  return name->pick;
}

// a local of name declared at depth d clashes with every name live there
// (live[i]: declared at depth i). 1 when one of the name was in scope already:
// the new one gets a frame slot (stay)
static bool  meet(item* const* c, u32i n, const item* name, u32i d, u64i* clash, u64i* live, u64i* mine) {
  u32i  j = 0;
  while(j < n && c[j] != name) j++;
  if(j == n) return 0;
  u64i  in = 0;
  if(d >= NEST) d = NEST - 1;
  for(u32i i = 0; i <= d; i++) in |= live[i];
  bool  again = in >> j & 1;
  in &= ~(1ull << j);
  clash[j] |= in;
  for(u32i i = 0; i < n; i++) if(in >> i & 1) clash[i] |= 1ull << j;
  live[d] |= 1ull << j;
  *mine |= 1ull << j;
  return again;
}

// prescan of a function body: its tokens are lexed once and walked, not parsed
// (chop replays them). Each use of a declared number or pointer counts, 8 times
// per enclosing loop; the most used names (3 uses at least) get a register
// (item.pick), names never in scope together may share one. A name whose
// address is taken anywhere gets none: one pass cannot see a use before it.
// When a call in a loop may come while a computed left operand is kept, a
// spare register is lent to kept operands (mode.lend); out of a loop the stack
// is cheaper. A leaf (when lone) gives spare registers, which need no save,
// first; with no compound literal and every local in a register it has no
// frame (bare). A wrong guess spoils the function: recompiled with lone 0
void  sift(mode* self, u32i top, bool lone) {
  self->leaf = self->bare = 0;
  if((!self->arch.regs && !self->arch.fregs) || self->debug) return; // -g keeps every local in the frame, where a debugger finds it
  unit* u = home(self);
  byte* at = u->code->body;
  u64i  line = u->line;
  word  talk = u->talk;
  item* c[SIFT];
  self->lexn = 0;
  item *loop = sget(self, "while"), *when = sget(self, "when"), *tag = sget(self, "struct");
  item *ret = sget(self, "return"), *alt = sget(self, "else"), *len = sget(self, "sizeof");
  item *last = NULL, *decl = NULL; // the name before, when the token was one; the name just declared
  const meta* of = NULL; // the type of the declaration at hand
  u32i  n = 0, depth = 0, loops = 0, end[8], cond = 0, deep = 0, p = 0; // p: the token before
  s32i  lvl = 0; // brackets open in the declaration
  bool  ok = 1, arm = 0, stars = 0, amp = 0; // arm: a declared name may follow; amp: a unary & came before
  u64i  clash[SIFT] = {0}, live[NEST] = {0};
  // hold: a computed left operand is kept in a loop; prec: rank of the operator
  // the operand ends, NEST for a unary one or ->, 0 for a lone name or number
  u32i  prec = 0;
  bool  hold = 0, lend = 0; // lend: a call while one is held
  // calls: maybe a call, but not # name( (run); lits: a compound literal;
  // again: a local of a name in scope already; divs: maybe an integer division
  bool  calls = 0, run = 0, lits = 0, again = 0, divs = 0;
  u64i  mine = 0; // the names of locals
  for(u32i i = top; ok && i < self->top; i++) {
    const meta* x = pin(self->pile, i);
    if(!(ok = sown(c, &n, x->name))) break;
    x->name->pick += 1;
    x->name->pick |= flot(self, x->type)? FLOT: fits(self, x->type)? INTS: NEVER;
    again |= meet(c, n, x->name, 0, clash, live, &mine); // in scope through the body
  }
  // an operand ends before the token: a & after it is no address
#define VALUE (p == tnum || p == treal || p == ')' || p == ']' || (p == ttag && last != ret && last != alt && last != len))
  // the name before the ( is called: not a keyword taking parentheses
#define CALLS (p == ttag && last != loop && last != when && last != len && last != ret)
  do { // one switch on the token's kind: each kind updates only what it changes
    u32i  k = u->talk.kind;
    item* it = NULL, *was = decl; // it: a name, not a member
    bool  shut = 0, open = !cond, ref = amp; // shut: the ) of a condition, a statement follows; ref: a unary & came before
    decl = NULL, amp = 0;
    if(cond) { cond += (k == '(') - (k == ')'); if(cond == 1) cond = 0, shut = 1; }
    if(k == ttag) goto name; // the most frequent kind: skips the switch's search
    switch(k) {
      case ttag: name: {
        if(p == '.' || p == tokd('-', '>')) { arm = 0; break; } // a member
        it = u->talk.hash;
        u32i  w = 1u << 3 * (loops < 3? loops: 3);
        if(open && (it == loop || it == when)) { cond = 1; if(it == loop && loops < 8) end[loops++] = depth, hold = 0, prec = 0; }
        const meta* m = !it->pick && !cond && it != ret && (p == '{' || p == '}' || p == ';')? find(self, self->unit, it): NULL;
        if(m && (m->kind == base || m->kind == list)) { of = m; arm = 1; stars = 0; lvl = 0; break; }
        if(arm || ref || deep) { // declared, or its address is taken
          if(!(ok = sown(c, &n, it))) goto full;
          if(ref || deep) it->pick |= NEVER;
          else it->pick |= !stars && flot(self, of)? FLOT: stars || fits(self, of)? INTS: NEVER;
          if(arm) { decl = it; again |= meet(c, n, it, depth, clash, live, &mine); }
        }
        if(it->pick && (it->pick & USES) < 1u << 28) it->pick += w;
        arm = 0;
        break;
      }
      case '(':
        lvl++, arm = 0;
        if((CALLS && !run) || p == ')' || p == ']') calls = 1;
        run = 0;
        if(deep) deep++;
        else if(ref) deep = 1;
        if(loops) { if(CALLS) lend |= hold; prec = 0; } // a call
        break;
      case ')':
        lvl--, arm = 0;
        if(deep) deep--;
        if(loops && shut) hold = 0, prec = 0;
        break;
      case '[':
        if(was) was->pick |= NEVER; // an array
        lvl++, arm = 0;
        if(loops) prec = 0;
        break;
      case ']': lvl--, arm = 0; break;
      case '{':
        depth++, lvl++, arm = 0;
        lits |= p == ')';
        if(loops) hold = 0, prec = 0;
        break;
      case '}':
        depth--, lvl--, arm = 0;
        if(open) while(loops && end[loops - 1] == depth) loops--;
        if(depth + 1 < NEST) live[depth + 1] = 0; // the locals of the block it ends leave scope
        if(loops) hold = 0, prec = 0;
        break;
      case ';':
        of = NULL, arm = 0;
        if(open) while(loops && end[loops - 1] == depth) loops--;
        if(loops) hold = 0, prec = 0;
        break;
      case ',':
        if(of && !lvl) arm = 1, stars = 0;
        else arm = 0;
        if(loops) hold = 0, prec = 0;
        break;
      case '#': run = 1, arm = 0; if(loops) hold = 0; break; // its call runs while compiling
      case tokd('/', '='): case tokd('%', '='): divs = 1;
        // fallthrough
      case '=': case tokd('&', '&'): case tokd('|', '|'):
      case tokd('+', '='): case tokd('-', '='): case tokd('*', '='):
      case tokd('&', '='): case tokd('|', '='): case tokd('^', '='): case tokd('<', '<', '='): case tokd('>', '>', '='):
        arm = 0;
        if(loops) hold = 0, prec = 0;
        break;
      case tokd('-', '>'): case '!': case '~': arm = 0; if(loops) prec = NEST; break;
      case '*': case '-': case '&': {
        bool  val = VALUE;
        if(k != '*' || !arm) arm = 0;
        else stars = 1;
        if(k == '&') amp = !val;
        if(!loops) break;
        if(!val) { prec = NEST; break; } // unary
        goto bin;
      }
      case '/': case '%': divs = 1;
        // fallthrough
      case '+': case '|': case '^': case tokd('<', '<'): case tokd('>', '>'):
      case '<': case '>': case tokd('<', '='): case tokd('>', '='): case tokd('=', '='): case tokd('!', '='):
        arm = 0;
        if(!loops) break;
      bin: {
        u32i  r = (u32i)rank((int)k); // binary: its left operand is computed unless a lone name or number
        if(p == ')' || p == ']' || prec >= r) hold = 1;
        prec = r;
        break;
      }
      default: arm = 0;
    }
    if(it != tag || !it) { p = shut? ';': k; last = it; } // struct name is a type as name is
    byte* from = u->code->body;
    lex(self);
    if(self->lexn == self->lexc) self->lexes = swell(self, self->lexes, self->lexn, &self->lexc, sizeof(lexed), 256);
    self->lexes[self->lexn++] = (lexed){from, u->code->body, u->line, u->talk};
  } while(depth && u->talk.kind);
full:
#undef VALUE
#undef CALLS
  u->code->body = at, u->line = line, u->talk = talk;
  self->lexi = 0, self->lexu = self->unit; // chop replays the tokens

  // most used first, to the lowest free register of its kind. A parameter
  // takes no spare that is a later parameter's argument register, not read yet
  bool  leaf = lone && ok && !calls;
  u32i  argc = self->top - top, regs = self->arch.regs, fregs = self->arch.fregs, sarg = self->arch.sarg;
  u32i  spare = leaf? self->arch.spare: 0, sdiv = divs? self->arch.sdiv: 0;
  u32i  fspare = leaf && !(self->arch.cpu == cpu_ia64 && self->form.os == os_win)? self->arch.fspare: 0; // Win64 keeps xmm6-15
  s32i  parm[SIFT]; // the parameter c[j] names, -1 when none
  u32i  got[SIFT] = {0}; // the register c[j] is given, plus 1
  u64i  has[2 * FREG] = {0}; // the names given register r
  u32i  e[SIFT], m = 0;
  for(u32i j = 0; j < n; j++) {
    parm[j] = -1;
    u32i q = c[j]->pick;
    if(ok && (q & USES) > (leaf? 0: 3) && !(q & NEVER) && (q & (FLOT | INTS)) != (FLOT | INTS)) e[m++] = j;
  }
  for(u32i i = 0; i < argc && i < n; i++) { // sown first, in order
    u32i j = 0;
    while(j < n && c[j] != pin(self->pile, top + i)->name) j++;
    if(j < n) parm[j] = (s32i)i;
  }
  for(u32i b = m; b--; ) { // a parameter in its own argument register
    u32i j = e[b], s = (u32i)parm[j] - sarg; // the spare of that register
    if(parm[j] < 0 || c[j]->pick & FLOT || s >= self->arch.sargs || s >= spare || sdiv >> s & 1) continue;
    has[regs + s] |= 1ull << j;
    got[j] = regs + s + 1;
    e[b] = e[--m];
  }
  while(m) {
    u32i b = 0;
    for(u32i i = 1; i < m; i++) if((c[e[i]]->pick & USES) > (c[e[b]]->pick & USES)) b = i;
    u32i j = e[b];
    e[b] = e[--m];
    bool f = c[j]->pick & FLOT;
    u32i r = f? FREG + fregs: regs, lim = r + (f? fspare: spare);
    for(; r < lim; r++) {
      u32i s = r - (f? FREG + fregs: regs), g = s + sarg; // spare s is argument register g
      if(clash[j] & has[r] || (!f && sdiv >> s & 1)) continue;
      if(!f && parm[j] >= 0 && s < self->arch.sargs && (s32i)g > parm[j] && g < argc) continue;
      break;
    }
    if(r == lim) {
      if((c[j]->pick & USES) <= 3) continue;
      r = f? FREG: 0, lim = f? FREG + fregs: regs;
      while(r < lim && clash[j] & has[r]) r++;
      if(r == lim) continue;
      if(f && self->fregn <= r - FREG) self->fregn = r - FREG + 1;
      if(!f && self->regn <= r) self->regn = r + 1;
    }
    has[r] |= 1ull << j;
    got[j] = r + 1;
  }
  for(u32i j = 0; j < n; j++) if((c[j]->pick = got[j])) self->pick[self->picks++] = c[j];
  self->lend = self->regn;
  if(lend && self->regn < self->arch.regs) self->regn++;
  bool  all = ok && !lits && !again && !self->regn && !self->fregn;
  for(u32i j = 0; all && j < n; j++) if(mine >> j & 1 && !c[j]->pick) all = 0;
  self->leaf = leaf;
  self->bare = leaf && all;
}

// bytes saved per real register: Win64 keeps all 16 of xmm8-15, else 8 or none
#define FULL(self) ((self)->arch.cpu == cpu_ia64 && (self)->form.os == os_win? 16: 8)
// the slot saving register r; the real ones (flot) after the integer ones
#define SAVE(self, r, flot) (flot? -8 * (s32i)(self)->regn - FULL(self) * (s32i)((r) + 1): -8 * (s32i)((r) + 1))

void  fini(mode* self) {
  for(u32i r = 0; r < self->regn; r++) emit(self, stow, r, SAVE(self, r, 0), 1);
  for(u32i r = 0; r < self->fregn; r++) emit(self, fstow, r, SAVE(self, r, 1), 1, FULL(self));
  byte* from = self->text->body;
  if(self->bare) emit(self, bare);
  else emit(self, done);
  dwarf_done(self, from);
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
  dwarf_line(self);
  if(twin(self, '{')) {
    dwarf_enter(self);
    while(!twin(self, '}')) stmt(self);
    dwarf_leave(self);
    self->lits = lits;
  }
  else if(twin(self, ';')) {
  }
  else if(type(self) || same(self, "static")) {
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

// an import from the library of the last load, or "c". Redeclaring the same
// import keeps its slot; over C code of an object or archive x is a prototype
// (cabi). A new overload (lots) is checked by its symbol (dub)
void  hire(mode* self, meta* x, const meta* prev, bool lots) {
  x->ext = 1;
  if(x->kind == call && prev && (prev->kind == pend || (prev->kind == call && prev->ext && !prev->lib)))
    return;
  x->lib = home(self)->from? home(self)->from: rack(self, sget(self, "\"c\""));
  if(prev && prev->ext && !prev->addr && prev->kind == x->kind && prev->lib == x->lib
     && prev->type == x->type && prev->size == x->size && prev->more == x->more)
    x->slot = prev->slot;
  else if(prev || !lots) once(self, prev, x->name, x->own);
}

item* bond(mode* self) {
  item* s = home(self)->talk.hash;
  if(home(self)->talk.kind != '"') flaw(self, "expected a string\n");
  chop(self);
  return s;
}

void  feed(mode* self, u32i at);

void  decl(mode* self) {
  u64i  born = home(self)->line; // where a function or global starts, for the debugger
  // the source a char* # gives is declarations, as a unit of their own
  if(twin(self, '#')) {
    u64i  line = home(self)->line;
    yield y = sharp(self);
    if(y.text) {
      if(self->layers == 64) flaw(self, "generated source nests too deep\n");
      file* code = self->heap(NULL, sizeof(file) + y.size);
      code->body = code->head;
      code->tail = code->head + y.size;
      copy(code->head, y.text, y.size + 1);
      char* path = self->heap(NULL, slen(home(self)->path) + 24);
      snprintf(path, slen(home(self)->path) + 24, "%s#%llu", home(self)->path, (unsigned long long)line);
      self->layers++;
      feed(self, nest(self, (unit){.path = path, .code = code, .kind = un_code}));
      self->layers--;
    }
    twin(self, ';');
    return;
  }
  // load: the bodyless functions after it in this source are imported from it
  if(same(self, "load")) {
    chop(self);
    home(self)->from = rack(self, bond(self));
    skip(self, ';');
    return;
  }
  // link: a source, an object or an archive, linked here once
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

  item* name;
  meta* base = sort(self);
  if(twin(self, ';')) return;
  meta* type = term(self, base, &name);

  if(twin(self, '(')) {
    const meta* prev = find(self, self->unit, name);
    seed(self, .name=name, .type=type, .kind=call, .own=own);
    meta* func = self->func = pin(self->pile, self->top - 1);
    u32i  top  = self->top;
    bool  anon = 0;
    meta* args[6];
    self->slot = self->deep = self->lits = self->busy = 0;
    for(; !twin(self, ')'); func->size++, twin(self, ',')) {
      if(twin(self, tokd('.', '.', '.'))) { func->more = 1; skip(self, ')'); break; }
      item* n;
      meta* of = coil(self, sort(self), &n);
      if(!n && !of->size && !func->size && twin(self, ')')) break; // (void)
      if(func->size < 6) args[func->size] = of;
      if(wide(of)) func->longs |= 1 << func->size;
      if(flot(self, of)) func->reals |= 1 << func->size;
      if(!n) { anon = 1; continue; } // an import may leave it out
      seed(self, .name=n, .type=of, .kind=data, .slot=-1); // placed once the body is sifted
    }
    if(func->size > 6) flaw(self, "more than 6 parameters\n");
    if(func->size) {
      func->args = self->heap(NULL, func->size * sizeof(meta*));
      copy((byte*)func->args, args, func->size * sizeof(meta*));
    }
    // func may declare one overload again (prev) and overload the others (c)
    meta* c[KIN];
    u32i  n = ours(prev)? kin(self, self->unit, name, c, func): 0, lots = 0;
    if(n) {
      prev = NULL;
      for(u32i i = 0; i < n; i++) if(peer(c[i], func)) prev = c[i]; else c[lots++] = c[i];
    }
    if(twin(self, ';')) {
      hire(self, func, prev, lots);
      if(lots) { crowd(self, func, c, lots, (meta*)prev); dub(self, func, prev); }
      bury(self, top);
      return;
    }
    if(prev || !lots) once(self, prev, name, own);
    if(lots) crowd(self, func, c, lots, NULL);
    if(anon) flaw(self, "a parameter without a name\n");
    if(func->more) flaw(self, "only an import can be variadic\n");
    if(home(self)->talk.kind != '{') flaw(self, "expected function body\n");
    self->frame = top;
    byte* hail = self->text->body;
    emit(self, hail, self->form.os, func->size, func->reals | flot(self, func->type) << RETS);
    func->addr = self->text->body;
    func->hail = hail != func->addr? hail: NULL;
    dwarf_open(self, born);
    // put back when sift's guess spoils the function
    unit  at = *home(self);
    byte* body = at.code->body;
    u32i  fixs = self->fixs, imps = self->impn, size = (u32i)(self->data->body - self->data->head);
    u64i  bss = self->zero;
    for(bool lone = 1;; lone = 0) {
      sift(self, top, lone);
      self->slot = self->deep = 8 * self->regn + FULL(self) * self->fregn; // the first slots save the registers of locals
      if(!self->bare) emit(self, open);
      for(u32i r = 0; r < self->regn; r++) emit(self, stow, r, SAVE(self, r, 0), 0);
      for(u32i r = 0; r < self->fregn; r++) emit(self, fstow, r, SAVE(self, r, 1), 0, FULL(self));
      for(u32i i = 0; i < func->size; i++) {
        meta* p = pin(self->pile, top + i);
        p->slot = stay(self, p->name, p->type, 8);
        dwarf_var(self, p->name, p->type, p->slot, 1, born);
        if(p->slot < 0) { emit(self, parm, i, p->slot); continue; }
        if(p->slot >= FREG) { emit(self, fbit, 1, p->slot - FREG, ARG - (s32i)i); continue; } // a real's bits
        emit(self, rop, '=' | wide(p->type), p->slot, ARG - (s32i)i, 0);
        if(span(p->type) < 4) emit(self, rop, '&', p->slot, NUM, (1 << span(p->type) * 8) - 1); // as a slot would hold it
      }
      stmt(self);
      self->lexn = 0; // the body is parsed: its tokens can go
      emit(self, move, 0); fini(self); // implicit return 0
      if(!self->spoil) break;
      trim(self, func->addr);
      self->fixs = fixs;
      for(u32i i = imps; i < self->impn; i++) self->imps[i]->slot = 0;
      self->impn = imps;
      file* d = self->data;
      for(u32i i = size; d->head + i < d->body; i++) d->head[i] = 0;
      d->body = d->head + size;
      self->zero = bss;
      self->busy = 0, self->spoil = 0;
      while(self->picks) self->pick[--self->picks]->pick = 0;
      self->regn = self->fregn = self->lend = 0;
      home(self)->code->body = body, home(self)->line = at.line, home(self)->talk = at.talk;
    }
    if(!self->bare) emit(self, size, func->addr, (self->deep + 15) & -16);
    dwarf_func(self, func, born, func->addr);
    if(lots) dub(self, func, NULL);
    bury(self, top);
    self->frame = ~0u;
    while(self->picks) self->pick[--self->picks]->pick = 0;
    self->regn = self->fregn = self->lend = 0;
    self->leaf = self->bare = 0;
    reel(self); // between functions: members go after text
    return;
  }

  for(type = full(self, type, name);; type = noun(self, base, &name)) {
    if(ext) {
      const meta* prev = find(self, self->unit, name);
      seed(self, .name=name, .type=type, .kind=glob, .own=own);
      hire(self, pin(self->pile, self->top - 1), prev, 0);
      if(!twin(self, ',')) break;
      continue;
    }
    once(self, find(self, self->unit, name), name, own);
    u32i off = lay(self, type->size, home(self)->talk.kind != '=', 8);
    if(twin(self, '=')) seal(self, type, 1, (s32i)off);
    seed(self, .name=name, .type=type, .kind=glob, .slot=off, .own=own);
    dwarf_glob(self, name, type, off, own, born);
    if(!twin(self, ',')) break;
  }
  skip(self, ';');
}

// each source keeps its own place (line, talk, from) in its unit. The file is
// freed once read, its names have their own bytes; an archive's at the end
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
    for(chop(self); home(self)->talk.kind; self->ripe = (u32i)(self->data->body - self->data->head)) decl(self);
    tie(self, fixs, 0);
    self->heap(code, 0);
    self->units[at].code = NULL;
  }
  self->unit = back;
}

// 0 when path is in already. A unit joins as it starts, so a cycle back into
// it sees what it has defined so far
bool  join(mode* self, const char* path, u64i size, file* code, byte kind) {
  for(const unit* u = self->units + 1; kind != un_memb && u < self->units + self->unitn; u++)
    if(u->kind != un_memb && alike(u->path, path, size + 1)) { self->heap(code, 0); return 0; }
  feed(self, nest(self, (unit){.path = path, .code = code, .kind = kind}));
  return 1;
}

// path is from mode.heap, freed when the file is in already; code is read
// when NULL. 0, path still the caller's, when it cannot be read
bool  land(mode* self, char* path, u64i size, file* code, byte kind) {
  size = tidy(path, size);
  if(!code && !(code = self->scan(path))) return 0;
  if(!join(self, path, size, code, kind)) self->heap(path, 0);
  return 1;
}

// a file named on the command line: fatal when it cannot be read
void  lead(mode* self, const char* name, file* code, byte kind) {
  u64i  size = slen(name);
  char* path = self->heap(NULL, size + 1);
  copy((byte*)path, name, size + 1);
  if(!land(self, path, size, code, kind)) fail("cannot open: %s\n", name);
}

// the front end: the start of a linked C library, else NULL. A mirror stops
// here: it has no image to write
meta* build(mode* self) {
  self->frame = ~0u;
  self->core = (core){dock, gap, lay, hget, find, heft, ask, sign, knit, dwarf_pack};
  self->text = (file*)self->hold(0, CODE);
  self->text->body = self->text->head;
  self->text->tail = (byte*)self->text + CODE;
  self->data = zero(self, sizeof(file) + DATA);
  self->data->body = self->data->head;
  self->data->tail = self->data->head + DATA;
  if(self->debug) self->dbg = zero(self, sizeof(dbg));

  emit(self, boot);

  self->units[0].path = self->name; // the root, made by main
  crux(self, void, .size=0);
  crux(self, bool, .size=1);
  crux(self, byte, .size=1);
  crux(self, char, .size=1);
  crux(self, nu16, .size=2);
  crux(self, int,  .size=4);
  crux(self, long, .size=8);
  crux(self, real, .size=8);

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

  // nothing is pulled or called forward from here on
  for(unit* u = self->units + 1; u < self->units + self->unitn; u++)
    if(u->kind == un_arch) { self->heap(u->code, 0); u->code = NULL; }
  self->askx.slot = self->heap(self->askx.slot, 0);
  self->askx.size = self->askx.used = 0;
  self->link = self->heap(self->link, 0);
  self->fixc = 0;
  return libc;
}

void  make(mode* self) {
  meta* libc = build(self);

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
    // a Linux program with imports starts through the shared libc the same way
    // (libs is set only when writing: running binds the imports itself)
    else if(self->impn && self->libs && self->form.os == os_linux) {
      item* start = self->clib == cl_bionic? sget(self, "__libc_init"): sget(self, "__libc_start_main");
      if(self->clib == cl_bionic) libc = find(self, 0, start);
      if(!libc || libc->kind != call || !libc->lib)
        libc = mint(self, (meta){.name = start, .kind = call, .ext = 1,
                                 .type = tget(self, int),
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
