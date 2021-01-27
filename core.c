// vim: ts=2:sw=2
#include "core.h"

struct file {
  char*  head;
  char*  body;
  char*  tail;
  size_t size;
  size_t line;
  //char*  name;
  char   data[1];
};

//#define tok1(x) (x[0])
//#define tok2(x) (x+=1, x[-1] | x[0]  << 8)
//#define tok3(x) (x+=2, x[-2] | x[-1] << 8 | x[0] << 16)
enum { tokend, toktag, toknum, tokstr };
#define tokc(x, y, z, ...) ((x) | (y) << 8 | (z) << 16)
#define tokd(...) tokc(__VA_ARGS__, 0, 0, 0)
#define tok1(x) ((x)+=1, tokd((x)[-1]))
#define tok2(x) ((x)+=2, tokd((x)[-2], (x)[-1]))
#define tok3(x) ((x)+=3, tokd((x)[-3], (x)[-2], (x)[-1]))

//typedef struct {
//  uint16_t align;
//
//  uint16_t _void    :1, _bool    :1, _char    :1,
//           _short   :2, _int     :2, _long    :2,
//           _float   :1, _double  :1;
//
//  uint16_t _const   :1;
//  uint16_t _signed  :2;
//  uint16_t _static  :1;
//  uint16_t _extern  :1;
//  uint16_t _inline  :1;
//  uint16_t _thread  :1;
//
//  uint16_t _typedef :1;
//  uint16_t _struct  :1;
//  uint16_t _union   :1;
//  uint16_t _enum    :1;
//} type;

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

#define term(...) head = (word){__VA_ARGS__}
//#define term(...) self->code->body = ++c; return (word){__VA_ARGS__}
word chop(mode* self) {
  char* tok;
  word  head = {};
  char* c = self->code->body;

  pass: switch(*c) {
    case '\n': self->code->line++;
    case ' ': case '\f': case '\t': case '\v': case '\r': c++; goto pass;
    case '#': case ',': case ';': case ':': case '?': case '~':
    case '(': case ')': case '[': case ']': case '{': case '}':
      term(tok1(c)); break;
    case '=': case '!': case '*': case '%': case '^':
      term(c[1] == '='? tok2(c): tok1(c)); break;
    case '&':
      term(c[1] == '=' || c[1] == '&'? tok2(c): tok1(c)); break;
    case '|':
      term(c[1] == '=' || c[1] == '|'? tok2(c): tok1(c)); break;
    case '+':
      term(c[1] == '=' || c[1] == '+'? tok2(c): tok1(c)); break;
    case '-':
      //kind = c[1] == '=' || c[1] == '-' || c[1] == '>'? tok2(c): tok1(c); break;
      term(c[1] == '=' || c[1] == '-' || c[1] == '>'? tok2(c): tok1(c)); break;
    case '<':
      term(c[1] == '<' && c[2] == '='? tok3(c):
           c[1] == '<' || c[1] == '='? tok2(c): tok1(c)); break;
    case '>':
      term(c[1] == '>' && c[2] == '='? tok3(c):
           c[1] == '>' || c[1] == '='? tok2(c): tok1(c)); break;
    case '/':
      if(c[1] == '/') // line comment
        while(*c != '\r' && *c != '\n') ++c;
      else if(c[1] == '*') // block comment
        while(c[0] != '*' && c[1] != '/') ++c; //XXX: *c will be /, should skip
      else term(c[1] == '='? tok2(c): tok1(c));
      break;
    //case '\'':
    //  term(tokstr); c+=2; break;
    case '"':
      tok = ++c;
      while(*c != '"') {
        //if(*c == '\n' || *c == '\0') fail("unterminated string %s", c);
        //if(*c == '\\') switch (*++c) {
        //  case 'a': '\a'; break;
        //  case 'b': '\b'; break;
        //  case 't': '\t'; break;
        //  case 'n': '\n'; break;
        //  case 'v': '\v'; break;
        //  case 'f': '\f'; break;
        //  case 'r': '\r'; break;
        //  default: break;
        //}
        ++c;
      }
      ++c;
      term(tokstr, .hash=hget(self->pool, tok, c - tok)); break;
    case '.':
      // '.', '...' or number
      if(c[1] == '.' && c[2] == '.') { term(tok3(c)); break; }
      else if (!(c[1] >= '0' && c[1] <= '9')) { term(tok1(c)); break; }
      // else fall trough to number
    case '0' ... '9': {
      int64_t ival = 0;
      int64_t fval = 0;
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
      term(toknum, .ival=ival);
      break;
    }
    default:
      tok = c;
      while((*c >= 'a' && *c <= 'z') || (*c >= 'A' && *c <= 'Z')
         || (*c >= '0' && *c <= '9') || *c == '_') c++;
      if(c - tok > 0) {
        term(toktag, .hash=hget(self->pool, tok, c - tok));
      }
      break;
  }
  self->code->body = c;
  self->talk = head;
  return head;
}

