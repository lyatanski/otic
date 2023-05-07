// vim: ts=2:sw=2
#include "core.h"

enum { tend, ttag, tnum };
#define tokc(x, y, z, ...) ((x) | (y) << 8 | (z) << 16)
#define tokd(...) tokc(__VA_ARGS__, 0, 0, 0)
#define tok1(x) ((x)+=1, tokd((x)[-1]))
#define tok2(x) ((x)+=2, tokd((x)[-2], (x)[-1]))
#define tok3(x) ((x)+=3, tokd((x)[-3], (x)[-2], (x)[-1]))

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

  return seek;
}

void chop(mode* self) {
  s64i  ival = 0;
  s64i  fval = 0;
  u32i  kind = tend;
  byte* c = self->code->body;

  pass: switch(*(self->code->body = c)) {
    case '\n': self->line++;
    case ' ': case '\f': case '\t': case '\v': case '\r': c++; goto pass;
    case '#': case ',': case ';': case ':': case '?': case '~':
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
        case '/': while(*c++ != '\n'); goto pass;
        case '*': while(c[-2] != '*' || c[-1] != '/') c++; goto pass;
        default: kind = (c[1] == '='? tok2(c): tok1(c));
      }
      break;
    //case '\'':
    //  kind = '"'; c+=2; break;
    case '"':
      while(*c++ && *c != '"') {
        //if(*c == '\n' || *c == '\0') fail("unterminated string %s\n", c);
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
        c++;
      }
      c++;
      kind = '"'; break;
    case '.':
      // '.', '...' or number
      if     (c[1] == '.' && c[2] == '.') { kind = tok3(c); break; }
      else if(c[1] <= '0' || c[1] >= '9') { kind = tok1(c); break; }
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
    default: break;
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

bool  twin(mode* self, int   kind) {
  return (self->talk.kind == kind)? chop(self), 1 : 0;
}

bool  skip(mode* self, int   kind) {
  if(!twin(self, kind))
    fail("line %ld expected %x, got %x\n", self->line+1, kind, self->talk.kind);
  return 1;
}

int rank(int op) {
  switch (op) {
    default:                                                      return  0;
    case '?':                                                     return  3;
    case tokd('|', '|'):                                          return  4;
    case tokd('&', '&'):                                          return  5;
    case '|':                                                     return  6;
    case '^':                                                     return  7;
    case '&':                                                     return  8;
    case tokd('=', '='): case tokd('!', '='):                     return  9;
    case '<': case tokd('<', '='): case '>': case tokd('>', '='): return 10;
    case '+': case '-':                                           return 11;
    case '*': case '/': case '%':                                 return 12;
  }
}

#define emit(self, call, ...) (self->arch.call(self->text __VA_OPT__(,) __VA_ARGS__))
void  expr(mode* self) {
  int rank = 0xffff;

  switch(self->talk.kind) {
    case tnum:
      emit(self, move, self->talk.ival);
      chop(self);
    break;
    case '"':
    break;
    case ttag:
    case tokd('('): /* type cast     */ skip(self, ')'); break;
    //case tokd('*'): /* pointer deref */ break;
    case tokd('@'): /* pointer deref */ break;
    case tokd('&'): /* address of    */ break;
    break;
    //case tokd('['):      break;
    default:
      fail("expr unexpected %x\n", self->talk.kind);
  }

  while(self->talk.kind >= rank) {
    switch(self->talk.kind) {
      case tokd('='):      break;
      case tokd('?'):      break;
      case tokd('^'): case tokd('|'): case tokd('&'):
      case tokd('+'): case tokd('-'): case tokd('*'):
      case tokd('/'): case tokd('%'): emit(self, calc, self->talk.kind); break;
      case tokd('['):      break;
      case tokd('<'):      break;
      case tokd('>'):      break;
      case tokd('<', '='): break;
      case tokd('>', '='): break;
      case tokd('!', '='): break;
      case tokd('=', '='): break;
      case tokd('<', '<'): emit(self, calc, '<'); break;
      case tokd('>', '>'): emit(self, calc, '>'); break;
      case tokd('+', '+'): break;
      case tokd('-', '-'): break;
      case tokd('|', '|'): break;
      case tokd('&', '&'): break;

      case tokd('('):
        // function call
        while(!twin(self, ')')) {
          //expr(Assign);
        }
        break;
      default: fail("error %x", self->talk.kind); break;
    }
  }
}

void  stmt(mode* self) {
  if(twin(self, '{')) {
    while(!twin(self, '}')) stmt(self);
  }
  else if(same(self, "if")) {
    byte* mark = NULL;
    chop(self);
    skip(self, '('); expr(self); skip(self, ')');
    emit(self, when, 0);
    mark = self->text->body - 1;
    stmt(self);
    if(same(self, "else")) {
      chop(self);
      emit(self, jump);
      emit(self, back, mark);
      mark = self->text->body - 1;
      stmt(self);
    }
    emit(self, back, mark);
  }
  else if(same(self, "while")) {
    chop(self);
    skip(self, '('); expr(self); skip(self, ')');
    emit(self, when, 1);
    stmt(self);
  }
  else if(same(self, "return")) {
    chop(self);
    expr(self); skip(self, ';');
    emit(self, done, 1);
  }
  else {
    expr(self); skip(self, ';');
  }
}

void  stack(mode* self) {
  static const char* kind[] = { "base", "list", "data", "call" };
  printf("---\n");
  for(int i = 0; i < self->top; i++) {
    printf("%02d element: sz:%d id:%-10.*s %s", i,
      self->pile[i].size,
      (int)self->pile[i].name->size,
      self->pile[i].name->name,
      kind[self->pile[i].kind]);
    if(self->pile[i].type) {
      meta* t = self->pile[i].type;
      printf(" %-10.*s\n",
      (int)t->name->size,
      t->name->name);
    }
    else {
      printf("\n");
    }
  }
}

#define seed(self, ...) {\
  meta x = { __VA_ARGS__ }; \
  x.name->data = self->pile+self->top; \
  self->pile[self->top++] = x; }

