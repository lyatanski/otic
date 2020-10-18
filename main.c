// vim: ts=2:sw=2
#include <stdio.h>
#include <stdlib.h>     /* malloc, free, rand */

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

void toss(file* f)
{
    free(f);
}

word* chop(file* f)
{
  int line = 0;
  word  head = {};
  word* tail = &head;

  while(f->head != f->tail)
  {
    switch(*f->head)
    {
      case '\n': case '\r':
        // line breaks
        ++line;
        break;
      case ' ': case '\f': case '\t': case '\v':
        // skip spaces
        break;
      case '/':
        // division or line comment
        if (f->head[1] != '/'){ dict(&tail, tokdef("/")); break; }
        // skip the comment
        while (*f->head != '\r' && *f->head != '\n' && f->head != f->tail)
          ++f->head;
        break;
      case '[':
        dict(&tail, tokdef("["));
      case ']':
        dict(&tail, tokdef("]"));
      case '=':
        if (f->head[1] != '=') dict(&tail, tokdef("="));
        else dict(&tail, tokdef("=="));
        break;
      case '<':
        if(f->head[1] == '=') dict(&tail, tokdef("<="));
        else if(f->head[1] == '<') dict(&tail, tokdef("<<"));
        else dict(&tail, tokdef("<"));
        break;
      case '>':
        if(f->head[1] == '=') dict(&tail, tokdef(">="));
        else if(f->head[1] == '>') dict(&tail, tokdef(">>"));
        else dict(&tail, tokdef(">"));
        break;
      case '"':
        // literal strings
        break;
      case '.':
        // '.', '...', or number
        if(f->head[1] == '.' && f->head[2] == '.'){ dict(&tail, tokdef("...")); break; }
        else if (!(f->head[1] >= '0' && f->head[1] <= '9')){ dict(&tail, tokdef(".")); break; }
        // else fall trough to number
      case '0': case '1': case '2': case '3': case '4':
      case '5': case '6': case '7': case '8': case '9':
        break;
      default:
        // identifier
        break;
    }
    ++f->head;
  }
  return head.next;
}

void sift(word* list)
{
  // program = (typedef | function | global-variable)*
  while(list)
  {
    printf("%x\n", list->kind);
    list = list->next;
  }
}

int main(int argc, char** argv)
{
  if(argc != 2) return 1;
  file* code = read(argv[1]);
  word* list = chop(code);
  sift(list);
  //printf("%s\n", f->head);
  return 0;
}

