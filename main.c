// vim: ts=2:sw=2
#include "core.h"

#ifdef _WIN32
#define WIN32_LEAN_AND_MEAN
#include <windows.h>    /* VirtualAlloc, LoadLibrary, FlushInstructionCache */
#include <direct.h>     /* _mkdir */
#include <sys/stat.h>
#define mkdir(path, mode) _mkdir(path)
#else
#include <sys/mman.h>   /* for mmap         */
#include <unistd.h>     /* for getcwd       */
#include <sys/stat.h>   /* for mkdir        */
#include <dlfcn.h>      /* for dlopen       */
#include <signal.h>     /* for signal       */
#endif

// after the system headers: macros of the parts (arm8.c X) clash with them
#ifndef part
#include "util.c"
#include "core.c"
#include "dwarf.c"
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
void  ario_load(mode*, file*);
void  ario_pull(mode*, meta*);
#if defined(__aarch64__) || defined(_M_ARM64)
#define base arm8
#else // __x86_64__
#define base ia64
#endif
// a Linux host's C library: glibc, bionic on Android, else musl
#if defined(__ANDROID__)
#define CLIB cl_bionic
#elif defined(__linux__) && !defined(__GLIBC__)
#define CLIB cl_musl
#else
#define CLIB cl_glibc
#endif
#ifdef __APPLE__
#define host mach
#elif defined(_WIN32)
#define host pe32
#else
#define host elfs
#endif

// realloc, failing instead of NULL; free when size is 0
void* heap(void* memory, size_t size) {
  if(!size) { free(memory); return NULL; }
  if(!(memory = realloc(memory, size))) fail("out of memory\n");
  return memory;
}

file* scan(const char* name) {
  FILE* fp = fopen(name, "rb");
  if(!fp) return NULL;
  fseek(fp, 0, SEEK_END);
  long end = ftell(fp);
  if(end < 0) fail("cannot size %s\n", name); // a pipe or another unseekable file
  size_t size = (size_t)end;
  rewind(fp);

  file* f = heap(NULL, sizeof(file)+size);
  f->body = f->head;
  f->tail = f->head + size;
  f->head[size] = 0; // lexer stops at NUL
  if(fread(f->head, 1, size, fp) != size) fail("read error: %s\n", name);
  fclose(fp);
  return f;
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
#ifdef _WIN32
  FlushInstructionCache(GetCurrentProcess(), memory, size);
#else
  __builtin___clear_cache((char*)memory, (char*)memory + size);
#endif
}

// the pages prep made executable, writable again
void thaw(void* memory, size_t size) {
#ifdef _WIN32
  DWORD old;
  if(!VirtualProtect(memory, size, PAGE_READWRITE, &old))
#else
  if(mprotect(memory, size, PROT_READ | PROT_WRITE))
#endif
    fail("cannot unprotect %zu bytes\n", size);
}

void* drop(void* memory, size_t size) {
#ifdef _WIN32
  VirtualFree(memory, 0, MEM_RELEASE);
  (void)size;
#else
  munmap(memory, size);
#endif
  return NULL;
}

