// vim: ts=2:sw=2
#include "core.h"

typedef struct file
{
  unsigned char* head;
  unsigned char* tail;
  unsigned int   size;
  unsigned char  name[512];
  unsigned char  data[1];
} file;

typedef union {
  unsigned long  hash;
  unsigned char* data;
} from;

typedef struct word word;
struct word {
#define tokid  1
#define tokstr 2
#define toknum 3
#define tok1(x) (*x)
#define tok2(x) (*x++ | *x   << 8)
#define tok3(x) (*x++ | *x++ << 8 | *x << 16)
#define tokd(x) (* (int*)x"\0\0\0\0")
  int   kind;
  word* next;
  int   size;
  from  data;
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

file* read(const char* name)
{
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
  return f;
}

void emit(const file* f)
{
}

void drop(file* f)
{
    free(f);
}

word* chop(file* f) {
  int   line = 0;
  unsigned char* tok;
  unsigned char* c = f->head;
  unsigned long hash = 0;
  word  head = {};
  word* tail = &head;

  unsigned char tmp;

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
        tok = c++;
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
        dict(&tail, tokstr);
        break;
      case '.':
        // '.', '...' or number
        if(c[1] == '.' && c[2] == '.') { dict(&tail, tok3(c)); break; }
        else if (!(c[1] >= '0' && c[1] <= '9')) { dict(&tail, tok1(c)); break; }
        // else fall trough to number
      case '0' ... '9':
        tok = c++;
        if(c[0] == '0' && (c[2] == 'X' || c[2] == 'x'))
          while(*c >= '0' && *c <= '9'
             || *c >= 'A' && *c <= 'F'
             || *c >= 'a' && *c <= 'f') ++c;
        if(c[0] == '0' && (c[2] == 'B' || c[2] == 'b'))
          while(*c == '0' || *c == '1') ++c;
        dict(&tail, toknum);
        break;
      default:
        tok = c;
        hash = 0;
        while(*c >= 'a' && *c <= 'z'
           || *c >= 'A' && *c <= 'Z'
           || *c >= '0' && *c <= '9'
           || *c == '_') hash = pile(hash, *c++);
        if(c - tok > 0)
        {
          dict(&tail, tokid, hash);
          tmp = *c;
          *c = '\0';
          //printf("%016lx %s\n", hash, tok);
          *c-- = tmp;
        }
        break;
    }
    ++c;
  }
  return head.next;
}

//int same(char* tok, char* exp)
//{
//  while(tok && exp && *tok && *exp && *tok++ == *exp++);
//  return *tok == *exp;
//}
//
//void skip(word** tok, char* exp)
//{
//    if(!same((*tok)->data, exp))
//      fail("expected %s, got %s", exp, (*tok)->data);
//    (*tok) = (*tok)->next;
//}

int twin(type* x, type* y)
{
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

int know(word* tok)
{
  return 0;
}

int decl_spec(word* tok)
{
  type t = {};

  while(know(tok))
  {
//    if(same(tok->data, "typedef"))   t.is_typedef  = 1;
//    if(same(tok->data, "static"))    t.is_static   = 1;
//    if(same(tok->data, "extern"))    t.is_extern   = 1;
//    if(same(tok->data, "inline"))    t.is_inline   = 1;
//
//    if(same(tok->data, "struct"))    t.is_struct   = 1;
//    if(same(tok->data, "union"))     t.is_union    = 1;
//    if(same(tok->data, "enum"))      t.is_enum     = 1;
//
//    if(same(tok->data, "signed"))    t.is_sig   = 1;
//    if(same(tok->data, "unsigned"))  t.is_abs = 1;
//
//    if(same(tok->data, "void"))      t.is_void    += 1;
//    if(same(tok->data, "_Bool"))     t.is_bool    += 1;
//    if(same(tok->data, "char"))      t.is_char    += 1;
//    if(same(tok->data, "short"))     t.is_short   += 1;
//    if(same(tok->data, "int"))       t.is_int     += 1;
//    if(same(tok->data, "long"))      t.is_long    += 1;
//    if(same(tok->data, "float"))     t.is_float   += 1;
//    if(same(tok->data, "double"))    t.is_double  += 1;

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

  return 0;
}

void sift(word* list)
{
  // unit = function | declaration
  // declaration = variable | type
  // (typedef/struct/union/enum are treated as meta types)
  while(list)
  {
    // every statement begins with type
    decl_spec(list);
    printf("%x %c\n", list->kind, list->kind);
    list = list->next;
  }
}

int main(int argc, char** argv)
{
  if(argc != 2) return 1;

  file* code = read(argv[1]);
  word* list = chop(code);
  sift(list);
  drop(code);

  return 0;
}

