// vim: ts=2:sw=2
#include "core.h"

#ifdef _WIN32
#include <memoryapi.h>  /* for VirtualAlloc */
#include <libloaderapi.h> /* for LoadLibrary */
#else
#include <sys/mman.h>   /* for mmap         */
#include <dlfcn.h>      /* for dlopen       */
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

file* scan(char* name) {
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

// the library s of a load ("c" is libc: tic's own on Unix), as given or
// decorated (m: libm.so, libm.dylib, m.dll)
void* bring(item* s) {
  char  name[256];
  int   n = (int)s->size - 2; // without the quotes
  if(n > 200) fail("library name too long: %.*s\n", nick(s));
#ifdef _WIN32
  if(n == 1 && *(s->name + 1) == 'c') return LoadLibraryA("msvcrt.dll");
  snprintf(name, sizeof name, "%.*s", n, s->name + 1);
  void* at = LoadLibraryA(name);
  if(at) return at;
  snprintf(name, sizeof name, "%.*s.dll", n, s->name + 1);
  return LoadLibraryA(name);
#else
  if(n == 1 && *(s->name + 1) == 'c') return dlopen(NULL, RTLD_NOW);
  snprintf(name, sizeof name, "%.*s", n, s->name + 1);
  void* at = dlopen(name, RTLD_NOW);
  if(at) return at;
#ifdef __APPLE__
  snprintf(name, sizeof name, "lib%.*s.dylib", n, s->name + 1);
  return dlopen(name, RTLD_NOW);
#else // libname.so may be an ld script (glibc's libm.so): then a soname, libm.so.6
  for(int v = -1; !at && v < 10; v++) {
    if(v < 0) snprintf(name, sizeof name, "lib%.*s.so", n, s->name + 1);
    else snprintf(name, sizeof name, "lib%.*s.so.%d", n, s->name + 1, v);
    at = dlopen(name, RTLD_NOW);
  }
  return at;
#endif
#endif
}

// the address of each import in its slot, from the libraries it loads
void  plug(mode* self, u64i* slot) {
  void* libs[sizeof self->libs / sizeof *self->libs] = {0};
  char  name[256];
  for(u32i i = 0; i < self->impn; i++) {
    meta* x = self->imps[i];
    u32i  l = x->lib - 1;
    if(!libs[l] && !(libs[l] = bring(self->libs[l])))
      fail("cannot load library %.*s\n", nick(self->libs[l]));
    if(x->name->size >= sizeof name) fail("name too long: %.*s\n", nick(x->name));
    snprintf(name, sizeof name, "%.*s", nick(x->name));
#ifdef _WIN32
    void* at = (void*)GetProcAddress(libs[l], name);
#else
    void* at = dlsym(libs[l], name);
#endif
    if(!at) fail("undefined: %s in %.*s\n", name, nick(self->libs[l]));
    slot[x->slot - 1] = (u64i)at;
  }
}

#if defined(_WIN32) && defined(__x86_64__)
typedef int (__attribute__((sysv_abi)) *func)(); // generated code clobbers rdi, rsi
#else
typedef int (*func)();
#endif
// the image: text, then data, bss and the import slots on pages of their own
void  exec(mode* self) {
  if(self->boot != self->main) fail("cannot run a program with a C library, write it with -e\n");
  u64i  code = self->text->body - self->text->head;
  u64i  at   = (code + 0x3fff) & ~0x3fff; // 16 KB: pages on any host
  u64i  size = self->data->body - self->data->head;
  u64i  bss  = at + ((size + gap(self) - 1) & -gap(self));
  u64i  all  = bss + ((self->zero + 7) & -8) + 8 * self->impn; // the slots as pose places them
  byte* run  = self->hold(0, all);
  dock(self, at, (u64i)run);
  copy(run, self->text->head, code);
  copy(run + at, self->data->head, size);
  plug(self, (u64i*)(run + all) - self->impn);
  if(at) prep(run, at); // text only: data and bss stay writable
  int return_code = ((func)(run + (self->main - self->text->head)))();
  printf("%d\n", return_code);
}

void  save(mode* self) {
  if(self->impn) fail("cannot call a library from this form yet\n");
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
    {"-a", "--arch", .args = 1, .desc = "cpu architecture: ia64, arm8", .call = arch},
    {"-f", "--form", .args = 1, .desc = "binary format: elfs, mach, pe32", .call = wrap},
    {"-e", "--emit", .args = 1, .desc = "write a file instead of running the code", .call = dest},
    {"-p", "--path", .args = 1, .desc = "specify path to look for library", .call = seek},
    {"-h", "--help", .desc = "display help", .call = help}, {},
  };
  parse(&self, args, argc, argv, apos);
  if(!self.name) fail("no input file\n");
  if(self.show == exec && self.arch.cpu != base.cpu)
    fail("cannot run foreign code, write it with -e\n");

  make(&self);

  return 0;
}

