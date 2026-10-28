// vim: ts=2:sw=2
#include "core.h"

enum { tend, ttag, tnum };
#define TEXT (1 << 16)
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
  //u64i  clue = 5381;
  //for(u64i i = 0; i < size; clue += (clue << 5) + clue + mark[i++]);
  //u64i  clue = 0;
  //for(u64i i = 0; i < size; clue += (clue << 6) + (clue << 16)i - clue + mark[i++]);
  //printf("clue %016lx %08lx % 4ld %.*s\n", clue, clue & (POOL - 1), size, (int)size, mark);

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
    //printf("hash %p %.*s\n", self->talk.hash, c - self->code->body, self->code->body);
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

// deferred operand: code is emitted only when the value must be in acc
typedef enum { vacc, vnum } vkind; // TODO local, global
typedef struct { vkind kind; s32i ival; } oper;
#define cnst(n) ((oper){vnum, (n)})
#define acc     ((oper){vacc, 0})

oper  load(mode* self, oper x) {
  if(x.kind == vnum) emit(self, move, x.ival);
  return acc;
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

oper  unit(mode* self) {
  u32i op = self->talk.kind;
  oper x;
  switch(op) {
    case tnum: x = cnst(self->talk.ival); chop(self); return x;
    case '(':  chop(self); x = expr(self, 1); skip(self, ')'); return x;
    case '-': case '!': case '~':
      chop(self);
      x = unit(self);
      if(x.kind == vnum && !self->O0)
        return cnst(op == '-'? (s32i)-(u32i)x.ival: op == '!'? !x.ival: ~x.ival);
      load(self, x);
      if(op == '-') { emit(self, swap, 0); emit(self, calc, '-', 0, 0); }
      else emit(self, calc, op == '!'? tokd('=', '='): '^', 1, op == '!'? 0: -1);
      return acc;
    default:
      fail("line %ld: expected expression, got %x\n", self->line, op);
  }
}

// x && y, x || y: y runs only when x does not decide, the result is 0 or 1
oper  both(mode* self, u32i op, oper x, int r) {
  bool  any  = op == tokd('|', '|'); // the value of x that decides
  byte* mark = self->text->body;
  oper  y;
  if(x.kind == vnum && !self->O0) {
    y = expr(self, r + 1);
    if(!x.ival != any) { self->text->body = mark; return cnst(any); } // y is dead
    if(y.kind == vnum) return cnst(!!y.ival);
    emit(self, calc, tokd('!', '='), 1, 0);
    return acc;
  }
  load(self, x);
  emit(self, when, any);
  mark = self->text->body;
  load(self, expr(self, r + 1));
  emit(self, calc, tokd('!', '='), 1, 0);
  emit(self, jump);
  emit(self, back, mark);
  mark = self->text->body;
  emit(self, move, any);
  emit(self, back, mark);
  return acc;
}

// precedence climbing over binary operators of rank >= least
oper  expr(mode* self, int least) {
  oper x = unit(self);
  for(u32i op, r; (r = rank(op = self->talk.kind)) >= (u32i)least; ) {
    chop(self);
    if(op == tokd('&', '&') || op == tokd('|', '|')) { x = both(self, op, x, r); continue; }

    byte* mark = self->text->body;
    if(x.kind == vacc) emit(self, push); // y may need acc
    oper y = expr(self, r + 1);
    if(y.kind == vnum) {
      bool trap = (op == '/' || op == '%') && !y.ival; // left to run time
      if(x.kind == vnum && !self->O0 && !trap) { x.ival = fold(op, x.ival, y.ival); continue; }
      self->text->body = mark; // y emitted nothing: drop the push
      load(self, x);
      emit(self, calc, op, 1, y.ival);
    }
    else {
      if(x.kind == vnum) emit(self, swap, x.ival);
      else               emit(self, pull);
      emit(self, calc, op, 0, 0);
    }
    x = acc;
  }
  return x;
}

// value of a full expression in acc
void  eval(mode* self) {
  load(self, expr(self, 1));
}

void  stmt(mode* self) {
  if(twin(self, '{')) {
    while(!twin(self, '}')) stmt(self);
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
    chop(self);
    skip(self, '('); eval(self); skip(self, ')');
    emit(self, when, 0); // TODO loop back, patch the exit
    stmt(self);
  }
  else if(same(self, "return")) {
    chop(self);
    eval(self); skip(self, ';');
    emit(self, done);
  }
  else {
    expr(self, 1); skip(self, ';');
  }
}

#define seed(self, ...) {\
  meta x = { __VA_ARGS__ }; \
  x.name->data = self->pile+self->top; \
  self->pile[self->top++] = x; }

