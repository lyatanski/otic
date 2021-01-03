// vim: ts=2:sw=2
#include "core.h"

typedef struct {
  char*        head;
  char*        tail;
  unsigned int size;
  char         name[512];
  char         data[1];
} file;

struct word {
#define toktag 1
#define toknum 2
#define tokstr 3
#define tok1(x) (x[0])
#define tok2(x) (x+=1, x[-1] | x[0]  << 8)
#define tok3(x) (x+=2, x[-2] | x[-1] << 8 | x[0] << 16)
#define toki(x, y, z, ...) (x | y << 8 | z << 16)
#define tokd(...) toki(__VA_ARGS__, 0, 0, 0)
  int   kind;
  word* next;
  int   size;
  form  data;
};

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

node* grow(mode* self, spec kind, form data) {
  if(self->tail) {
    self->tail->next = malloc(sizeof(node));
    self->tail = self->tail->next;
  }
  else self->tail = malloc(sizeof(node));
  self->tail->kind = kind;
  self->tail->next = NULL;
  self->tail->data = data;
  return self->tail;
}

void  init(mode* self) {
  self->tail = NULL;
  self->head = grow(self, unit, (form){});
  hnew(&self->pile);
}

file* scan(char* name) {
  file* f;

  FILE* fp = fopen(name, "r");
  fseek(fp, 0, SEEK_END);
  unsigned int size = ftell(fp);
  rewind(fp);

  f = malloc(sizeof(file)+size);
  if(!f) return NULL;
  f->head = f->data;
  f->tail = f->head + size;
  f->size = size;
  fread(f->head, 1, f->size, fp);
  fclose(fp);
  return f;
}
void term(word** prev, int kind, form data) {
  word* tok = malloc(sizeof(word));
  tok->kind = kind;
  tok->next = NULL;
  tok->size = 0;
  tok->data = data;
  (*prev)->next = tok;
  (*prev) = tok;
}
#define dict(p, k, ...) term(p, k, (form){__VA_ARGS__})

//void emit(file* f) {
//  FILE* fp = fopen(f->name, "wb");
//  fwrite(f->head, 1, f->size, fp);
//  fclose(fp);
//}

void shed(file* f) {
    free(f);
}

word* chop(file* f, hash** h) {
  int   line = 0;
  char* tok;
  char* c = f->head;
  word  head = {};
  word* tail = &head;

  while(c != f->tail) {
    switch(*c) {
      case '\n': case '\r': ++line; break;
      case ' ': case '\f': case '\t': case '\v': break;
      case '#': case ',': case ';': case ':': case '?': case '~':
      case '(': case ')': case '[': case ']': case '{': case '}':
        dict(&tail, tok1(c)); break;
      case '=': case '!': case '*': case '%': case '^':
        dict(&tail, c[1] == '=' ? tok2(c) : tok1(c)); break;
      case '&':
        dict(&tail, c[1] == '=' || c[1] == '&' ? tok2(c) : tok1(c)); break;
      case '|':
        dict(&tail, c[1] == '=' || c[1] == '|' ? tok2(c) : tok1(c)); break;
      case '+':
        dict(&tail, c[1] == '=' || c[1] == '+' ? tok2(c) : tok1(c)); break;
      case '-':
        dict(&tail, c[1] == '=' || c[1] == '-' || c[1] == '>' ? tok2(c)
                                                              : tok1(c)); break;
      case '<':
        dict(&tail, c[1] == '<' && c[2] == '=' ? tok3(c) :
                    c[1] == '<' || c[1] == '=' ? tok2(c) : tok1(c)); break;
      case '>':
        dict(&tail, c[1] == '>' && c[2] == '=' ? tok3(c) :
                    c[1] == '>' || c[1] == '=' ? tok2(c) : tok1(c)); break;
      case '/':
        if(c[1] == '/') // line comment
          while (*c != '\r' && *c != '\n' && c != f->tail) ++c;
        else if(c[1] == '*') // block comment
          while (c[0] != '*' && c[1] != '/' && c != f->tail) ++c;
        else dict(&tail, c[1] == '=' ? tok2(c) : tok1(c));
        break;
      //case '\'':
      //  dict(&tail, tokstr); c+=2; break;
      case '"':
        tok = ++c;
        while(*c != '"' && *c != '\0') {
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
        dict(&tail, tokstr, hget(h, tok, c - tok));
        break;
      case '.':
        // '.', '...' or number
        if(c[1] == '.' && c[2] == '.') { dict(&tail, tok3(c)); break; }
        else if (!(c[1] >= '0' && c[1] <= '9')) { dict(&tail, tok1(c)); break; }
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
            switch(*c) {
              case '0' ... '9': ival = ival * 16 + *c - '0';      continue;
              case 'A' ... 'F': ival = ival * 16 + *c - 'A' + 10; continue;
              case 'a' ... 'f': ival = ival * 16 + *c - 'a' + 10; continue;
            }
            break;
          }
        dict(&tail, toknum, .ival=ival);
        c--;
        break;
      }
      default:
        tok = c;
        while((*c >= 'a' && *c <= 'z') || (*c >= 'A' && *c <= 'Z')
           || (*c >= '0' && *c <= '9') || *c == '_') c++;
        if(c - tok > 0) {
          dict(&tail, toktag, hget(h, tok, c - tok));
          c--;
        }
        break;
    }
    ++c;
  }
  return head.next;
}

