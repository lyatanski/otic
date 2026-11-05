// vim: ts=2:sw=2
// core's spec, the front end alone (core, link, util): the target, the
// form and the host are mocks in mode. Each emit call is one byte of text,
// its op, and a line of the trace ("move 42"), so a case reads what core asked
// the target for. fail longjmps back here, so errors are checked in process
#include <setjmp.h>
#include <stdarg.h>
#include <string.h>
#include "../core.h"

static jmp_buf bail;
static char    said[256]; // what fail said
#undef  fail
#define fail(...) (snprintf(said, sizeof said, __VA_ARGS__), longjmp(bail, 1))

#include "../util.c"
#include "../core.c"
#include "../link.c"
#include "test.h"

// the trace: each emit call, ", " between them
static char  trace[8192];
static u32i  traced;
static u32i  calls[64]; // per op, the calls made
static u32i  shows, packs, takes;
static u64i  laid; // data bytes when shown: make frees data after show

#define OPS(X) X(move) X(load) X(stor) X(peek) X(addr) X(glob) X(site) X(read) \
  X(fill) X(push) X(pull) X(swap) X(cast) X(calc) X(when) X(jump) X(back)       \
  X(loop) X(open) X(size) X(parm) X(args) X(dial) X(trap) X(done) X(boot)       \
  X(link) X(ffi) X(crt) X(flag) X(cond) X(keep) X(lift) X(take) X(pass)
#define TAG(x) op_##x,
#define NAME(x) #x,
enum { OPS(TAG) };
static char* const ops[] = { OPS(NAME) };

static void note(file* text, byte op, char* fmt, ...) {
  va_list ap;
  calls[op]++;
  traced += snprintf(trace + traced, sizeof trace - traced, "%s%s", traced? ", ": "", ops[op]);
  va_start(ap, fmt);
  traced += vsnprintf(trace + traced, sizeof trace - traced, fmt, ap);
  va_end(ap);
  if(traced >= sizeof trace) traced = sizeof trace - 1;
  if(text) *text->body++ = op;
}

// what a patch points at: the op emitted there
static char* at(const file* text, const byte* x) {
  return x >= text->head && x < text->body? ops[*x]: "nowhere";
}
// the op whose branch a patch mends: the one before the mark
static char* before(const file* text, const byte* x) {
  return x > text->head && x <= text->body? ops[x[-1]]: "nowhere";
}

