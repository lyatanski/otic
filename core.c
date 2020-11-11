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
#define tokid  1
#define tokstr 2
#define toknum 3
#define tok1(x) (x[0])
#define tok2(x) (x+=1, x[-1] | x[0]  << 8)
#define tok3(x) (x+=2, x[-2] | x[-1] << 8 | x[0] << 16)
#define tokd(x) (* (int*)x"\0\0\0\0")
  int   kind;
  word* next;
  int   size;
  from  data;
};

struct node {
    int i;
};

typedef struct {
  unsigned short is_void     :2;
  unsigned short is_bool     :2;
  unsigned short is_char     :2;
  unsigned short is_short    :2;
  unsigned short is_int      :2;
  unsigned short is_long     :2;
  unsigned short is_float    :2;
  unsigned short is_double   :2;

  unsigned short align;

  unsigned int   is_typedef  :1;
  unsigned int   is_static   :1;
  unsigned int   is_extern   :1;
  unsigned int   is_inline   :1;
  unsigned int   is_thread   :1;

  unsigned int   is_struct   :1;
  unsigned int   is_union    :1;
  unsigned int   is_enum     :1;

  unsigned int   is_sig   :1;
  unsigned int   is_abs :1;

} type;

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

file* read(const char* name) {
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

void emit(const file* f) {
}

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
        if(c[0] == '0' && (c[2] == 'X' || c[2] == 'x'))
          while((*c >= '0' && *c <= '9')
             || (*c >= 'A' && *c <= 'F') || (*c >= 'a' && *c <= 'f')) ++c;
        if(c[0] == '0' && (c[2] == 'B' || c[2] == 'b'))
          while(*c == '0' || *c == '1') ++c;
        dict(&tail, toknum, hget(h, tok, c - tok));
        break;
      default:
        tok = c;
        while((*c >= 'a' && *c <= 'z') || (*c >= 'A' && *c <= 'Z')
           || (*c >= '0' && *c <= '9') || *c == '_') c++;
        if(c - tok > 0) {
          dict(&tail, tokid, hget(h, tok, c - tok));
          c--;
        }
        break;
    }
    ++c;
  }
  return head.next;
}

//void same(word** tok, int kind, from f) {
//    if((*tok)->kind != kind)
//      fail("expected %x, got %x", kind, (*tok)->kind);
//    (*tok) = (*tok)->next;
//}
//#define skip(p, k, ...) same(p, k, (from){__VA_ARGS__})

int twin(type* x, type* y) {
  return ( x->is_void   == y->is_void
        || x->is_bool   == y->is_bool
        || x->is_char   == y->is_char
        || x->is_short  == y->is_short
        || x->is_int    == y->is_int
        || x->is_long   == y->is_long
        || x->is_float  == y->is_float
        || x->is_double == y->is_double
        || x->is_sig    == y->is_sig
        || x->is_abs    == y->is_abs);
}

int know(word* tok, hash** map) {
  return 0;
}