// the library s of a load ("c" is libc: otic's own on Unix), as given or
// decorated (m: libm.so, libm.dylib, m.dll)
void* bring(const item* s) {
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

bool  have(const item* s) {
  return bring(s) != NULL;
}

// the address of each import in its slot, from the libraries it loads. On
// Windows a name may be exported behind an underscore: spell makes the import
// that name, for a program written to load it. An import that the host lacks
// fails, unless soft: its slot stays as it was
void  plug(const mode* self, u64i* slot, bool spell, bool soft) {
  char  name[256];
  for(u32i i = 0; i < self->impn; i++) {
    meta* x = self->imps[i];
    meta* l = x->lib; // loaded when first used: its handle is its addr
    if(!l->addr && !(l->addr = bring(l->name))) {
      if(soft) continue;
      fail("cannot load library %.*s\n", nick(l->name));
    }
    void* lib = l->addr;
    if(x->name->size >= sizeof name) fail("name too long: %.*s\n", nick(x->name));
    snprintf(name, sizeof name, "%.*s", nick(x->name));
#ifdef _WIN32 // msvcrt has POSIX names behind an underscore: environ is _environ
    void* at = (void*)GetProcAddress(lib, name);
    if(!at && x->name->size + 1 < sizeof name) {
      snprintf(name, sizeof name, "_%.*s", nick(x->name));
      at = (void*)GetProcAddress(lib, name);
      if(at && spell) { // a copy of the item: the name stays in its table
        u64i  n = slen(name);
        item* s = self->heap(NULL, sizeof(item) + n + 1);
        *s = *x->name;
        s->name = (char*)(s + 1);
        copy((byte*)s->name, name, n + 1);
        s->size = n;
        x->name = s;
      }
      snprintf(name, sizeof name, "%.*s", nick(x->name)); // the failure names it as written
    }
#else
    (void)spell;
    void* at = dlsym(lib, name);
#endif
    if(!at && soft) continue;
    if(!at) fail("undefined: %s in %.*s\n", name, nick(l->name));
    slot[x->slot - 1] = (u64i)at;
  }
}

// Compile-time code (#) runs in one image for the whole compilation, so that
// what a # does to its globals and its heap is there for the next: text, data,
// bss and the import slots at fixed places of one region. Each # copies the
// text over again, docked (dock patches text in place: it is put back clean
// after), and the data laid since the last. One image for the process: a mode
// that makes code for the host has it, else the mirror of one that does not
#define IMGT (64u << 20) // text, then data, bss and the slots after it
#define IMGD (32u << 20)
#define IMGB (32u << 20)
#define IMGS (64u << 10)
static byte* image;
static u32i  warm;  // bytes of data the image has
static const char* where; // the # that runs: if its code crashes
static u64i  atline;

#ifndef _WIN32
static void  crash(int sig) {
  printf("%s:%llu: code run at compile time crashed (signal %d)\n", where, (unsigned long long)atline, sig);
  exit(1);
}
#endif

// the result bits of the function at entry in text, a real's too
u64i  eval(mode* self, byte* entry, bool real) {
  u64i  code = self->text->body - self->text->head;
  u64i  size = self->data->body - self->data->head;
  if(code > IMGT || size > IMGD || self->zero > IMGB || 8 * self->impn > IMGS)
    fail("code run at compile time is too large\\n");
  if(!image) {
    image = self->hold(0, (u64i)IMGT + IMGD + IMGB + IMGS);
#ifndef _WIN32
    signal(SIGSEGV, crash); signal(SIGBUS, crash); signal(SIGILL, crash);
    signal(SIGFPE, crash);  signal(SIGTRAP, crash);
#endif
  }
  else thaw(image, IMGT);
  byte* clean = self->heap(NULL, code + 1);
  copy(clean, self->text->head, code);
  self->bssat = IMGT + IMGD;
  self->slotat = IMGT + IMGD + IMGB;
  dock(self, IMGT, (u64i)image);
  self->bssat = self->slotat = 0;
  copy(image, self->text->head, code);
  copy(self->text->head, clean, code);
  self->heap(clean, 0);
  if(size > warm) copy(image + IMGT + warm, self->data->head + warm, size - warm);
  warm = self->ripe < size? self->ripe: (u32i)size;
  plug(self, (u64i*)(image + IMGT + IMGD + IMGB), 0, 1);
  prep(image, IMGT);
  where = home(self)->path;
  atline = home(self)->line;
  byte* at = image + (entry - self->text->head);
  if(!real) return ((u64i (*)(void))at)();
  double v = ((double (*)(void))at)();
  u64i  bits;
  copy((byte*)&bits, &v, sizeof bits);
  return bits;
}

typedef int (*func)(void);
// the image: text, then data, bss and the import slots on pages of their own
void  exec(mode* self) {
  if(self->boot != self->main) fail("cannot run a program with a C library, write it with -e\n");
#ifdef _WIN32 // a Win64 caller: only main's hail keeps rdi and rsi, as it wants
  if(self->form.os != os_win) fail("cannot run another form here, write it with -e\n");
#endif
  u64i  code = self->text->body - self->text->head;
  u64i  at   = (code + 0x3fff) & ~0x3fff; // 16 KB: pages on any host
  u64i  size = self->data->body - self->data->head;
  u64i  bss  = at + ((size + gap(self) - 1) & ~(gap(self) - 1));
  u64i  all  = bss + ((self->zero + 7) & -8) + 8 * self->impn; // the slots as pose places them
  byte* run  = self->hold(0, all);
  dock(self, at, (u64i)run);
  copy(run, self->text->head, code);
  copy(run + at, self->data->head, size);
  plug(self, (u64i*)(run + all) - self->impn, 0, 0);
  if(at) prep(run, at); // text only: data and bss stay writable
  int return_code = ((func)(run + (self->hail - self->text->head)))();
  printf("%d\n", return_code);
}

// the bytes of out into the file at path, which is freed
static void store(mode* self, const char* path, file* out) {
  FILE* fp = fopen(path, "wb");
  if(!fp) fail("cannot write: %s\n", path);
  fwrite(out->head, 1, out->body - out->head, fp);
  if(ferror(fp) | fclose(fp)) fail("write error: %s\n", path);
  self->heap(out, 0);
}

// a program written for the host, with its C library: each import is looked
// up as running it would, so that one its library lacks fails now rather than
// when the program loads
void  save(mode* self) {
  if(self->impn && self->main && self->form.os == host.os && self->arch.cpu == base.cpu
     && (self->form.os != os_linux || self->clib == CLIB)) {
    u64i* slot = self->heap(NULL, 8 * self->impn);
    plug(self, slot, 1, 0);
    self->heap(slot, 0);
  }
  file* out = self->form.pack(self);
  store(self, self->outp, out);
  for(u32i i = 0; i < self->sides; i++) {
    char* path = self->side[i].path;
    for(char* c = path; *c; c++) { // the directories of the path
      if(*c != '/') continue;
      *c = 0;
      mkdir(path, 0755);
      *c = '/';
    }
    store(self, path, self->side[i].data);
    self->heap(path, 0);
  }
}

// for code that makes another target's: a mode of the host for the same
// source, built as running it here would (libraries as exec has them), whose
// # run and left their results in its tape
mode* mirror(mode* from) {
  mode* self = zero(from, sizeof(mode));
  *self = (mode){
    .hold = hold,
    .drop = drop,
    .heap = heap,
    .scan = scan,
    .arch = base,
    .form = host,
    .ario = {ario_load, ario_pull},
    .have = have,
    .clib = CLIB,
    .name = from->name,
    .libs = lb_none,
    .eval = eval,
    .dirn = from->dirn,
  };
  copy((byte*)self->dirs, from->dirs, sizeof self->dirs);
  nest(self, (unit){.kind = un_root});
  build(self);
  return self;
}

typedef struct opts opts;
typedef void (*move)(mode*, const opts*, const char*);
struct opts {
  char* name;
  char* full;
  char* desc;
  move  call;
  byte  need:1,
        args:1;
};

static void help(mode* m, const opts* self, const char* para) {
  (void)m; (void)para;
  printf("usage: %s [options] file\n", "otic");
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

// x is the string y, none when y is NULL
static bool is(const char* x, const char* y) {
  return y && alike(x, y, slen(y) + 1);
}

static void arch(mode* self, const opts* opt, const char* para) {
  (void)opt;
  if(is(para, "ia64")) self->arch = ia64;
  else if(is(para, "arm8")) self->arch = arm8;
  else fail("architecture '%s' not supported\n", para);
}

static void wrap(mode* self, const opts* opt, const char* para) {
  (void)opt;
  if(is(para, "elfs")) self->form = elfs;
  else if(is(para, "mach")) self->form = mach;
  else if(is(para, "pe32")) self->form = pe32;
  else fail("form '%s' not supported\n", para);
}

static void dest(mode* self, const opts* opt, const char* para) {
  (void)opt;
  self->outp = para;
  self->show = save;
  if(!self->libs) self->libs = lb_dyno;
}

static void libc(mode* self, const opts* opt, const char* para) {
  (void)opt;
  if(is(para, "glibc")) self->clib = cl_glibc;
  else if(is(para, "musl")) self->clib = cl_musl;
  else if(is(para, "bionic")) self->clib = cl_bionic;
  else fail("C library '%s' not supported\n", para);
}

static void gdbg(mode* self, const opts* opt, const char* para) {
  (void)opt; (void)para;
  self->debug = 1;
}

static void flat(mode* self, const opts* opt, const char* para) {
  (void)opt; (void)para;
  self->libs = lb_relo;
}

static void seek(mode* self, const opts* opt, const char* para) {
  (void)opt;
  if(self->dirn == sizeof self->dirs / sizeof *self->dirs) fail("too many -L directories\n");
  self->dirs[self->dirn++] = para;
}

// the source: its link and load lines name the objects and libraries
static void apos(mode* self, const opts* opt, const char* para) {
  (void)opt;
  self->name = para;
}

static void parse(mode* self, const opts* opt, int argc, char** argv, move call) {
  for(int i = 1; i < argc; i++) {
    bool match = 0;
    for(const opts* back = opt; back->name; back++) {
      if(is(argv[i], back->name) || is(argv[i], back->full)) {
        match = 1;
        if(back->call) {
          if(back->args && ++i == argc) fail("%s needs an argument\n", argv[i - 1]);
          if(back->args) back->call(self, opt, argv[i]);
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
    .heap = heap,
    .show = exec,
    .scan = scan,
    .arch = base,
    .form = host,
    .ario = {ario_load, ario_pull},
    .have = have,
    .clib = CLIB,
  };
  nest(&self, (unit){.kind = un_root}); // unit 0, the builtins

  opts args[] = {
    {"-a", "--arch", .args = 1, .desc = "cpu architecture: ia64, arm8", .call = arch},
    {"-f", "--form", .args = 1, .desc = "binary format: elfs, mach, pe32", .call = wrap},
    {"-e", "--emit", .args = 1, .desc = "write a file instead of running the code", .call = dest},
    {"-s", "--static", .desc = "without main, write an object instead of a shared library", .call = flat},
    {"-g", "--debug", .desc = "write debug information", .call = gdbg},
    {"-p", "--path", .args = 1, .desc = "directory to look for modules and libraries", .call = seek},
    {"-l", "--libc", .args = 1, .desc = "C library of a Linux program: glibc, musl, bionic", .call = libc},
    {"-h", "--help", .desc = "display help", .call = help}, {0},
  };
  parse(&self, args, argc, argv, apos);
  if(!self.name) fail("no input file\n");
  if(self.show == exec && self.arch.cpu != base.cpu)
    fail("cannot run foreign code, write it with -e\n");
  if(self.show == exec) self.libs = lb_none, self.debug = 0; // running needs main; nothing reads debug information there
  char cwd[1024];
#ifdef _WIN32
  DWORD  size = self.debug? GetCurrentDirectoryA(sizeof cwd, cwd): 0;
  if(size && size < sizeof cwd) self.cwd = cwd;
#else
  if(self.debug && getcwd(cwd, sizeof cwd)) self.cwd = cwd;
#endif
  self.mirror = mirror;
  if(self.arch.cpu == base.cpu && self.form.os == host.os && (self.form.os != os_linux || self.clib == CLIB))
    self.eval = eval; // code for the host runs at compile time here, else in a mirror

  make(&self);

  return 0;
}

