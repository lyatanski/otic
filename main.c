// vim: ts=2:sw=2
#include "bare.h"

typedef struct file
{
  unsigned char* head;
  unsigned char* tail;
  unsigned int   size;
  unsigned char  name[512];
  unsigned char  data[1];
} file;

typedef struct word word;
struct word {
#define tokid  1
#define tokstr 2
#define toknum 3
#define tokdef(x) (* (int*)x"\0\0\0\0")
  int   kind;
  word* next;
  int   size;
  void* data;
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

  unsigned int   is_signed   :1;
  unsigned int   is_unsigned :1;

} type;

void fail(char* mesg, ...)
{
    va_list args;
    va_start(args, mesg);
    vfprintf(stderr, mesg, args);
    exit(1);
}

void dict(word** prev, int kind) {
  word* tok = malloc(sizeof(word));
  tok->kind = kind;
  tok->next = NULL;
  tok->size = 0;
  tok->data = NULL;
  (*prev)->next = tok;
  (*prev) = tok;
}

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
  word  head = {};
  word* tail = &head;

  while(f->head != f->tail)
  {
    switch(*f->head)
    {
      case '\n': case '\r':
        ++line;
        break;
      case ' ': case '\f': case '\t': case '\v': break;
      case '#': dict(&tail, tokdef("#")); break;
      case ',': dict(&tail, tokdef(",")); break;
      case ';': dict(&tail, tokdef(";")); break;
      case ':': dict(&tail, tokdef(":")); break;
      case '?': dict(&tail, tokdef("?")); break;
      case '~': dict(&tail, tokdef("~")); break;
      case '(': dict(&tail, tokdef("(")); break;
      case ')': dict(&tail, tokdef(")")); break;
      case '[': dict(&tail, tokdef("[")); break;
      case ']': dict(&tail, tokdef("]")); break;
      case '{': dict(&tail, tokdef("{")); break;
      case '}': dict(&tail, tokdef("}")); break;
      case '=':
        if (f->head[1] == '=') dict(&tail, tokdef("=="));
        else dict(&tail, tokdef("="));
        break;
      case '!':
        if (f->head[1] == '=') dict(&tail, tokdef("!="));
        else dict(&tail, tokdef("!"));
        break;
      case '*':
        if (f->head[1] == '=') dict(&tail, tokdef("*="));
        else dict(&tail, tokdef("*"));
        break;
      case '%':
        if (f->head[1] == '=') dict(&tail, tokdef("%="));
        else dict(&tail, tokdef("%"));
        break;
      case '^':
        if (f->head[1] == '=') dict(&tail, tokdef("^="));
        else dict(&tail, tokdef("^"));
        break;
      case '&':
        if(f->head[1] == '&') dict(&tail, tokdef("&&"));
        else if(f->head[1] == '=') dict(&tail, tokdef("&="));
        else dict(&tail, tokdef("&"));
        break;
      case '|':
        if(f->head[1] == '|') dict(&tail, tokdef("||"));
        else if(f->head[1] == '=') dict(&tail, tokdef("|="));
        else dict(&tail, tokdef("|"));
        break;
      case '+':
        if(f->head[1] == '+') dict(&tail, tokdef("++"));
        else if(f->head[1] == '=') dict(&tail, tokdef("+="));
        else dict(&tail, tokdef("+"));
        break;
      case '-':
        if(f->head[1] == '-') dict(&tail, tokdef("--"));
        else if(f->head[1] == '=') dict(&tail, tokdef("-="));
        else if(f->head[1] == '>') dict(&tail, tokdef("->"));
        else dict(&tail, tokdef("-"));
        break;
      case '<':
        if(f->head[1] == '<')
          if(f->head[2] == '=') dict(&tail, tokdef("<<="));
          else dict(&tail, tokdef("<<"));
        else if(f->head[1] == '=') dict(&tail, tokdef("<="));
        else dict(&tail, tokdef("<"));
        break;
      case '>':
        if(f->head[1] == '>')
          if(f->head[2] == '=') dict(&tail, tokdef(">>="));
          else dict(&tail, tokdef(">>"));
        else if(f->head[1] == '=') dict(&tail, tokdef(">="));
        else dict(&tail, tokdef(">"));
        break;
      case '/':
        // division or line comment
        if (f->head[1] == '=') dict(&tail, tokdef("/="));
        else if(f->head[1] == '/')
          // skip till end of line
          while (*f->head != '\r' && *f->head != '\n' && f->head != f->tail)
            ++f->head;
        else if(f->head[1] == '*')
          // skip till close comment
          while (f->head[0] != '*' && f->head[1] != '/' && f->head != f->tail)
            ++f->head;
        else dict(&tail, tokdef("/"));
        break;
      case '"':
        tok = f->head++;
        while(f->head && *f->head != '"') {
            ++f->head;
            if(*f->head == '\\' && *f->head == '"') ++f->head;
        }
        dict(&tail, tokstr);
        break;
      case '.':
        // '.', '...' or number
        if(f->head[1] == '.' && f->head[2] == '.'){ dict(&tail, tokdef("...")); break; }
        else if (!(f->head[1] >= '0' && f->head[1] <= '9')){ dict(&tail, tokdef(".")); break; }
        // else fall trough to number
      case '0' ... '9':
        tok = f->head++;
        while(*f->head >= '0' && *f->head <= '9') ++f->head;
        dict(&tail, toknum);
        break;
      default:
        tok = f->head++;
        while(*f->head >= 'a' && *f->head <= 'z'
           || *f->head >= 'A' && *f->head <= 'Z'
           || *f->head >= '0' && *f->head <= '9'
           || *f->head == '_') ++f->head;
        dict(&tail, tokid);
        --f->head;
        break;
    }
    ++f->head;
  }
  return head.next;
}

