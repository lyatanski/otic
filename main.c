// vim: ts=2:sw=2
#include "core.h"

#ifndef part
#include "core.c"
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
#if defined(__APPLE__) && defined(__aarch64__)
#include <pthread.h>    /* for pthread_jit_write_protect_np */
#define JIT MAP_JIT     /* W^X: RWX pages must be MAP_JIT, toggled per thread */
#else
#define JIT 0
#endif

extern const emit ia64;
extern const emit arm8;
extern const form elfs;
#ifdef __aarch64__
#define base arm8
#else // __x86_64__
#define base ia64
#endif

file* scan(char* name) {
  FILE* fp = fopen(name, "r");
  if(!fp) fail("cannot open: %s\n", name);
  fseek(fp, 0, SEEK_END);
  size_t size = ftell(fp);
  rewind(fp);

  file* f = malloc(sizeof(file)+size);
  if(!f) return NULL;
  f->body = f->head;
  f->tail = f->head + size;
  f->head[size] = 0; // lexer stops at NUL
  if(fread(f->head, 1, size, fp) != size) fail("read error: %s\n", name);
  fclose(fp);
  return f;
}

void shed(file* data) {
  free(data);
}

void* hold(void* memory, size_t size) {
#ifdef _WIN32
  return VirtualAlloc(memory, size,
    MEM_COMMIT | MEM_RESERVE,
    PAGE_READWRITE);
#else
  memory = mmap(memory, size,
    PROT_READ | PROT_WRITE | PROT_EXEC,
    MAP_ANONYMOUS | MAP_PRIVATE | JIT, /*fd=*/-1, /*offset=*/0);
  if(memory == MAP_FAILED) fail("cannot map %zu bytes\n", size);
#if JIT
  pthread_jit_write_protect_np(0); // writable until exec
#endif
  return memory;
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
void  exec(mode* self) {
  //prep(self->text->head, 0);
#if JIT
  pthread_jit_write_protect_np(1);
#endif
  __builtin___clear_cache((char*)self->text->head, (char*)self->text->body);
  int return_code = ((func)self->main)();
  printf("%d\n", return_code);
}

void  show(mode* self) {
  for(byte* b = self->text->head; b < self->text->body; b++)
    printf("%.2x", *b);
  printf("\n");
}

void  save(mode* self) {
  file* out = self->form.pack(self);
  FILE* fp = fopen(self->outp, "wb");
  if(!fp) fail("cannot write: %s\n", self->outp);
  fwrite(out->head, 1, out->body - out->head, fp);
  if(ferror(fp) | fclose(fp)) fail("write error: %s\n", self->outp);
  shed(out);
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
  if(!scmp(para, "exec")) self->show = exec;
  else if(!scmp(para, "ia64")) self->arch = ia64;
  else if(!scmp(para, "arm8")) self->arch = arm8;
  else printf("architecture '%s' not supported\n", para);
}

static void wrap(mode* self, opts*, char* para) {
  if(!scmp(para, "elfs")) self->form = elfs;
  else printf("format '%s' not supported\n", para);
}

static void dest(mode* self, opts*, char* para) {
  self->outp = para;
  self->show = save;
}

static void apos(mode* self, opts*, char* para) {
  self->name = para;
  self->code = scan(self->name);
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
    .show = show,
    .arch = base,
    .form = elfs,
  };

  opts args[] = {
    {"-m", "--mode", .args = 1, .desc = "exec, ia64, arm8", .call = arch},
    {"-f", "--format", .args = 1, .desc = "elfs", .call = wrap},
    {"-o", "--output", .args = 1, .desc = "write static executable", .call = dest},
    {"-h", "--help", .desc = "display this help", .call = help}, {},
  };
  parse(&self, args, argc, argv, apos);
  if(!self.code) fail("no input file\n");

  make(&self);
  shed(self.code);

  return 0;
}