#define crux(slef, str, ...) \
  seed((self), .kind = base, .name = sget((self), (str)), __VA_ARGS__)

meta* sort(mode* self) {
  meta* type = self->talk.kind == ttag? self->talk.hash->data: NULL;
  if(!type || (type->kind != base && type->kind != list)) {
    if(self->talk.kind == ttag)
      fail("line %ld: expected type, got '%.*s'\n", self->line,
        (int)self->talk.hash->size, self->talk.hash->name);
    fail("line %ld: expected type, got %x\n", self->line, self->talk.kind);
  }
  chop(self);
  if(self->talk.kind != ttag)
    fail("line %ld: expected name, got %x\n", self->line, self->talk.kind);
  return type;
}

void  decl(mode* self) {
  // declaration begins with type or struct or union or enum
  // "list"  name? "{" decl "}" ";"
  // "poly"  name? "{" decl "}" ";"
  // "enum"  name? "{" list "}" ";"

  if(same(self, "list")) {
    chop(self);
    twin(self, ttag); // optional name
    skip(self, '{');
    while(!twin(self, '}')) { // TODO members
      if(!self->talk.kind) fail("line %ld: unterminated list\n", self->line);
      chop(self);
    }
    skip(self, ';');
    return;
  }

  meta* type = sort(self); // get the name

  seed(self, .name=self->talk.hash, .type=type);
  chop(self); // get next token

  // type name "(" args ")" "{" stmt "}"     // callable decl
  if(twin(self, '(')) {
    meta* func = self->pile + self->top - 1;
    func->kind = call;
    while(!twin(self, ')')) {
      meta* type = sort(self);
      seed(self, .name=self->talk.hash, .type=type, .kind=data);
      chop(self);
      twin(self, ',');
    }
    func->addr = self->text->body;
    stmt(self);
    emit(self, move, 0); emit(self, done); // implicit return 0
  }
  else
  if(twin(self, '[')) {
    // array
    self->pile[self->top-1].kind = list;
    chop(self);
    self->pile[self->top-1].size = self->talk.ival; // TODO error parsing size
    skip(self, ']');
    skip(self, ';'); // XXX init?
  }
  else
  while(!twin(self, ';')) {
    chop(self);
  }
}

void  make(mode* self) {
  // unit = callable
  //      | variable
  //      | typedecl

  self->line = 1;
  self->text = (file*)self->hold(0, TEXT);
  self->text->body = self->text->head;
  self->text->tail = (byte*)self->text + TEXT;

  emit(self, boot);

  crux(self, "void", .size=0);
  crux(self, "bool", .size=1);
  crux(self, "byte", .size=1);
  crux(self, "char", .size=1);
  crux(self, "nu16", .size=2);
  crux(self, "int",  .size=4);
  crux(self, "long", .size=8);

  chop(self);
  do {
    decl(self);
  } while(self->talk.kind);

  meta* func = sget(self, "main")->data;
  if(!func || func->kind != call) fail("no main function\n");
  self->main = func->addr;
  emit(self, link, self->text->head, self->main);

  self->show(self);
  self->drop(self->text, TEXT);
}