#define crux(slef, str, ...) \
  seed((self), .kind = base, .name = sget((self), (str)), __VA_ARGS__)

void  decl(mode* self) {
  // declaration begins with type or struct or union or enum
  // "list"  name? "{" decl "}" ";"
  // "poly"  name? "{" decl "}" ";"
  // "enum"  name? "{" list "}" ";"

  if(same(self, "list")) {
    skip(self, '{');
    while(!twin(self, '}')) {
      skip(self, ';');
    }
    skip(self, '}');
  //  skip(self, ';');
  }

  meta* type = self->talk.hash->data;
  if(!type || (type->kind != base && type->kind != list))
    fail("expected type, got '%.*s'\n",
      (int)self->talk.hash->size,
      self->talk.hash->name);
  chop(self); // get the name

  seed(self, .name=self->talk.hash, .type=type);
  chop(self); // get next token

  // type name "(" args ")" "{" stmt "}"     // callable decl
  if(twin(self, '(')) {
    self->pile[self->top-1].kind = call;
    while(!twin(self, ')')) {
      meta* type = self->talk.hash->data;
      if(!type || (type->kind != base && type->kind != list))
        fail("expected param type, got %p %p '%.*s'\n",
          self->talk.hash, type,
          (int)self->talk.hash->size,
          self->talk.hash->name);
      chop(self);
      seed(self, .name=self->talk.hash, .type=type, .kind=data);
      chop(self);
      twin(self, ',');
    }
    stack(self);
    stmt(self);
  }
  else
  if(twin(self, '[')) {
    // array
    self->pile[self->top-1].kind = list;
    chop(self);
    self->pile[self->top-1].size = self->talk.ival;
    skip(self, ']');
  }
}

void  make(mode* self) {
  // unit = callable
  //      | variable
  //      | typedecl

  self->line = 1;
  self->text = (file*)self->hold(0, 1024);
  self->text->body = self->text->head;
  self->text->tail = self->text->head + 1000;

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

  self->show(self->text);
  self->drop(self->text, 1024);
}