static void mock_move(file* t, s32i n)                 { note(t, op_move, " %d", n); }
static void mock_load(file* t, s32i slot, u32i size)   { note(t, op_load, " %d:%u", slot, size); }
static void mock_stor(file* t, s32i slot, u32i size)   { note(t, op_stor, " %d:%u", slot, size); }
static void mock_peek(file* t, s32i slot, u32i size)   { note(t, op_peek, " %d:%u", slot, size); }
static void mock_addr(file* t, s32i slot)              { note(t, op_addr, " %d", slot); }
static void mock_glob(file* t)                         { note(t, op_glob, ""); }
static void mock_site(const file* t, byte* g, s64i dist) { note(NULL, op_site, " %ld", (long)dist); (void)t; (void)g; }
static void mock_read(file* t, u32i size)              { note(t, op_read, " %u", size); }
static void mock_fill(file* t, u32i size)              { note(t, op_fill, " %u", size); }
static void mock_push(file* t)                         { note(t, op_push, ""); }
static void mock_pull(file* t)                         { note(t, op_pull, ""); }
static void mock_swap(file* t)                         { note(t, op_swap, ""); }
static void mock_cast(file* t, bool wide)              { note(t, op_cast, wide? " long": " int"); }
// a comparison op as written: "<=" for tokd('<', '='), its flags dropped
static char* mock_op(u32i op, char* o) {
  o[0] = (char)op; o[1] = (char)(op >> 8); o[2] = (char)(op >> 16); o[3] = 0;
  return o;
}
static void mock_calc(file* t, u32i op, bool cnst, s32i n) {
  char o[4];
  char* w = op & WIDE? op & TEST? ".w.t": ".w": op & TEST? ".t": "";
  if(cnst) note(t, op_calc, "%s %s %d", w, mock_op(op, o), n);
  else note(t, op_calc, "%s %s", w, mock_op(op, o));
}
static void mock_when(file* t, bool nz)                { note(t, op_when, nz? " nz": " z"); }
static void mock_jump(file* t)                         { note(t, op_jump, ""); }
static void mock_back(const file* t, byte* prev)       { note(NULL, op_back, " %s", before(t, prev)); }
static void mock_loop(file* t, const byte* dest)       { note(t, op_loop, " %s", at(t, dest)); }
static void mock_open(file* t)                         { note(t, op_open, ""); }
static void mock_size(const file* t, byte* func, s32i size) { note(NULL, op_size, " %s %d", at(t, func), size); }
static void mock_parm(file* t, u32i i, s32i slot)      { note(t, op_parm, " %u %d", i, slot); }
static void mock_args(file* t, u32i i)                 { note(t, op_args, " %u", i); }
static void mock_dial(file* t)                         { note(t, op_dial, ""); }
static void mock_trap(file* t, u32i argc)              { note(t, op_trap, " %u", argc); }
static void mock_done(file* t)                         { note(t, op_done, ""); }
static void mock_boot(file* t)                         { note(t, op_boot, ""); }
static void mock_link(const file* t, byte* from, const byte* to) { note(NULL, op_link, " %s %s", at(t, from), at(t, to)); }
static byte* mock_ffi(file* t, const byte* func, u32i argc, u32i fixed, osys os) {
  (void)os;
  byte* site = t->body;
  note(t, op_ffi, " %u/%u%s", fixed, argc, func? "": " slot");
  return func? site: NULL;
}
static void mock_crt(file* t, const byte* main, const byte* start) {
  note(t, op_crt, " %s %s", at(t, main), at(t, start));
}
static void mock_flag(file* t, u32i op)                { char o[4]; note(t, op_flag, " %s", mock_op(op, o)); }
static void mock_cond(file* t, u32i op, bool hold)     { char o[4]; note(t, op_cond, " %s%s", hold? "": "!", mock_op(op, o)); }
static void mock_keep(file* t)                         { note(t, op_keep, ""); }
static void mock_lift(const file* t, byte* keep, u32i k) { note(NULL, op_lift, " %s %u", at(t, keep), k); }
static void mock_tmpk(file* t, u32i k)                 { note(t, op_take, " %u", k); }
static void mock_pass(file* t, u32i i)                 { note(t, op_pass, " %u", i); }
static void mock_hail(file* t, osys os)                { (void)t; (void)os; }

static const emit mock = {
  mock_move, mock_load, mock_stor, mock_peek, mock_addr, mock_glob, mock_site,
  mock_read, mock_fill, mock_push, mock_pull, mock_swap, mock_cast, mock_calc,
  mock_when, mock_jump, mock_back, mock_loop, mock_open, mock_size, mock_parm,
  mock_args, mock_dial, mock_trap, mock_done, mock_boot, mock_link, mock_ffi,
  mock_crt, mock_flag, mock_cond, mock_keep, mock_lift, mock_tmpk, mock_pass,
  mock_hail, cpu_ia64,
};

static file* mock_pack(mode* m)          { packs++; (void)m; return NULL; }
static void  mock_take(mode* m, file* f) { takes++; (void)m; (void)f; }

// the sources make may scan, by path
typedef struct { char* path; char* src; } named;
static named files[8];
static u32i  filen;

static file* mock_scan(const char* path) {
  for(u32i i = 0; i < filen; i++) {
    if(strcmp(files[i].path, path)) continue;
    u64i  n = strlen(files[i].src);
    file* f = malloc(sizeof(file) + n);
    f->body = f->head;
    f->tail = f->head + n;
    memcpy(f->head, files[i].src, n + 1);
    return f;
  }
  return NULL;
}
static void* mock_hold(void* p, size_t size)  { (void)p; return malloc(size); }
static void* mock_drop(void* p, size_t size)  { (void)p; (void)size; return NULL; } // text: freed by the next make_
static void* mock_heap(void* p, size_t size)  { if(size) return realloc(p, size); free(p); return NULL; }
static void  mock_show(mode* m)               { shows++; laid = m->data->body - m->data->head; }