int same(word** tok, item* hash) {
    //printf("compare %p %d %s == %p %d %s\n", (*tok)->data.hash, (*tok)->data.hash->len, (*tok)->data.hash->key, hash, hash->len, hash->key);
    return ((*tok)->kind == toktag && (*tok)->data.hash == hash)? (*tok) = (*tok)->next, 1 : 0;
}

int twin(word** tok, int   kind) {
    return ((*tok)->kind == kind)? (*tok) = (*tok)->next, 1 : 0;
 }

int skip(word** tok, int   kind) {
    if((*tok)->kind != kind) fail("expected %x, got %x %s\n", kind, (*tok)->kind, (*tok)->kind == 1? (*tok)->data.hash->key: "");
    (*tok) = (*tok)->next;
    return 1;
}

int know(word** tok, hash** map) {
  return 1;
}

void type(word** tok, hash** map) {
  // "typedef" type                  name     ";"
  // "struct"  name?             "{" decl "}" ";"
  // "union"   name?             "{" decl "}" ";"
  // "enum"    name?             "{" list "}" ";"

  word* list = *tok;

  //skip type tag
  if(list->kind < 4) printf("skipping type %x %s\n", list->kind, list->data.hash->key);
  else               printf("skipping type %x %c\n", list->kind, list->kind);
  *tok = list = list->next;

  if(list->kind < 4) printf("skipping name %x %s\n", list->kind, list->data.hash->key);
  else               printf("skipping name %x %c\n", list->kind, list->kind);
  *tok = list = list->next;

  //item* h_typedef  = sget(map, "typedef");
  //item* h_static   = sget(map, "static");
  //item* h_extern   = sget(map, "extern");
  //item* h_inline   = sget(map, "inline");
  //item* h_struct   = sget(map, "struct");
  //item* h_union    = sget(map, "union");
  //item* h_enum     = sget(map, "enum");
  //item* h_signed   = sget(map, "signed");
  //item* h_unsigned = sget(map, "unsigned");
  //item* h_void     = sget(map, "void");
  //item* h_bool     = sget(map, "_Bool");
  //item* h_char     = sget(map, "char");
  //item* h_short    = sget(map, "short");
  //item* h_int      = sget(map, "int");
  //item* h_long     = sget(map, "long");
  //item* h_float    = sget(map, "float");
  //item* h_double   = sget(map, "double");

  //while(know(tok, map)) {
  //  if((*tok)->data.hash == h_typedef)   t.is_typedef  = 1;
  //  if((*tok)->data.hash == h_static)    t.is_static   = 1;
  //  if((*tok)->data.hash == h_extern)    t.is_extern   = 1;
  //  if((*tok)->data.hash == h_inline)    t.is_inline   = 1;

  //  if((*tok)->data.hash == h_struct)    t.is_struct   = 1;
  //  if((*tok)->data.hash == h_union)     t.is_union    = 1;
  //  if((*tok)->data.hash == h_enum)      t.is_enum     = 1;

  //  if((*tok)->data.hash == h_signed)    t.is_sig      = 1;
  //  if((*tok)->data.hash == h_unsigned)  t.is_abs      = 1;

  //  if((*tok)->data.hash == h_void)      t.is_void    += 1;
  //  if((*tok)->data.hash == h_bool)      t.is_bool    += 1;
  //  if((*tok)->data.hash == h_char)      t.is_char    += 1;
  //  if((*tok)->data.hash == h_short)     t.is_short   += 1;
  //  if((*tok)->data.hash == h_int)       t.is_int     += 1;
  //  if((*tok)->data.hash == h_long)      t.is_long    += 1;
  //  if((*tok)->data.hash == h_float)     t.is_float   += 1;
  //  if((*tok)->data.hash == h_double)    t.is_double  += 1;

  //  //if(!(twin(&t, &(type){           .is_void=1            })
  //  //   ||twin(&t, &(type){           .is_bool=1            })
  //  //   ||twin(&t, &(type){.is_sig=1, .is_char=1            })
  //  //   ||twin(&t, &(type){.is_abs=1, .is_char=1            })
  //  //   ||twin(&t, &(type){           .is_short=1           })
  //  //   ||twin(&t, &(type){           .is_short=1, .is_int=1})
  //  //   ||twin(&t, &(type){.is_sig=1, .is_short=1           })
  //  //   ||twin(&t, &(type){.is_sig=1, .is_short=1, .is_int=1})
  //  //   ||twin(&t, &(type){.is_abs=1, .is_short=1           })
  //  //   ||twin(&t, &(type){.is_abs=1, .is_short=1, .is_int=1})
  //  //   ||twin(&t, &(type){                        .is_int=1})
  //  //   ||twin(&t, &(type){.is_sig=1                        })
  //  //   ||twin(&t, &(type){.is_sig=1,              .is_int=1})
  //  //   ||twin(&t, &(type){.is_abs=1                        })
  //  //   ||twin(&t, &(type){.is_abs=1,              .is_int=1})
  //  //   ||twin(&t, &(type){           .is_long=1            })
  //  //   ||twin(&t, &(type){           .is_long=1,  .is_int=1})
  //  //   ||twin(&t, &(type){           .is_long=2,  .is_int=1})
  //  //   ||twin(&t, &(type){.is_sig=1, .is_long=1            })
  //  //   ||twin(&t, &(type){.is_sig=1, .is_long=2            })
  //  //   ||twin(&t, &(type){.is_sig=1, .is_long=1,  .is_int=1})
  //  //   ||twin(&t, &(type){.is_sig=1, .is_long=2,  .is_int=1})
  //  //   ||twin(&t, &(type){.is_abs=1, .is_long=1            })
  //  //   ||twin(&t, &(type){.is_abs=1, .is_long=2            })
  //  //   ||twin(&t, &(type){.is_abs=1, .is_long=1,  .is_int=1})
  //  //   ||twin(&t, &(type){.is_abs=1, .is_long=2,  .is_int=1})
  //  //   ||twin(&t, &(type){           .is_float=1           })
  //  //   ||twin(&t, &(type){           .is_double=1          })
  //  //   )) fail("invalid type");
  //  *tok = (*tok)->next;
  //  break;
  //}
}

