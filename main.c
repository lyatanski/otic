// vim: ts=2:sw=2
#include "core.h"

#ifdef _WIN32
#include <memoryapi.h>  /* for VirtualAlloc */
#else
#include <sys/mman.h>   /* for mmap         */
#endif

// after the system headers: macros of the parts (arm8.c X) clash with them
#ifndef part
#include "core.c"
#include "link.c"
#include "ia64.c"
#include "arm8.c"
#include "elfs.c"
#include "ario.c"
#include "pe32.c"
#include "mach.c"
#endif

extern const emit ia64;
extern const emit arm8;
extern const form elfs;
extern const form mach;
extern const form pe32;
#ifdef __aarch64__
#define base arm8
#else // __x86_64__
#define base ia64
#endif
#ifdef __APPLE__
#define host mach
#elif defined(_WIN32)
#define host pe32
#else
#define host elfs
#endif

file* look(char* name) {
  FILE* fp = fopen(name, "rb");
  if(!fp) return NULL;
  fseek(fp, 0, SEEK_END);
  size_t size = ftell(fp);
  rewind(fp);

  file* f = malloc(sizeof(file)+size);
  if(!f) fail("out of memory\n");
  f->body = f->head;
  f->tail = f->head + size;
  f->head[size] = 0; // lexer stops at NUL
  if(fread(f->head, 1, size, fp) != size) fail("read error: %s\n", name);
  fclose(fp);
  return f;
}

file* scan(char* name) {
  file* f = look(name);
  if(!f) fail("cannot open: %s\n", name);
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
    PROT_READ | PROT_WRITE,
    MAP_ANONYMOUS | MAP_PRIVATE, /*fd=*/-1, /*offset=*/0);
  if(memory == MAP_FAILED) fail("cannot map %zu bytes\n", size);
  return memory;
#endif
  return NULL;
}

// W^X: the pages held writable turn executable, and no longer writable
void prep(void* memory, size_t size) {
#ifdef _WIN32
  DWORD old;
  if(!VirtualProtect(memory, size, PAGE_EXECUTE_READ, &old))
#else
  if(mprotect(memory, size, PROT_READ | PROT_EXEC))
#endif
    fail("cannot protect %zu bytes\n", size);
  __builtin___clear_cache((char*)memory, (char*)memory + size);
}

void* drop(void* memory, size_t size) {
#ifdef _WIN32
  VirtualFree(memory, 0, MEM_RELEASE);
#else
  munmap(memory, size);
#endif
  return NULL;
}

#if defined(_WIN32) && defined(__x86_64__)
typedef int (__attribute__((sysv_abi)) *func)(); // generated code clobbers rdi, rsi
#else
typedef int (*func)();
#endif
// the image: text, then data and bss on pages of their own
void  exec(mode* self) {
  if(self->impn) fail("cannot run a program that calls a library, write it with -o\n");
  if(self->boot != self->main) fail("cannot run a program with a C library, write it with -o\n");
  u64i  code = self->text->body - self->text->head;
  u64i  at   = (code + 0x3fff) & ~0x3fff; // 16 KB: pages on any host
  u64i  size = self->data->body - self->data->head;
  u64i  all  = at + ((size + 15) & ~15) + self->zero;
  byte* run  = self->hold(0, all);
  dock(self, at);
  copy(run, self->text->head, code);
  copy(run + at, self->data->head, size);
  if(at) prep(run, at); // text only: data and bss stay writable
  int return_code = ((func)(run + (self->main - self->text->head)))();
  printf("%d\n", return_code);
}

void  save(mode* self) {
  if(self->impn) fail("cannot call a library from this form yet\n"); // steps 12b-12d
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
  printf("usage: %s [options] file [obj.o...] [lib.a...]\n", "tic");
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
  if(!scmp(para, "ia64")) self->arch = ia64;
  else if(!scmp(para, "arm8")) self->arch = arm8;
  else printf("architecture '%s' not supported\n", para);
}

static void wrap(mode* self, opts*, char* para) {
  if(!scmp(para, "elfs")) self->form = elfs;
  else if(!scmp(para, "mach")) self->form = mach;
  else if(!scmp(para, "pe32")) self->form = pe32;
  else printf("form '%s' not supported\n", para);
}

static void dest(mode* self, opts*, char* para) {
  self->outp = para;
  self->show = save;
}

static void seek(mode* self, opts*, char* para) {
  if(self->dirn == sizeof self->dirs / sizeof *self->dirs) fail("too many -L directories\n");
  self->dirs[self->dirn++] = para;
}

// the source, an object file (name.o), or a static library (name.a), whose
// members are object files
static void apos(mode* self, opts*, char* para) {
  u64i n = slen(para);
  if(n > 2 && !scmp(para + n - 2, ".o")) bolt(self, para, scan(para), un_objs);
  else if(n > 2 && !scmp(para + n - 2, ".a")) bolt(self, para, scan(para), un_arch);
  else self->name = para;
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
    .show = exec,
    .arch = base,
    .form = host,
  };

  opts args[] = {
    {"-a", "--arch", .args = 1, .desc = "ia64, arm8", .call = arch},
    {"-f", "--form", .args = 1, .desc = "elfs, mach, pe32", .call = wrap},
    {"-o", "--emit", .args = 1, .desc = "write a file instead of running the code", .call = dest},
    {"-L", "--libs", .args = 1, .desc = "look for libname.a in this directory too", .call = seek},
    {"-h", "--help", .desc = "display help", .call = help}, {},
  };
  parse(&self, args, argc, argv, apos);
  if(!self.name) fail("no input file\n");
  if(self.show == exec && self.arch.cpu != base.cpu)
    fail("cannot run foreign code, write it with -o\n");

  make(&self);

  return 0;
}