static mode self;

// a source beside main.c, for link
static void source(char* path, char* src) {
  files[filen++] = (named){path, src};
}

static byte libs; // what make_ asks for without main, reset after each

// compile main.c, src: 1 when make got to show, else 0 and said tells why
static int  make_(char* src) {
  free(self.text);
  memset(&self, 0, sizeof self);
  memset(calls, 0, sizeof calls);
  laid = traced = shows = packs = takes = 0;
  trace[0] = said[0] = 0;
  self = (mode){
    .name = "main.c",
    .hold = mock_hold, .drop = mock_drop, .heap = mock_heap, .show = mock_show, .scan = mock_scan,
    .arch = mock, .form = {mock_pack, mock_take, os_linux}, .libs = libs,
  };
  libs = lb_none;
  source("main.c", src);
  nest(&self, (unit){.kind = un_root});
  if(setjmp(bail)) { filen = 0; return 0; }
  make(&self);
  filen = 0;
  return 1;
}

// the trace has this run of calls, else it is shown
static bool has(const char* run) {
  if(strstr(trace, run)) return 1;
  printf("    want %s\n    in   %s\n", run, trace);
  return 0;
}
// the trace is exactly calls, else it is shown
static bool is(const char* all) {
  if(!strcmp(trace, all)) return 1;
  printf("    want %s\n    got  %s\n", all, trace);
  return 0;
}

// src compiles, else why not is shown
static bool compiles(char* src) {
  if(make_(src)) return 1;
  printf("    failed: %s", said);
  return 0;
}
// src fails with why
static bool rejects(char* src, const char* why) {
  if(!make_(src) && strstr(said, why)) return 1;
  printf("    want %s\n    got  %s\n", why, said[0]? said: "no error\n");
  return 0;
}

