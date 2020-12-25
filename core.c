// vim: ts=2:sw=2
#include "core.h"

struct file {
  char*          head;
  char*          tail;
  unsigned int   size;
  char           name[512];
  char           data[1];
};

typedef union {
  item* hash;
} from;

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
  from  data;
};

//typedef struct node node;
//struct node {
//  spec  kind;
//  node* cond;
//  node* then;
//  node* altr;
//};

//typedef struct {
//  unsigned short is_void     :2;
//  unsigned short is_bool     :2;
//  unsigned short is_char     :2;
//  unsigned short is_short    :2;
//  unsigned short is_int      :2;
//  unsigned short is_long     :2;
//  unsigned short is_float    :2;
//  unsigned short is_double   :2;
//
//  unsigned short align;
//
//  unsigned int   is_typedef  :1;
//  unsigned int   is_static   :1;
//  unsigned int   is_extern   :1;
//  unsigned int   is_inline   :1;
//  unsigned int   is_thread   :1;
//
//  unsigned int   is_struct   :1;
//  unsigned int   is_union    :1;
//  unsigned int   is_enum     :1;
//
//  unsigned int   is_sig   :1;
//  unsigned int   is_abs :1;
//
//} type;

void  init(mode* self) {
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
void term(word** prev, int kind, from data) {
  word* tok = malloc(sizeof(word));
  tok->kind = kind;
  tok->next = NULL;
  tok->size = 0;
  tok->data = data;
  (*prev)->next = tok;
  (*prev) = tok;
}
#define dict(p, k, ...) term(p, k, (from){__VA_ARGS__})

//node* grow(spec kind) {
//  node* nd = malloc(sizeof(node));
//  nd->kind = kind;
//  nd->cond = NULL;
//  nd->then = NULL;
//  nd->altr = NULL;
//  return nd;
//}

//void emit(file* f) {
//  FILE* fp = fopen(f->name, "wb");
//  fwrite(f->head, 1, f->size, fp);
//  fclose(fp);
//}

void drop(file* f) {
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
      case '0' ... '9':
        tok = c++;
        //if(c[0] == '0' && (c[2] == 'X' || c[2] == 'x'))
        //  while((*c >= '0' && *c <= '9')
        //     || (*c >= 'A' && *c <= 'F') || (*c >= 'a' && *c <= 'f')) ++c;
        //if(c[0] == '0' && (c[2] == 'B' || c[2] == 'b'))
        //  while(*c == '0' || *c == '1') ++c;
        while(*c >= '0' && *c <= '9') ++c;
        dict(&tail, toknum, hget(h, tok, c - tok));
        c--;
        break;
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

void expr(word** tok, hash** map) {
    int rank = 0xffff;

    switch((*tok)->kind) {
      case toknum:
        printf("expr %d\n", (*tok)->data.hash->key[0]-'0');
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

void stmt(word** tok, hash** map) {
  printf("stmt ");
  if(twin(tok, tokd('{'))) {
    printf("span\n");
    while(!twin(tok, tokd('}')))
      stmt(tok, map);
  }
  else if(same(tok, sget(map, "if"))) {
    printf("when  ");
    skip(tok, tokd('('));
    expr(tok, map);
    skip(tok, tokd(')'));
    stmt(tok, map);
    if(same(tok, sget(map, "else")))
      stmt(tok, map);
  }
  else if(same(tok, sget(map, "while"))) {
    printf("loop\n");
    skip(tok, tokd('('));
    expr(tok, map);
    skip(tok, tokd(')'));
    stmt(tok, map);
  }
  else if(same(tok, sget(map, "return"))) {
    printf("done\n");
    expr(tok, map);
    skip(tok, tokd(';'));
  }
  else {
    printf("expr\n");
    expr(tok, map);
    skip(tok, tokd(';'));
  }
}

int post(word** tok, hash** map) {
  return 0;
}

void make(mode* self) {
  // unit = function
  //      | variable
  //      | typedecl
  word* list = self->list;
  hash** map = &(self->pile);

  while(list) {
    // every statement begins with type
    type(&list, map);

    // type name "(" args ")" "{" stmt "}"     // function decl
    if(args(&list, map)) {
      // function
      printf("its a function \n");
      stmt(&list, map);
    }

    // type name "[" size "]"              ";" // variable decl
    //if(post(&list, map)) {
    //}
    //else {
    //}
  }

  self->arch.show();
}

