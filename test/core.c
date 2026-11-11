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
  X(link) X(ffi) X(crt) X(flag) X(cond) X(keep) X(lift) X(take) X(pass)       \
  X(rop) X(stow)
#define TAG(x) op_##x,
#define NAME(x) #x,
enum { OPS(TAG) };
static char* const ops[] = { OPS(NAME) };

static void note(file* text, byte op, char* fmt, ...) {
  va_list ap;
  calls[op]++;
  traced += snprintf(trace + traced, sizeof trace - traced, "%s%s", traced? ", ": "", ops[op]);
  if(traced >= sizeof trace) traced = sizeof trace - 1; // snprintf counts what it cut: keep the next size positive
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
static void mock_size(file* t, byte* func, s32i size) { note(NULL, op_size, " %s %d", at(t, func), size); }
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
static void mock_crt(file* t, const byte* main, const byte* start, clib c) {
  note(t, op_crt, " %s %s%s", at(t, main), at(t, start), c == cl_bionic? " bionic": "");
}
static void mock_flag(file* t, u32i op)                { char o[4]; note(t, op_flag, " %s", mock_op(op, o)); }
static void mock_cond(file* t, u32i op, bool hold)     { char o[4]; note(t, op_cond, " %s%s", hold? "": "!", mock_op(op, o)); }
static void mock_keep(file* t)                         { note(t, op_keep, ""); }
static void mock_lift(const file* t, byte* keep, u32i k) { note(NULL, op_lift, " %s %u", at(t, keep), k); }
static void mock_tmpk(file* t, u32i k)                 { note(t, op_take, " %u", k); }
static void mock_pass(file* t, u32i i)                 { note(t, op_pass, " %u", i); }
static void mock_hail(file* t, osys os)                { (void)t; (void)os; }

// an operand of rop: r2 a register of a local, a2 an argument register
static char* mock_reg(s32i x, s32i n, char* o) {
  if(x >= 0) sprintf(o, "r%d", x);
  else if(x == NUM) sprintf(o, "%d", n);
  else if(x <= ARG) sprintf(o, "a%d", ARG - x);
  else sprintf(o, x == ACC? "acc": "tmp");
  return o;
}
static void mock_rop(file* t, u32i op, s32i to, s32i by, s32i n) {
  char o[4], a[16], b[16];
  char* w = op & WIDE? op & TEST? ".w.t": ".w": op & TEST? ".t": "";
  note(t, op_rop, "%s %s %s %s", w, mock_reg(to, 0, a), mock_op(op, o), mock_reg(by, n, b));
}
static void mock_stow(file* t, u32i r, s32i slot, bool back) { note(t, op_stow, back? " r%u < %d": " r%u > %d", r, slot); }

static u32i regs; // registers for locals the next make_ has, reset after each

static const emit mock = {
  mock_move, mock_load, mock_stor, mock_peek, mock_addr, mock_glob, mock_site,
  mock_read, mock_fill, mock_push, mock_pull, mock_swap, mock_cast, mock_calc,
  mock_when, mock_jump, mock_back, mock_loop, mock_open, mock_size, mock_parm,
  mock_args, mock_dial, mock_trap, mock_done, mock_boot, mock_link, mock_ffi,
  mock_crt, mock_flag, mock_cond, mock_keep, mock_lift, mock_tmpk, mock_pass,
  mock_hail, mock_rop, mock_stow, cpu_ia64, 0,
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
static u64i  tail8; // the last 8 bytes of data when shown
static void  mock_show(mode* m) {
  shows++;
  laid = m->data->body - m->data->head;
  if(laid >= 8) memcpy(&tail8, m->data->body - 8, 8);
}

static mode self;

// a source beside main.c, for link
static void source(char* path, char* src) {
  files[filen++] = (named){path, src};
}

static byte libs; // what make_ asks for without main, reset after each
static clib clibs; // the C library of a Linux image, reset after each

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
    .clib = clibs,
  };
  self.arch.regs = regs;
  libs = lb_none;
  clibs = cl_glibc;
  regs = 0;
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

// the calls of op whose code is still in text: the trace keeps the dropped ones
static u32i left(byte op) {
  u32i n = 0;
  for(const byte* p = self.text->head; p < self.text->body; p++) n += *p == op;
  return n;
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
    it("starts a written program with imports through libc, its start an import too") {
      libs = lb_dyno;
      check(compiles("int puts(char* s);\nint main() { return puts(\"\"); }"));
      check(has("size open 0, glob, crt open nowhere, link boot glob"));
      check(self.aims[self.seats - 1].off == (SLOT | 1)); // the glob: its slot
      check(self.impn == 2 && self.imps[1]->kind == call && self.imps[1]->lib == self.imps[0]->lib);
      check(self.imps[1]->name->size == 17 && !memcmp(self.imps[1]->name->name, "__libc_start_main", 17));
    }
    it("starts a written program with imports through bionic's __libc_init") {
      libs = lb_dyno;
      clibs = cl_bionic;
      check(compiles("int puts(char* s);\nint main() { return puts(\"\"); }"));
      check(has("crt open nowhere bionic, link boot glob"));
      check(self.impn == 2 && self.imps[1]->lib == self.imps[0]->lib);
      check(self.imps[1]->name->size == 11 && !memcmp(self.imps[1]->name->name, "__libc_init", 11));
    }
    it("runs a program with imports from main") {
      check(compiles("int puts(char* s);\nint main() { return puts(\"\"); }"));
      check(has("link boot open") && calls[op_crt] == 0 && self.impn == 1);
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
      check(compiles("int f(int a) { return a; } int main() { int x = 3; return f(x); }"));
      check(has("load -8:4, pass 0, dial, link dial open"));
    }
    it("passes the last argument from acc, pops the others") {
      check(compiles("int f(int a, int b) { return a; } int main() { int x = 1; return f(x + 1, x + 2); }"));
      check(has("calc + 1, push, load -8:4, calc + 2, pass 1, args 0, dial"));
    }
    it("moves a constant argument to its register once the others are computed") {
      check(compiles("int f(int a, int b) { return a; } int main() { return f(1, 2); }"));
      check(has("open, rop.w a1 = 2, rop.w a0 = 1, dial") && calls[op_push] == 0);
    }
    it("leaves the argument before constants in acc, its push dropped") {
      check(compiles("int f(int a, int b) { return a; } int main() { int x = 1; return f(x + 1, 2); }"));
      check(has("calc + 1, push, pass 0, rop.w a1 = 2, dial") && left(op_push) == 0);
    }
    it("sign extends an int argument only for a long parameter") {
      check(compiles("int f(int a, long b) { return a; } int main() { int n = -1; return f(n, n); }"));
      check(has("load -8:4, push, load -8:4, cast long, pass 1") && calls[op_cast] == 1);
    }
    it("sign extends each int argument of a call not defined yet") {
      check(compiles("int main() { int n = -1; return f(n); } int f(int a) { return a; }"));
      check(has("load -8:4, cast long, pass 0"));
    }
    it("leaves the sign of a constant argument to its move") {
      check(compiles("int f(long a) { return 0; } int main() { long x = 5; x = x + 1; return f(2); }"));
      check(calls[op_cast] == 0);
      check(compiles("int f(long a) { return 0; } int main() { return f(-2); }"));
      check(has("rop.w a0 = -2, dial") && calls[op_cast] == 0);
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
    it("folds sizeof and characters like numbers") {
      check(compiles("int main() { return sizeof(long) * 'a' + sizeof \"ab\"; }"));
      check(has("open, move 779, done") && calls[op_calc] == 0);
    }
    it("drops the code of the operand of sizeof") {
      check(compiles("int g; int main() { return sizeof(g = 7); }"));
      check(has("fill 4, move 4, done")); // emitted, then trimmed from text
      check(self.main[0] == op_open && self.main[1] == op_move && self.main[2] == op_done);
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

  context("registers") {
    it("keeps a local used 3 times in a register, saved in the first slot") {
      regs = 2;
      check(compiles("int main() { int a = 1; a += 2; return a; }"));
      check(is("boot, open, stow r0 > -8, rop r0 = 1, rop r0 + 2, rop acc = r0, stow r0 < -8, done, "
               "move 0, stow r0 < -8, done, size open 16, link boot open"));
    }
    it("leaves a local used less in the frame") {
      regs = 2;
      check(compiles("int main() { int a = 1; return a; }"));
      check(has("move 1, stor -8:4, load -8:4, done") && calls[op_stow] == 0);
    }
    it("has none on a target without registers for locals") {
      check(compiles("int main() { int a = 1; a += 2; a += 3; return a; }"));
      check(calls[op_stow] == 0 && calls[op_rop] == 0);
    }
    it("moves a parameter in from its argument register, on its width") {
      regs = 2;
      check(compiles("int f(int a, long b, int c) { when(a < b) return a + b; return a + c; } int main() { return 0; }"));
      check(has("open, stow r0 > -8, stow r1 > -16, rop r0 = a0, rop.w r1 = a1, parm 2 -24,"));
    }
    it("restores the registers before each return") {
      regs = 2;
      check(compiles("int f(int a) { when(a < 2) return a; return a + 1; } int main() { return 0; }"));
      check(calls[op_stow] == 4 && has("rop acc = r0, stow r0 < -8, done, back cond"));
    }
    it("keeps a local whose address is taken in the frame") {
      regs = 2;
      check(compiles("int main() { int a = 1; int* p = &a; a += 2; a += 3; return a; }"));
      check(has("addr -8") && calls[op_rop] == 0);
      regs = 2;
      check(compiles("int main() { int a = 1; int* p = &(a); a += 2; a += 3; return a; }"));
      check(has("addr -8") && calls[op_rop] == 0);
      regs = 2;
      check(compiles("int main() { int a = 1; a += 2; a += 3; return &a == 0; }"));
      check(has("addr -8") && calls[op_rop] == 0);
    }
    it("tells a & between operands from an address") {
      regs = 2;
      check(compiles("int main() { int a = 1, b = 3; a += 2; b += a & b; return (a) & b; }"));
      check(has("rop acc = r0, rop acc & r1, rop r1 + acc, rop acc = r0, rop acc & r1, stow") && calls[op_addr] == 0);
    }
    it("leaves arrays and structs in the frame") {
      regs = 2;
      check(compiles("struct v { int x; }; int main() { int a[2]; v b; a[0] = 1; a[1] = a[0]; b.x = a[1]; b.x += 1; return b.x; }"));
      check(calls[op_stow] == 0);
    }
    it("gives the registers to the names used most, a use in a loop counting 8") {
      regs = 1;
      check(compiles("int main() { int a = 1, b = 2, i = 0; a += 1; a += 1; a += 1;\n"
                     "  while(i < 3) { b += 1; i += 1; } return a + b; }"));
      check(has("rop r0 = 0,") && has("rop.t r0 < 3, cond !<,") && has("rop r0 + 1, loop rop"));
      check(calls[op_stow] == 3);
    }
    it("leaves a local in the frame while one of its name has the register") {
      regs = 2;
      check(compiles("int main() { int a = 1; a += 1; { int a = 5; a += 1; a += 1; } return a; }"));
      check(has("rop r0 + 1, move 5, stor -16:4,") && has("stor -16:4, rop acc = r0"));
    }
    it("shares a register between locals of one name in sibling scopes") {
      regs = 1;
      check(compiles("int main() { { int a = 1; a += 1; a += 1; } { long a = 5; a += 1; a += 1; } return 0; }"));
      check(has("rop r0 = 1, rop r0 + 1, rop r0 + 1, rop.w r0 = 5, rop.w r0 + 1, rop.w r0 + 1"));
    }
    it("compares and computes between registers where they are") {
      regs = 2;
      check(compiles("int main() { int a = 1, b = 2; while(a < b) a += b; return a == 2; }"));
      check(has("rop.t r0 < r1, cond !<, rop r0 + r1, loop rop, back cond, rop.t r0 == 2, flag =="));
    }
    it("tests a register against 0 in a condition") {
      regs = 2;
      check(compiles("int main() { int n = 3; while(n) n -= 1; return n; }"));
      check(has("rop.t r0 != 0, cond !!=, rop r0 - 1, loop rop") && calls[op_when] == 0);
    }
    it("goes through acc and tmp for the ops a register does not take") {
      regs = 2;
      check(compiles("int main() { int a = 6, b = 2; a = a * b; a = a << b; return a; }"));
      check(has("rop acc = r0, rop tmp = r1, calc *, rop r0 = acc, rop acc = r0, rop tmp = r1, calc <<, rop r0 = acc"));
    }
    it("masks a narrow local as its slot would hold it") {
      regs = 2;
      check(compiles("int main() { char c = 1; c += 2; return c; }"));
      check(has("rop r0 = 1, rop r0 & 255, rop r0 + 2, rop r0 & 255, rop acc = r0"));
    }
    it("sign extends an int into a long register, and moves a pointer by elements") {
      regs = 2;
      check(compiles("int main() { long a = 1; int b = 2; a += b; a += b; b += a; return b; }"));
      check(has("rop acc = r0, cast long, rop.w r1 + acc,") && has("rop r0 + r1, rop acc = r0"));
      regs = 2;
      check(compiles("int main() { int* q = 0; q += 2; q -= 1; return q == 0; }"));
      check(has("rop.w r0 = 0, rop.w r0 + 8, rop.w r0 - 4, rop.w.t r0 == 0"));
    }
    it("moves an argument from its register to the argument register") {
      regs = 2;
      check(compiles("int f(int a, int b) { return a; } int main() { int x = 1; x += 1; x += 1; return f(x, 7); }"));
      check(has("rop.w a1 = 7, rop a0 = r0, dial") && calls[op_push] == 0 && calls[op_pass] == 0);
    }
    it("stores a register or a constant through a pointer without a push") {
      regs = 2;
      check(compiles("int main() { int a[4]; int i = 0; while(i < 4) { a[i] = i; i += 1; } return a[2]; }"));
      check(has("calc.w +, push, rop tmp = r0, fill 4, rop r0 + 1") && left(op_push) == 0);
      check(compiles("int g; int main() { int* p = &g; *p = 5; return 0; }"));
      check(has("load -8:8, push, rop tmp = 5, fill 4, move 0") && left(op_push) == 0);
    }
    it("takes a & after a condition or a keyword as an address") {
      regs = 2;
      check(compiles("int main() { int a = 1; a += 2; a += 3; when(a) &a; return sizeof &a; }"));
      check(calls[op_rop] == 0);
      regs = 2;
      check(compiles("int main() { int a = 1; a += 2; a += 3; when(a) a += 1; else &a; return &a != 0; }"));
      check(calls[op_rop] == 0);
    }
    it("gives up past 64 names, the same code as without registers") {
      char src[4096] = "int main() { int s = 0;";
      for(int i = 0; i < 70; i++) sprintf(src + strlen(src), " int v%d = %d; s += v%d;", i, i, i);
      strcat(src, " s += 1; s += 2; return s; }");
      regs = 2;
      check(compiles(src));
      check(calls[op_stow] == 0 && calls[op_rop] == 0);
    }
    it("puts the lexer back for the pass, the lines too") {
      regs = 2;
      check(rejects("int main() {\n  int a = 1;\n  a += 2;\n  a += b;\n  return a;\n}", "main.c:4: no variable 'b'"));
    }
  }

  context("globals") {
    it("addresses a global through glob, patched later") {
      check(compiles("int g = 7; int main() { return g; }"));
      check(has("glob, read 4") && self.seats == 1);
      check(laid >= 4);
    }
    it("makes each pointer, array, function and bit-field type once") {
      check(compiles("int main() { return 0; }"));
      meta* t = find(&self, 0, sget(&self, "long"));
      u32i  n = self.defn;
      for(u32i i = 0; i < 5000; i++) knot(&self, many, t, 8 * (i + 1));
      check(self.defn == n + 5000 && knot(&self, many, t, 8) != href(&self, t));
      for(u32i i = 0; i < 5000; i++)
        if(knot(&self, many, t, 8 * (i + 1)) != pin(self.defs, n + i)) { check(0); break; }
      meta* f = mold(&self, t, 2, 1, 0);
      check(mold(&self, t, 2, 1, 0) == f && mold(&self, t, 2, 3, 0) != f && mold(&self, t, 2, 1, 1) != f);
      meta* b = nibs(&self, t, 3, 5);
      check(nibs(&self, t, 3, 5) == b && nibs(&self, t, 4, 5) != b && b->size == 8);
      check(href(&self, t) == href(&self, t) && self.defn == n + 5000 + 1 + 3 + 2); // arrays, long*, f..., b...
    }
    it("grows past any count of names, types, data, code, calls and breaks") {
      static char src[1 << 20];
      u32i n = 0;
      for(u32i i = 0; i < 1000; i++) // a name, an array type and i + 1 bytes each
        n += sprintf(src + n, "byte a%u[%u] = {%u};\n", i, i + 1, i % 100);
      n += sprintf(src + n, "long last = 19088743;\nint main() {\n  int t = 0;\n");
      for(u32i i = 0; i < 300; i++) n += sprintf(src + n, "  t = t + f();\n");
      for(u32i i = 0; i < 30000; i++) n += sprintf(src + n, "  t = t + 1;\n");
      n += sprintf(src + n, "  while(t) {\n");
      for(u32i i = 0; i < 100; i++) n += sprintf(src + n, "    when(t == %u) break;\n", i);
      sprintf(src + n, "  }\n  return t;\n}\nint f() { return 1; }\n");
      check(compiles(src));
      check(self.top > 1000 && self.defn >= 1000 && laid > 1000 * 1001 / 2 && tail8 == 19088743);
      check(self.text->body - self.text->head > 1 << 16 && calls[op_link] >= 300);
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