int same(mode* self, item* hash) {
    return (self->talk.kind == toktag && self->talk.hash == hash)? chop(self), 1 : 0;
}

int twin(mode* self, int   kind) {
    return (self->talk.kind == kind)? chop(self), 1 : 0;
}

int skip(mode* self, int   kind) {
    if(self->talk.kind != kind) fail("expected %x, got %x\n", kind, self->talk.kind);
    chop(self);
    return 1;
}

void type(mode* self) {
  // "typedef" type      name     ";"
  // "struct"  name? "{" decl "}" ";"
  // "union"   name? "{" decl "}" ";"
  // "enum"    name? "{" list "}" ";"

  //skip type tag
  printf("skipping %p ", self->talk.hash);
  fwrite(self->talk.hash->key, self->talk.hash->len, 1, stdout);
  printf("\n");
  chop(self);

  printf("skipping %p ", self->talk.hash);
  fwrite(self->talk.hash->key, self->talk.hash->len, 1, stdout);
  printf("\n");
  chop(self);
}

int args(mode* self) {
  if(!twin(self, '(')) return 0;
  while(!twin(self, ')')) {
    chop(self);
  }
  return 1;
}

void expr(mode* self) {
  int rank = 0xffff;

  switch(self->talk.kind) {
    case toknum:
      self->arch.move(&self->data, self->talk.ival);
      chop(self);
    break;
    case tokstr:
    break;
    case toktag:
    case tokd('('): /* type cast     */ skip(self, ')'); break;
    case tokd('*'): /* pointer deref */ break;
    case tokd('&'): /* address of    */ break;
    break;
    //case tokd('['):      break;
    default:
      printf("expr unexpected %x\n", self->talk.kind);
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

void stmt(mode* self) {
  if(twin(self, '{')) {
    while(!twin(self, '}')) stmt(self);
  }
  else if(same(self, sget(self->pool, "if"))) {
    skip(self, '('); expr(self); skip(self, ')');
    self->arch.when(&self->data, 0);
    stmt(self);
    if(same(self, sget(self->pool, "else"))) {
      self->arch.jump(&self->data, 1);
      stmt(self);
    }
    self->arch.back(&self->data);
  }
  else if(same(self, sget(self->pool, "while"))) {
    skip(self, '('); expr(self); skip(self, ')');
    self->arch.when(&self->data, 1);
    self->arch.call(&self->data, (uint32_t)"foobar\n");
    stmt(self);
  }
  else if(same(self, sget(self->pool, "return"))) {
    expr(self); skip(self, ';');
    self->arch.done(&self->data);
  }
  else {
    expr(self); skip(self, ';');
  }
}

void  show(void* data, size_t size) {
  uint8_t* buff = data;
  for( int i = 0; i < size; i++)
    printf("%2.2x ", buff[i]);
  printf("\n");
}

void make(mode* self) {
  // unit = function
  //      | variable
  //      | typedecl

  self->code = scan(self->name);
  self->data.head = self->data.tail = self->hold(0, 1024);

  //for(int i= 0; i < 100; i++) {
  //while(self->talk.kind) {
  //  if(self->talk.kind == toknum)        printf("token num %ld", self->talk.ival);
  //  else if(self->talk.kind == tokstr) { printf("token str %p ", self->talk.hash); fwrite(self->talk.hash->key, self->talk.hash->len, 1, stdout); }
  //  else if(self->talk.kind == toktag) { printf("token tag %p ", self->talk.hash); fwrite(self->talk.hash->key, self->talk.hash->len, 1, stdout); }
  //  else                                 printf("token %x %c", self->talk.kind, self->talk.kind);
  //  printf("\n");
  //  chop(self);
  //}

  do {
    chop(self);

    // every statement begins with type
    type(self);

    // type name "(" args ")" "{" stmt "}"     // function decl
    if(args(self)) {
      // function
      //printf("its a function \n");
      stmt(self);
    }

    // type name "[" size "]"              ";" // variable decl
    //if(post(&list, map)) {
    //}
    //else {
    //}
  } while(self->talk.kind);

  shed(self->code);

  self->exec(self->data.head, self->data.tail - self->data.head);
  self->drop(self->data.head, 1024);
}