type decl_spec(word* tok, hash** map) {
  type t = {};
  item* h_typedef  = sget(map, "typedef");
  item* h_static   = sget(map, "static");
  item* h_extern   = sget(map, "extern");
  item* h_inline   = sget(map, "inline");
  item* h_struct   = sget(map, "struct");
  item* h_union    = sget(map, "union");
  item* h_enum     = sget(map, "enum");
  item* h_signed   = sget(map, "signed");
  item* h_unsigned = sget(map, "unsigned");
  item* h_void     = sget(map, "void");
  item* h_bool     = sget(map, "_Bool");
  item* h_char     = sget(map, "char");
  item* h_short    = sget(map, "short");
  item* h_int      = sget(map, "int");
  item* h_long     = sget(map, "long");
  item* h_float    = sget(map, "float");
  item* h_double   = sget(map, "double");

  while(know(tok, map)) {
    if(tok->data.hash == h_typedef)   t.is_typedef  = 1;
    if(tok->data.hash == h_static)    t.is_static   = 1;
    if(tok->data.hash == h_extern)    t.is_extern   = 1;
    if(tok->data.hash == h_inline)    t.is_inline   = 1;

    if(tok->data.hash == h_struct)    t.is_struct   = 1;
    if(tok->data.hash == h_union)     t.is_union    = 1;
    if(tok->data.hash == h_enum)      t.is_enum     = 1;

    if(tok->data.hash == h_signed)    t.is_sig      = 1;
    if(tok->data.hash == h_unsigned)  t.is_abs      = 1;

    if(tok->data.hash == h_void)      t.is_void    += 1;
    if(tok->data.hash == h_bool)      t.is_bool    += 1;
    if(tok->data.hash == h_char)      t.is_char    += 1;
    if(tok->data.hash == h_short)     t.is_short   += 1;
    if(tok->data.hash == h_int)       t.is_int     += 1;
    if(tok->data.hash == h_long)      t.is_long    += 1;
    if(tok->data.hash == h_float)     t.is_float   += 1;
    if(tok->data.hash == h_double)    t.is_double  += 1;

    if(!(twin(&t, &(type){           .is_void=1            })
       ||twin(&t, &(type){           .is_bool=1            })
       ||twin(&t, &(type){.is_sig=1, .is_char=1            })
       ||twin(&t, &(type){.is_abs=1, .is_char=1            })
       ||twin(&t, &(type){           .is_short=1           })
       ||twin(&t, &(type){           .is_short=1, .is_int=1})
       ||twin(&t, &(type){.is_sig=1, .is_short=1           })
       ||twin(&t, &(type){.is_sig=1, .is_short=1, .is_int=1})
       ||twin(&t, &(type){.is_abs=1, .is_short=1           })
       ||twin(&t, &(type){.is_abs=1, .is_short=1, .is_int=1})
       ||twin(&t, &(type){                        .is_int=1})
       ||twin(&t, &(type){.is_sig=1                        })
       ||twin(&t, &(type){.is_sig=1,              .is_int=1})
       ||twin(&t, &(type){.is_abs=1                        })
       ||twin(&t, &(type){.is_abs=1,              .is_int=1})
       ||twin(&t, &(type){           .is_long=1            })
       ||twin(&t, &(type){           .is_long=1,  .is_int=1})
       ||twin(&t, &(type){           .is_long=2,  .is_int=1})
       ||twin(&t, &(type){.is_sig=1, .is_long=1            })
       ||twin(&t, &(type){.is_sig=1, .is_long=2            })
       ||twin(&t, &(type){.is_sig=1, .is_long=1,  .is_int=1})
       ||twin(&t, &(type){.is_sig=1, .is_long=2,  .is_int=1})
       ||twin(&t, &(type){.is_abs=1, .is_long=1            })
       ||twin(&t, &(type){.is_abs=1, .is_long=2            })
       ||twin(&t, &(type){.is_abs=1, .is_long=1,  .is_int=1})
       ||twin(&t, &(type){.is_abs=1, .is_long=2,  .is_int=1})
       ||twin(&t, &(type){           .is_float=1           })
       ||twin(&t, &(type){           .is_double=1          })
       )) fail("invalid type");
  }

  return t;
}

node* asgn(word** tok) {
    return NULL;
}

node* expr(word** tok) {
    node* n = asgn(tok);
    if((*tok)->kind == tokd(",")) return NULL;
    return n;
}

node* prim(word** tok) {
    node* n;
    if((*tok)->kind == tokd("(")) {
      n = expr(tok);
      //skip(tok, tokd(")"));
    }
    return n;
}

void sift(word* list, hash** map) {
  // unit = function | declaration
  // declaration = variable | type
  // (typedef/struct/union/enum are treated as meta types)
  while(list) {
    // every statement begins with type
    //type t = decl_spec(list, map);

    //if(t.is_typedef) {
    //  // type alias
    //}

    //if(list->next->kind == tokd("(")) {
    //  // function
    //}

    //if(list->next->kind == tokd(";")) {
    //  // variable
    //}

    if(list->kind < 4) printf("%x %s\n", list->kind, list->data.hash->key);
    else               printf("%x %c\n", list->kind, list->kind);
    list = list->next;
  }
  //printf("%x \n", tokd("->"));
}