spec("core") {
  context("program") {
    it("boots once, into main") {
      check(compiles("int main() { return 42; }"));
      check(is("boot, open, move 42, done, move 0, done, size open 0, link boot open"));
      check(calls[op_boot] == 1 && calls[op_link] == 1);
      check(shows == 1 && packs == 0);
    }
    it("leaves the output to show, not the form") {
      check(compiles("int main() { }"));
      check(shows == 1 && packs == 0 && takes == 0);
    }
    it("enters at main, not the first function") {
      check(compiles("int f() { return 1; } int main() { return 2; }"));
      check(*self.main == op_open && self.main > self.text->head + 2);
    }
    it("fails without main") {
      check(rejects("int f() { return 1; }", "no main function"));
      check(shows == 0);
    }
    it("makes a library without main, when one is asked for") {
      libs = lb_dyno;
      check(compiles("int f() { return 1; }"));
      check(shows == 1 && !self.main && !self.boot && calls[op_link] == 0);
    }
    it("still enters at main in a library build") {
      libs = lb_relo;
      check(compiles("int main() { return 2; }"));
      check(self.main && calls[op_link] == 1);
    }
    it("names the line of an error") {
      check(rejects("int main() {\n  return 1;\n  @\n}", "main.c:3:"));
    }
  }

  context("functions") {
    it("opens, stores each parameter, sizes the frame") {
      check(compiles("int f(int a, int b) { return a; } int main() { return 0; }"));
      check(has("open, parm 0 -8, parm 1 -16, load -8:4, done, move 0, done, size open 16"));
    }
    it("calls one defined before, linking it at once") {
      check(compiles("int f(int a) { return a; } int main() { return f(3); }"));
      check(has("move 3, pass 0, dial, link dial open"));
    }
    it("passes the last argument from acc, pops the others") {
      check(compiles("int f(int a, int b) { return a; } int main() { return f(1, 2); }"));
      check(has("move 1, push, move 2, pass 1, args 0, dial"));
    }
    it("sign extends an int argument only for a long parameter") {
      check(compiles("int f(int a, long b) { return a; } int main() { int n = -1; return f(n, n); }"));
      check(has("load -8:4, push, load -8:4, cast long, pass 1") && calls[op_cast] == 1);
    }
    it("sign extends each int argument of a call not defined yet") {
      check(compiles("int main() { int n = -1; return f(n); } int f(int a) { return a; }"));
      check(has("load -8:4, cast long, pass 0"));
    }
    it("leaves a constant not below 0 as its move zero extends it") {
      check(compiles("int f(long a) { return 0; } int main() { long x = 5; x = x + 1; return f(2); }"));
      check(calls[op_cast] == 0);
      check(compiles("int f(long a) { return 0; } int main() { return f(-2); }"));
      check(has("move -2, cast long, pass 0"));
    }
    it("calls one defined later, linking it when the pass ends") {
      check(compiles("int main() { return f(); } int f() { return 1; }"));
      check(calls[op_dial] == 1 && calls[op_link] == 2);
      check(has("size open 0, link dial open, link boot open"));
    }
    it("checks the number of arguments") {
      check(rejects("int f(int a) { return a; } int main() { return f(); }", "takes 1 arguments, got 0"));
    }
    it("fails on a call to no function") {
      check(rejects("int main() { return g(); }", "no function 'g'"));
    }
    it("sends syscall to the kernel") {
      check(compiles("int main() { syscall(60, 0); }"));
      check(calls[op_trap] == 1 && calls[op_dial] == 0 && has("trap 2"));
    }
  }

  context("expressions") {
    it("folds constants into one move") {
      check(compiles("int main() { return 2 + 3 * 4; }"));
      check(has("open, move 14, done") && calls[op_calc] == 0);
    }
    it("computes locals at run time, a constant operand inline") {
      check(compiles("int main() { int a = 2; return a + 3; }"));
      check(has("load -8:4, calc + 3, done") && calls[op_push] == 0);
    }
    it("peeks a local right operand") {
      check(compiles("int main() { int a = 2; int b = 3; return a * b; }"));
      check(has("load -8:4, peek -16:4, calc *") && calls[op_push] == 0);
    }
    it("computes the right first when the left is a local") {
      check(compiles("int main() { int a = 2; int b = 3; return a * (b - 1); }"));
      check(has("load -16:4, calc - 1, swap, load -8:4, calc *") && calls[op_push] == 0);
    }
    it("keeps a computed left operand in a scratch register while the right is computed") {
      check(compiles("int main() { int a = 2; int b = 3; return (a + 1) * (b - 1); }"));
      check(has("load -8:4, calc + 1, keep, load -16:4, calc - 1, lift keep 0, take 0, calc *"));
    }
    it("nests kept operands in the next scratch registers") {
      check(compiles("int main() { int a = 2; return (a + 1) * ((a + 2) - (a + 3)); }"));
      check(has("lift keep 1, take 1, calc -, lift keep 0, take 0, calc *"));
    }
    it("keeps them on the stack past the scratch registers") {
      check(compiles("int main() { int a = 2; return (a+1) * ((a+2) * ((a+3) * ((a+4) * ((a+5) - (a+6))))); }"));
      check(calls[op_lift] == 4 && calls[op_pull] == 1);
    }
    it("keeps a left operand on the stack across a call") {
      check(compiles("int f() { return 1; } int main() { int a = 2; return (a + 1) * f(); }"));
      check(has("calc + 1, keep, dial") && has("pull, calc *") && calls[op_lift] == 0);
    }
    it("keeps the outer operand on the stack when a nested right operand calls") {
      check(compiles("int f() { return 1; } int main() { int a = 2; return (a + 1) * ((a + 2) - f()); }"));
      check(calls[op_lift] == 0 && calls[op_pull] == 2);
    }
    it("computes on 64 bits for a long") {
      check(compiles("int main() { long a = 2; return a + 3; }"));
      check(has("calc.w + 3"));
    }
    it("stores through a pointer") {
      check(compiles("int main() { int a; int* p = &a; *p = 5; return a; }"));
      check(has("addr -8") && calls[op_fill] == 1);
    }
  }

  context("statements") {
    it("branches on the flags of a comparison") {
      check(compiles("int main() { int a = 1; when(a < 2) return 2; return 3; }"));
      check(has("load -8:4, calc.t < 2, cond !<, move 2, done, back cond") && calls[op_flag] == 0);
    }
    it("loops on the flags of a comparison") {
      check(compiles("int main() { int a = 3; while(a > 0) a -= 1; return a; }"));
      check(has("load -8:4, calc.t > 0, cond !>,") && has("loop load, back cond"));
    }
    it("compares longs on 64 bits") {
      check(compiles("int main() { long a = 1; when(a == 2) return 2; return 3; }"));
      check(has("calc.w.t == 2, cond !=="));
    }
    it("turns the flags into a value when it is not a condition") {
      check(compiles("int main() { int a = 1; return a < 2; }"));
      check(has("calc.t < 2, flag <, done"));
    }
    it("branches on each comparison of && and ||") {
      check(compiles("int main() { int a = 1; when(a < 2 && a > 0) return 2; return 3; }"));
      check(has("calc.t < 2, cond !<,") && has("calc.t > 0, flag >, jump") && calls[op_when] == 1);
    }
    it("branches over a when, patching the branch after its body") {
      check(compiles("int main() { int a = 1; when(a) return 2; return 3; }"));
      check(has("when z, move 2, done, back when"));
    }
    it("jumps over the else") {
      check(compiles("int main() { int a = 1; when(a) return 2; else return 3; }"));
      check(has("when z, move 2, done, jump, back when, move 3, done, back jump"));
    }
    it("loops back to the condition of a while") {
      check(compiles("int main() { int a = 3; while(a) a -= 1; return a; }"));
      check(has("load -8:4, when z,") && has("loop load, back when"));
    }
    it("patches each break past the loop") {
      check(compiles("int main() { int a = 3; while(a) { when(a) break; break; } return a; }"));
      check(calls[op_jump] == 2 && has("loop load, back jump, back jump, back when"));
    }
    it("fails on break outside a loop") {
      check(rejects("int main() { break; }", "break outside a loop"));
    }
  }

  context("globals") {
    it("addresses a global through glob, patched later") {
      check(compiles("int g = 7; int main() { return g; }"));
      check(has("glob, read 4") && self.seats == 1);
      check(laid >= 4);
    }
  }

  context("units") {
    it("links another source once, as code before the call") {
      source("lib.c", "int one() { return 1; }");
      check(compiles("link \"lib\"; link \"lib\"; int main() { return one(); }"));
      check(self.unitn == 3 && calls[op_open] == 2);
      check(has("link dial open"));
    }
    it("hides a static function from other sources") {
      source("lib.c", "static int one() { return 1; }");
      check(rejects("link \"lib\"; int main() { return one(); }", "no function 'one'"));
    }
    it("fails when a link finds nothing") {
      check(rejects("link \"none\"; int main() { }", "cannot find \"none\""));
    }
  }
  context("library") {
    meta** defs;
    cord*  cords;
    it("signs the public functions and globals, not statics or imports") {
      libs = lb_relo;
      check(compiles("int g = 1;\nstatic int h;\nint f() { return g + h; }\nstatic int s() { return 0; }\n"
                     "extern int e;\nint imp(int);\nstruct pt { int x; };\n"));
      u32i n = self.core.sign(&self, &defs), fs = 0, gs = 0;
      for(u32i i = 0; i < n; i++) {
        fs += defs[i]->kind == call && defs[i]->name->size == 1 && *defs[i]->name->name == 'f';
        gs += defs[i]->kind == glob && defs[i]->name->size == 1 && *defs[i]->name->name == 'g';
      }
      free(defs);
      check(n == 2 && fs == 1 && gs == 1);
    }
    it("knits the references of the source to its globals at each glob") {
      libs = lb_relo;
      check(compiles("int g = 1;\nint z;\nint f() { return g + z; }\n")); // z is loaded first
      u32i n = self.core.knit(&self, &cords);
      check(n == 2 && cords[0].kind == cd_glob && cords[1].kind == cd_glob && !cords[0].name);
      check(cords[0].aim.off == ZERO && cords[1].aim.off == 0);
      check(!strcmp(at(self.text, cords[0].site.at), "glob"));
      free(cords);
    }
  }
}