int same(word* tok, char* exp)
{
  return memcmp(tok->data, exp, tok->size) == 0;
}

void skip(word** tok, char* exp)
{
    if(!same(*tok, exp))
      fail("expected %s, got %s", exp, (*tok)->data);
    (*tok) = (*tok)->next;
}

int twin(type* x, type* y)
{
  return ( x->is_void     == y->is_void
        || x->is_bool     == y->is_bool
        || x->is_char     == y->is_char
        || x->is_short    == y->is_short
        || x->is_int      == y->is_int
        || x->is_long     == y->is_long
        || x->is_float    == y->is_float
        || x->is_double   == y->is_double
        || x->is_signed   == y->is_signed
        || x->is_unsigned == y->is_unsigned);
}

int know(word* tok)
{
  return 0;
}

int decl(word* tok)
{
  type t = {};

  while(know(tok))
  {
    if(same(tok, "typedef"))   t.is_typedef  = 1;
    if(same(tok, "static"))    t.is_static   = 1;
    if(same(tok, "extern"))    t.is_extern   = 1;
    if(same(tok, "inline"))    t.is_inline   = 1;

    if(same(tok, "struct"))    t.is_struct   = 1;
    if(same(tok, "union"))     t.is_union    = 1;
    if(same(tok, "enum"))      t.is_enum     = 1;

    if(same(tok, "signed"))    t.is_signed   = 1;
    if(same(tok, "unsigned"))  t.is_unsigned = 1;

    if(same(tok, "void"))      t.is_void    += 1;
    if(same(tok, "_Bool"))     t.is_bool    += 1;
    if(same(tok, "char"))      t.is_char    += 1;
    if(same(tok, "short"))     t.is_short   += 1;
    if(same(tok, "int"))       t.is_int     += 1;
    if(same(tok, "long"))      t.is_long    += 1;
    if(same(tok, "float"))     t.is_float   += 1;
    if(same(tok, "double"))    t.is_double  += 1;

    if(!(twin(&t, &(type){                .is_void=1            })
       ||twin(&t, &(type){                .is_bool=1            })
       ||twin(&t, &(type){.is_signed=1,   .is_char=1            })
       ||twin(&t, &(type){.is_unsigned=1, .is_char=1            })
       ||twin(&t, &(type){                .is_short=1           })
       ||twin(&t, &(type){                .is_short=1, .is_int=1})
       ||twin(&t, &(type){.is_signed=1,   .is_short=1           })
       ||twin(&t, &(type){.is_signed=1,   .is_short=1, .is_int=1})
       ||twin(&t, &(type){.is_unsigned=1, .is_short=1           })
       ||twin(&t, &(type){.is_unsigned=1, .is_short=1, .is_int=1})
       ||twin(&t, &(type){                             .is_int=1})
       ||twin(&t, &(type){.is_signed=1                          })
       ||twin(&t, &(type){.is_signed=1,                .is_int=1})
       ||twin(&t, &(type){.is_unsigned=1                        })
       ||twin(&t, &(type){.is_unsigned=1,              .is_int=1})
       ||twin(&t, &(type){                .is_long=1            })
       ||twin(&t, &(type){                .is_long=1,  .is_int=1})
       ||twin(&t, &(type){                .is_long=2,  .is_int=1})
       ||twin(&t, &(type){.is_signed=1,   .is_long=1            })
       ||twin(&t, &(type){.is_signed=1,   .is_long=2            })
       ||twin(&t, &(type){.is_signed=1,   .is_long=1,  .is_int=1})
       ||twin(&t, &(type){.is_signed=1,   .is_long=2,  .is_int=1})
       ||twin(&t, &(type){.is_unsigned=1, .is_long=1            })
       ||twin(&t, &(type){.is_unsigned=1, .is_long=2            })
       ||twin(&t, &(type){.is_unsigned=1, .is_long=1,  .is_int=1})
       ||twin(&t, &(type){.is_unsigned=1, .is_long=2,  .is_int=1})
       ||twin(&t, &(type){.is_float=1                           })
       ||twin(&t, &(type){.is_double=1                          })
       )) fail("invalid type");
  }

  return 0;
}

void sift(word* list)
{
  // type declaration
  // function declaration/definition
  // global variable
  // each beggining with type 
  // (typedef/struct/union/enum are treated as meta types)
  while(list)
  {
    // every statement begins with type
    decl(list);
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
  //drop(code);
  //printf("%s\n", f->head);
  return 0;
}

