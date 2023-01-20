// vim: ts=2:sw=2
#include "core.h"

enum { tend, ttag, tnum };
#define tokc(x, y, z, ...) ((x) | (y) << 8 | (z) << 16)
#define tokd(...) tokc(__VA_ARGS__, 0, 0, 0)
#define tok1(x) ((x)+=1, tokd((x)[-1]))
#define tok2(x) ((x)+=2, tokd((x)[-2], (x)[-1]))
#define tok3(x) ((x)+=3, tokd((x)[-3], (x)[-2], (x)[-1]))

file* scan(char* name) {
  file* f;

  FILE* fp = fopen(name, "r");
  fseek(fp, 0, SEEK_END);
  unsigned int size = ftell(fp);
  rewind(fp);

  f = malloc(sizeof(file)+size);
  if(!f) return NULL;
  f->head = f->body = f->data;
  f->tail = f->head + size;
  f->size = size;
  f->line = 1;
  fread(f->head, 1, f->size, fp);
  fclose(fp);
  return f;
}

void shed(file* data) {
  free(data);
}

void chop(mode* self) {
  int64_t ival = 0;
  int64_t fval = 0;
  int     kind = tend;
  char*   c = self->code->body;

  pass: switch(*(self->code->body = c)) {
    case '\n': self->code->line++;
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
        //if(*c == '\n' || *c == '\0') fail("unterminated string %s", c);
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

  if(kind == '"' || kind == ttag)
    self->talk.hash = hget(self->pool, self->code->body, c - self->code->body);
  else
    self->talk.ival = kind == tnum? ival: 0;
  self->talk.kind = kind;
  self->code->body = c;
}

int same(mode* self, item* hash) {
    return (self->talk.kind == ttag && self->talk.hash == hash)? chop(self), 1 : 0;
}

int twin(mode* self, int   kind) {
    return (self->talk.kind == kind)? chop(self), 1 : 0;
}

int skip(mode* self, int   kind) {
    if(self->talk.kind != kind)
        fail("line %ld expected %x, got %x\n", self->code->line+1, kind, self->talk.kind);
    chop(self);
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

#define emit(self, call, ...) (self->arch.call(&self->data, __VA_ARGS__))
void expr(mode* self) {
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
    case tokd('*'): /* pointer deref */ break;
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
      case tokd('|'):      break;
      case tokd('^'):      break;
      case tokd('&'):      break;
      case tokd('<'):      break;
      case tokd('>'):      break;
      case tokd('+'):      break;
      case tokd('-'):      break;
      case tokd('*'):      break;
      case tokd('/'):      break;
      case tokd('%'):      break;
      case tokd('['):      break;
      case tokd('|', '|'): break;
      case tokd('&', '&'): break;
      case tokd('=', '='): break;
      case tokd('!', '='): break;
      case tokd('<', '='): break;
      case tokd('>', '='): break;
      case tokd('<', '<'): break;
      case tokd('>', '>'): break;
      case tokd('+', '+'): case tokd('-', '-'):  break;
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
  else if(same(self, sget(self->pool, "if"))) {
    skip(self, '('); expr(self); skip(self, ')');
    emit(self, when, 0);
    stmt(self);
    if(same(self, sget(self->pool, "else"))) {
      emit(self, jump, 1);
      stmt(self);
    }
    self->arch.back(&self->data);
  }
  else if(same(self, sget(self->pool, "while"))) {
    skip(self, '('); expr(self); skip(self, ')');
    emit(self, when, 1);
    emit(self, echo, puts, "foo");
    stmt(self);
  }
  else if(same(self, sget(self->pool, "return"))) {
    expr(self); skip(self, ';');
    emit(self, done, 1);
  }
  else {
    expr(self); skip(self, ';');
  }
}

void  show(void* data, size_t size) {
  uint8_t* buff = data;
  for(int i = 0; i < size; i++)
    printf("%2.2x ", buff[i]);
  printf("\n");
}

void  decl(mode* self) {
  // declaration begins with type or struct or union or enum
  // "typedef" type      name     ";"
  // "struct"  name? "{" decl "}" ";"
  // "union"   name? "{" decl "}" ";"
  // "enum"    name? "{" list "}" ";"

  item* type = self->talk.hash;
  if(((meta*)type->val)->kind != typedecl) return;
  chop(self);

  self->decl[self->top++] = (meta){.kind=function, .type=type};
  self->talk.hash->val = &self->decl[self->top-1];
  chop(self);

  // type name "(" args ")" "{" stmt "}"     // function decl
  if(twin(self, '(')) {
    // function
    while(!twin(self, ')')) {
      item* type = self->talk.hash;
      if(((meta*)type->val)->kind != typedecl) return;
      chop(self);

      self->decl[self->top++] = (meta){.kind=function, .type=type};
      self->talk.hash->val = &self->decl[self->top-1];
      chop(self);
      twin(self, ',');
    }

    stmt(self);
  }
  else
  if(twin(self, '[')) {
    // array
    self->decl[self->top-1].size = self->talk.ival;
    skip(self, ']');
  }

  //if(same(self, sget(self->pool, "struct"))
  //|| same(self, sget(self->pool, "union"))) {
  //  skip(self, '{');
  //  while(!twin(self, '}')) {
  //    rvar(self);
  //    skip(self, ';');
  //  }
  //  skip(self, '}');
  //  skip(self, ';');
  //}
  //if(same(self, sget(self->pool, "enum"))) {
  //  skip(self, '{');
  //  while(!twin(self, '}')) {
  //    rtok(self);
  //    skip(self, ',');
  //  }
  //  skip(self, '}');
  //  skip(self, ';');
  //}
  //
  //if(self->talk.hash.val.kind == type) {
  //  t = self->talk.hash;
  //  chop(self);
  //}
  //
  //mark* n = malloc(sizeof(mark));
  //n->base = t;
  //self->talk.hash->val = n;
}

void  make(mode* self) {
  // unit = function
  //      | variable
  //      | typedecl

  self->code = scan(self->name);
  self->data.head = self->data.tail = self->hold(0, 1024);

  self->decl[self->top++] = (meta){.kind=typedecl};
  sget(self->pool, "void")->val = &self->decl[self->top-1];
  self->decl[self->top++] = (meta){.kind=typedecl};
  sget(self->pool, "bool")->val = &self->decl[self->top-1];
  self->decl[self->top++] = (meta){.kind=typedecl};
  sget(self->pool, "char")->val = &self->decl[self->top-1];
  self->decl[self->top++] = (meta){.kind=typedecl};
  sget(self->pool, "short")->val = &self->decl[self->top-1];
  self->decl[self->top++] = (meta){.kind=typedecl};
  sget(self->pool, "int")->val = &self->decl[self->top-1];
  self->decl[self->top++] = (meta){.kind=typedecl};
  sget(self->pool, "long")->val = &self->decl[self->top-1];
  self->decl[self->top++] = (meta){.kind=typedecl};
  sget(self->pool, "float")->val = &self->decl[self->top-1];
  self->decl[self->top++] = (meta){.kind=typedecl};
  sget(self->pool, "double")->val = &self->decl[self->top-1];

  do {
    chop(self);
    decl(self);
  } while(self->talk.kind);

  shed(self->code);

  self->exec(self->data.head, self->data.tail - self->data.head);
  self->drop(self->data.head, 1024);
}