int args(word** tok, hash** map) {
  word* p = *tok;
  if(!skip(tok, tokd('('))) return 0;
  while(!skip(tok, tokd(')'))) {
    p = p->next;
  }
  return 1;
}

void expr(mode* self, word** tok, hash** map) {
    int rank = 0xffff;

    switch((*tok)->kind) {
      case toknum:
        grow(self, _val, (*tok)->data);
        *tok = (*tok)->next;
      break;
      case tokstr:
      break;
      case toktag:
      case tokd('('): /* type cast     */ skip(tok, tokd(')')); break;
      case tokd('*'): /* pointer deref */ break;
      case tokd('&'): /* address of    */ break;
      break;
      //case tokd('['):      break;
      default:
        printf("expr unexpected %x %s\n", (*tok)->kind, (*tok)->kind == 1? (*tok)->data.hash->key: "");
    }

    while((*tok)->kind >= rank) {
      switch((*tok)->kind) {
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
          while(skip(tok, tokd(')'))) {
            //expr(Assign);
          }
          break;
        default: fail("error %x", (*tok)->kind); break;
      }
    }
}

void stmt(mode* self, word** tok, hash** map) {
  if(twin(tok, tokd('{'))) {
    while(!twin(tok, tokd('}')))
      stmt(self, tok, map);
  }
  else if(same(tok, sget(map, "if"))) {
    node * nd1 = grow(self, jump, (*tok)->data);
    grow(self, when, (*tok)->data);
    skip(tok, tokd('('));
    expr(self, tok, map);
    skip(tok, tokd(')'));
    stmt(self, tok, map);
    if(same(tok, sget(map, "else"))) {
      node * nd2 = grow(self, jump, (*tok)->data);
      nd1->then = self->tail;
      stmt(self, tok, map);
      nd2->then = self->tail;
    }
    else
      nd1->then = self->tail;
  }
  else if(same(tok, sget(map, "while"))) {
    printf("loop\n");
    skip(tok, tokd('('));
    expr(self, tok, map);
    skip(tok, tokd(')'));
    stmt(self, tok, map);
  }
  else if(same(tok, sget(map, "return"))) {
    grow(self, give, (*tok)->data);
    expr(self, tok, map);
    skip(tok, tokd(';'));
  }
  else {
    printf("expr\n");
    expr(self, tok, map);
    skip(tok, tokd(';'));
  }
}

