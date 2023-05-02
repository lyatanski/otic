// vim: ts=2:sw=2
#include "core.h"

#ifndef part
#include "core.c"
#include "hash.c"

#include "ia64.c"
#include "arm8.c"

#include "elfs.c"
//#include "pe32.c"
//#include "mach.c"
#endif

#ifdef _WIN32
#include <memoryapi.h>  /* for VirtualAlloc */
#else
#include <sys/mman.h>   /* for mmap         */
#endif

extern const emit ia64;
extern const emit arm8;
#ifdef __aarch64__
#define base arm8
#else // __x86_64__
#define base ia64
#endif

void* hold(void* memory, size_t size) {
#ifdef _WIN32
  return VirtualAlloc(0, size,
    MEM_COMMIT | MEM_RESERVE,
    PAGE_READWRITE);
#else
  return mmap(/*addr=*/0, size,
    PROT_READ | PROT_WRITE | PROT_EXEC,
    MAP_ANONYMOUS | MAP_PRIVATE, /*fd=*/-1, /*offset=*/0);
#endif
  return NULL;
}

void prep(void* memory, size_t size) {
#ifdef _WIN32
  VirtualProtect(memory, size, PAGE_EXECUTE_READ, 0);
#else
  mprotect(memory, size, PROT_EXEC);
#endif
}

void* drop(void* memory, size_t size) {
#ifdef _WIN32
  VirtualFree(memory, 0, MEM_RELEASE);
#else
  munmap(memory, size);
#endif
  return NULL;
}

typedef int (*func)();
void  exec(file* data) {
  //prep(data->head, 0);
  int return_code = ((func)data->head)();
  printf("%d\n", return_code);
}

void  show(file* data) {
  for(byte* b = data->head; b < data->body; b++)
    printf("%.2x", *b);
  printf("\n");
}

typedef struct opts opts;
typedef void (*move)(mode*, opts*, char*);
struct opts {
  char* name;
  char* full;
  char* desc;
  move  call;
  byte  need:1,
        args:1;
};

static void help(mode*, opts* self, char*) {
  printf("usage: %s [options] file [file...]\n", "tic");
  printf("options:\n");
  while(self->name) {
    printf("  %s", self->name);
    if(self->full) printf(", %s", self->full);
    else printf("\t");
    printf("\t%s\n", self->desc);
    self++;
  }
  exit(0);
}

int  scmp(char* x, char* y) {
  while(x && y && *x && *y && *x==*y) {x++; y++;}
  return (!x || !y)? 1: *x - *y;
}

static void arch(mode* self, opts*, char* para) {
  if(!scmp(para, "exec")) self->exec = exec;
  else if(!scmp(para, "ia64")) self->arch = ia64;
  else if(!scmp(para, "arm8")) self->arch = arm8;
  else printf("architecture '%s' not supported\n", para);
}

static void apos(mode* self, opts*, char* para) {
  self->name = para;
}

static void parse(mode* self, opts* opt, int argc, char** argv, move call) {
  for(int i = 1; i < argc; i++) {
    bool match = 0;
    for(opts* back = opt; back->name; back++) {
      if(!scmp(argv[i], back->name) || !scmp(argv[i], back->full)) {
        match = 1;
        if(back->call) {
          if(back->args) back->call(self, opt, argv[++i]);
          else back->call(self, opt, NULL);
        }
      }
    }
    if(!match) call(self, opt, argv[i]);
  }
}

int   main(int argc, char** argv) {
  mode self = {
    .hold = hold,
    .drop = drop,
    .exec = show,
    .arch = base,
  };

  opts args[] = {
    {"-m", "--mode", .args = 1, .desc = "exec, ia64, arm8", .call = arch},
    {"-h", "--help", .desc = "display this help", .call = help}, {},
  };
  parse(&self, args, argc, argv, apos);

  make(&self);

  //self.name = "bins/ia64.o";
  //elfs(&self);

  return 0;
}

