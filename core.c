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
typedef enum { vacc, vnum, vloc } vkind; // TODO global
typedef struct { vkind kind; s32i ival; } oper; // ival: value or slot
#define cnst(n) ((oper){vnum, (n)})
#define acc     ((oper){vacc, 0})

oper  load(mode* self, oper x) {
  if(x.kind == vnum) emit(self, move, x.ival);
  if(x.kind == vloc) emit(self, load, x.ival);
  return acc;
}

// rewind text to mark: calls in the dropped code are checked but not patched
void  trim(mode* self, byte* mark) {
  self->text->body = mark;
  for(u32i i = self->fixs; i-- && self->link[i].at >= mark; ) self->link[i].at = NULL;
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

#define nick(x) (int)(x)->size, (x)->name

// point the call at to the function name with argc arguments
void  bind(mode* self, byte* at, item* name, u32i argc) {
  meta* func = name->data;
  if(!func || func->kind != call)
    fail("line %ld: no function '%.*s'\n", self->line, nick(name));
  if(func->size != argc)
    fail("line %ld: '%.*s' takes %d arguments, got %d\n", self->line, nick(name), func->size, argc);
  if(at) emit(self, link, at, func->addr);
}

// name(args): each argument is pushed, then popped into its register
oper  dial(mode* self, item* name) {
  u32i argc = 0;
  for(; !twin(self, ')'); argc++) {
    if(argc) skip(self, ',');
    if(argc == 6) fail("line %ld: more than 6 arguments\n", self->line);
    eval(self);
    emit(self, push);
  }
  for(u32i i = argc; i--; ) emit(self, args, i);
  byte* at = self->text->body;
  emit(self, dial);
  if(name->data) bind(self, at, name, argc);
  else if(self->fixs == sizeof self->link / sizeof *self->link)
    fail("line %ld: too many forward calls\n", self->line);
  else self->link[self->fixs++] = (fix){name, at, argc, self->line};
  return acc;
}

oper  unit(mode* self) {
  u32i op = self->talk.kind;
  oper x;
  switch(op) {
    case tnum: x = cnst(self->talk.ival); chop(self); return x;
    case ttag: {
      item* name = self->talk.hash;
      meta* var = name->data;
      chop(self);
      if(twin(self, '(')) return dial(self, name);
      if(!var || var->kind != data)
        fail("line %ld: no variable '%.*s'\n", self->line, nick(name));
      return (oper){vloc, var->slot};
    }
    case '(':  chop(self); x = expr(self, 1); skip(self, ')'); return x;
    case '-': case '!': case '~':
      chop(self);
      x = unit(self);
      if(x.kind == vnum)
        return cnst(op == '-'? (s32i)-(u32i)x.ival: op == '!'? !x.ival: ~x.ival);
      load(self, x);
      if(op == '-') { emit(self, swap); emit(self, move, 0); emit(self, calc, '-', 0, 0); }
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
  if(x.kind == vnum) {
    y = expr(self, r + 1);
    if(!x.ival != any) { trim(self, mark); return cnst(any); } // y is dead
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

// x op y, y parsed after mark; x was pushed there when it was in acc
oper  pair(mode* self, u32i op, oper x, oper y, byte* mark) {
  if(y.kind == vacc) {
    if(x.kind == vacc) emit(self, pull);
    else { emit(self, swap); load(self, x); }
  }
  else {
    bool trap = (op == '/' || op == '%') && y.kind == vnum && !y.ival; // left to run time
    if(x.kind == vnum && y.kind == vnum && !trap)
      return cnst(fold(op, x.ival, y.ival));
    self->text->body = mark; // y emitted nothing: drop the push
    load(self, x);
    if(y.kind == vnum) { emit(self, calc, op, 1, y.ival); return acc; }
    emit(self, peek, y.ival);
  }
  emit(self, calc, op, 0, 0);
  return acc;
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
  if(x.kind != vloc) fail("line %ld: cannot assign\n", self->line);
  byte* mark = self->text->body;
  oper y = expr(self, 1);
  load(self, op == '='? y: pair(self, op, x, y, mark));
  emit(self, stor, x.ival);
  return acc;
}

// precedence climbing over binary operators of rank >= least
oper  expr(mode* self, int least) {
  oper x = unit(self);
  u32i op = sets(self->talk.kind);
  if(op && least <= 1) { chop(self); return give(self, op, x); }
  for(u32i r; (r = rank(op = self->talk.kind)) >= (u32i)least; ) {
    chop(self);
    if(op == tokd('&', '&') || op == tokd('|', '|')) { x = both(self, op, x, r); continue; }

    byte* mark = self->text->body;
    if(x.kind == vacc) emit(self, push); // y may need acc
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

// a fresh frame slot
s32i  room(mode* self) {
  self->slot += 8;
  if(self->deep < self->slot) self->deep = self->slot;
  return -self->slot;
}

#define type(self) ((self)->talk.kind == ttag && (self)->talk.hash->data &&\
  ((meta*)(self)->talk.hash->data)->kind == base)

meta* sort(mode* self) {
  meta* type = self->talk.kind == ttag? self->talk.hash->data: NULL;
  if(!type || (type->kind != base && type->kind != list)) {
    if(self->talk.kind == ttag)
      fail("line %ld: expected type, got '%.*s'\n", self->line, nick(self->talk.hash));
    fail("line %ld: expected type, got %x\n", self->line, self->talk.kind);
  }
  chop(self);
  if(self->talk.kind != ttag)
    fail("line %ld: expected name, got %x\n", self->line, self->talk.kind);
  return type;
}

// type name (= expr)? (, name (= expr)?)* ;
void  vars(mode* self) {
  meta* type = sort(self);
  do {
    if(self->talk.kind != ttag)
      fail("line %ld: expected name, got %x\n", self->line, self->talk.kind);
    seed(self, .name=self->talk.hash, .type=type, .kind=data, .slot=room(self));
    chop(self);
    if(twin(self, '=')) give(self, '=', (oper){vloc, self->pile[self->top-1].slot});
  } while(twin(self, ','));
  skip(self, ';');
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
    if(self->talk.kind != ';') eval(self);
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
    u32i  top  = self->top;
    func->kind = call;
    self->slot = self->deep = 0;
    while(!twin(self, ')')) {
      meta* type = sort(self);
      seed(self, .name=self->talk.hash, .type=type, .kind=data, .slot=room(self));
      chop(self);
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

  for(fix* f = self->link; f < self->link + self->fixs; f++) {
    self->line = f->line;
    bind(self, f->at, f->name, f->argc);
  }

  meta* func = sget(self, "main")->data;
  if(!func || func->kind != call) fail("no main function\n");
  self->main = func->addr;
  emit(self, link, self->text->head, self->main);

  self->show(self);
  self->drop(self->text, TEXT);
}