int post(word** tok, hash** map) {
  return 0;
}

char debug[ops][5] = { "unit",
  "_val", "_add", "_sub", "_mul", "_div", "_mod",
  "when", "jump", "call", "proc", "give",
};
void make(mode* self) {
  // unit = function
  //      | variable
  //      | typedecl

  file* code = scan(self->name);
  self->list = chop(code, &self->pile);

  word* list = self->list;
  hash** map = &(self->pile);

  //while(list) {
  //  if(list->kind == toknum)      printf("token num %ld\n", list->data.ival);
  //  else if(list->kind == tokstr) printf("token str %s\n", list->data.hash->key);
  //  else if(list->kind == toktag) printf("token tag %s\n", list->data.hash->key);
  //  else                          printf("token %x %c\n", list->kind, list->kind);
  //  list = list->next;
  //}

  while(list) {
    // every statement begins with type
    type(&list, map);

    // type name "(" args ")" "{" stmt "}"     // function decl
    if(args(&list, map)) {
      // function
      grow(self, proc, list->data);
      //printf("its a function \n");
      stmt(self, &list, map);
    }

    // type name "[" size "]"              ";" // variable decl
    //if(post(&list, map)) {
    //}
    //else {
    //}
  }

  node* it = self->head;
  while(it) {
    printf(" %p %s", it, debug[it->kind]);
    //if(it->kind == proc) printf(" %s", it->data.hash->key);
    if(it->kind == _val) printf(" %ld", it->data.ival);
    if(it->kind == jump) printf(" %p", it->then->next);
    printf("\n");
    it = it->next;
  }

  self->arch.show();

  shed(code);
}

