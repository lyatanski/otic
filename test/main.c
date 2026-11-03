// vim: ts=2:sw=2
// tic's spec: each case is written to bins/ and compiled by the tic binary in
// $TIC (default bins/one) in a child process (fail() exits, code may crash),
// run in memory and as an executable written with `-e` (ELF, Mach-O
// or PE). Constant operands are folded, so the cases that test generated code
// take their operands from locals. It tests the target it is built for, which
// must be tic's: for arm8, cross-build both.
#include "test.h"
#include <stdbool.h> /* bool before C23 */

#include <fcntl.h>    /* open, O_BINARY */
#include <limits.h>   /* INT_MIN */
#include <stdlib.h>   /* getenv */
#include <sys/stat.h> /* chmod, mkdir */
#include <unistd.h>   /* read, close, dup2, access, unlink */
#ifdef _WIN32
#define WIN32_LEAN_AND_MEAN
#include <windows.h>  /* WaitForSingleObject, TerminateProcess */
#include <process.h>  /* _spawnv */
#include <direct.h>   /* _mkdir */
#define mkdir(path, mode) _mkdir(path)
#define pipe(fd) _pipe(fd, 256, O_BINARY)
#define NULL_DEV "NUL"
#define EXE ".exe"
#else
#include <sys/wait.h> /* waitpid */
#define NULL_DEV "/dev/null"
#define EXE ""
#endif

#ifdef __aarch64__
#define TAG "arm8"
#define pick(x86, arm) (arm)
#define WRITE "64"
#else
#define TAG "x86"
#define pick(x86, arm) (x86)
#define WRITE "1"
#endif
#ifdef __linux__ // syscall() is the Linux ABI
#define linux_it it
#else
#define linux_it xit
#endif
#if defined(__linux__) && defined(__x86_64__) // objects: x86-64 ELF only so far
#define elfs_it it
#else
#define elfs_it xit
#endif

enum { bad = INT_MIN, odd }; // compile error, no result (crash, timeout, ...)

static char src_at[64], exe_at[64];
static char* dirs_at[3]; // -p directories for the cases, NULL ended

// head (NULL ended) then a -p for each of dirs_at, in args
static char** with(char** args, char** head) {
  char** a = args;
  while(*head) *a++ = *head++;
  for(char** d = dirs_at; *d; d++) { *a++ = "-p"; *a++ = *d; }
  *a = NULL;
  return args;
}

// run args[0] with stdout on fd for at most 5s: its exit status, or -1 when
// it was killed
#ifdef _WIN32
static int  spawn(int fd, char** args) {
  fflush(NULL);
  int out = dup(1);
  dup2(fd, 1);
  HANDLE child = (HANDLE)_spawnv(_P_NOWAIT, args[0], (const char* const*)args);
  dup2(out, 1);
  close(out);
  if(child == INVALID_HANDLE_VALUE) return -1;
  if(WaitForSingleObject(child, 5000) != WAIT_OBJECT_0) TerminateProcess(child, ~0u);
  WaitForSingleObject(child, INFINITE);
  DWORD code = ~0u;
  GetExitCodeProcess(child, &code);
  CloseHandle(child);
  return code > 255? -1: (int)code; // a crash exits with its NTSTATUS
}
#else
static int  spawn(int fd, char** args) {
  fflush(NULL);
  pid_t pid = fork();
  if(!pid) {
    alarm(5); // survives execv
    dup2(fd, 1);
    execv(args[0], args);
    _exit(127);
  }
  int status;
  if(pid < 0 || waitpid(pid, &status, 0) < 0) return -1;
  return WIFEXITED(status)? WEXITSTATUS(status): -1;
}
#endif

static char* tic(void) {
  char* path = getenv("TIC");
  return path? path: "bins/one";
}

// `tic` alone: the value main returned, as printed on the last line
static int  jit(void) {
  int fd[2];
  if(pipe(fd)) return odd;
  char* args[16];
  int code = spawn(fd[1], with(args, (char*[]){tic(), src_at, NULL}));
  close(fd[1]);
  char out[256], *last = out;
  int n = read(fd[0], out, sizeof out - 1);
  close(fd[0]);
  if(code == 1) return bad;
  if(code || n <= 0) return odd;
  out[n] = 0;
  for(char* c = out; *c; c++)
    if(*c == '\n' && c[1]) last = c + 1;
  int value;
  return sscanf(last, "%d", &value) == 1? value: odd;
}

// `tic -e`: the written executable's exit code
static int  exe(void) {
  int null = open(NULL_DEV, O_WRONLY);
  unlink(exe_at);
  char* args[16];
  int code = spawn(null, with(args, (char*[]){tic(), "-e", exe_at, src_at, NULL}));
  close(null);
  if(code == 1) return access(exe_at, F_OK)? bad: odd; // and no executable
  if(code || chmod(exe_at, 0755)) return odd;
  code = spawn(1, (char*[]){exe_at, NULL});
  return code < 0? odd: code;
}

static char* shown(int value, char* buf) {
  if(value == bad) return "error";
  if(value == odd) return "no result";
  sprintf(buf, "%d", value);
  return buf;
}

// compile src both ways: NULL when exec gives run and -e gives want, else why
// not
static char* tried(char* src, int run, int want) {
  mkdir("bins", 0755);
  sprintf(src_at, "bins/" TAG "-%03zu.c", test_count);
  sprintf(exe_at, "bins/" TAG "-%03zu" EXE, test_count);
  FILE* fp = fopen(src_at, "wb");
  if(!fp) return "cannot write the case to bins/";
  fputs(src, fp);
  fclose(fp);

  static char why[64];
  char w[16], g[16];
  int got = jit();
  char* how = "exec";
  if(got == run) { got = exe(); how = "exe"; }
  else want = run;
  if(got == want) return NULL;
  snprintf(why, sizeof why, "%s: want %s, got " TEST_COLOR_FAIL("%s"),
           how, shown(want, w), shown(got, g));
  return why;
}

// expect(want, src): main returns want, or tic exits 1 when want is bad
#define expect(want, src) saw(want, want, src)
// only(want, src): exec rejects it, the executable from -e returns want
#define only(want, src) saw(bad, want, src)
// here(run, src): exec returns run, writing an executable rejects it (it
// cannot bind imports yet)
#define here(run, src) saw(run, bad, src)
#define saw(run, want, src) \
  if((error = tried(src, run, want))) location = "at " __FILE__ ":" TEST_STRING(__LINE__);

// a module at bins/name.c, beside the cases, for link "name"
static void module(char* name, char* src) {
  char at[64];
  snprintf(at, sizeof at, "bins/%s.c", name);
  FILE* fp = fopen(at, "wb");
  if(fp) { fputs(src, fp); fclose(fp); }
}

// bins/name.a, a member per source in src (NULL ended), each compiled by the
// host's cc without a stack protector (it calls libc) into bins/name-i.o: its
// path, or NULL when it could not be built. The sources are not .c files, so
// link "name-i" finds the object
static char* archive(char* name, char** src) {
  static char at[64];
  char cmd[512], run[640];
  int  n = snprintf(cmd, sizeof cmd, "ar rcs bins/%s.a", name);
  snprintf(at, sizeof at, "bins/%s.a", name);
  unlink(at);
  for(int i = 0; src[i]; i++) {
    snprintf(run, sizeof run, "bins/%s-%d.cin", name, i);
    FILE* fp = fopen(run, "wb");
    if(!fp) return NULL;
    fputs(src[i], fp);
    fclose(fp);
    snprintf(run, sizeof run, "cc -O2 -fPIC -fno-stack-protector -c -o bins/%s-%d.o -x c bins/%s-%d.cin", name, i, name, i);
    if(system(run)) return NULL;
    n += snprintf(cmd + n, sizeof cmd - n, " bins/%s-%d.o", name, i);
  }
  return system(cmd)? NULL: at;
}

// src as a library written by tic, bins/libname.so, or with -s (stat)
// bins/name.o, then the C program drv built with it by the host's cc (with
// cflags) and run: its exit code, bad when tic failed, odd when cc did
static int  library(char* name, char* src, bool stat, char* cflags, char* drv) {
  char at[64], out[64], cmd[512];
  snprintf(at, sizeof at, "bins/%s.c", name);
  snprintf(out, sizeof out, stat? "bins/%s.o": "bins/lib%s.so", name);
  FILE* fp = fopen(at, "wb");
  if(!fp) return odd;
  fputs(src, fp);
  fclose(fp);
  unlink(out);
  int null = open(NULL_DEV, O_WRONLY);
  int code = spawn(null, stat? (char*[]){tic(), "-s", "-e", out, at, NULL}
                             : (char*[]){tic(), "-e", out, at, NULL});
  close(null);
  if(code) return bad;
  snprintf(at, sizeof at, "bins/%s-drv.cin", name);
  if(!(fp = fopen(at, "wb"))) return odd;
  fputs(drv, fp);
  fclose(fp);
  snprintf(cmd, sizeof cmd, "cc %s -o bins/%s-drv -x c %s -x none %s -ldl -Wl,-rpath,'$ORIGIN' 2>" NULL_DEV,
           cflags, name, at, out);
  if(system(cmd)) return odd;
  snprintf(at, sizeof at, "bins/%s-drv", name);
  code = spawn(1, (char*[]){at, out, NULL});
  return code < 0? odd: code;
}

// 1 when clang and ld64.lld can build Mach-O programs here, else 0: its cases
// are skipped. x86-64 hosts only, so it runs once
static int  apple(void) {
  static int ok = -1;
#ifdef __x86_64__
  if(ok < 0) ok = !system("clang --version >" NULL_DEV " 2>&1 && ld64.lld --version >" NULL_DEV " 2>&1");
#else
  ok = 0;
#endif
  return ok;
}

// src as a Mach-O library written by tic for arch (ia64 or arm8),
// bins/libname.dylib, or with -s (stat) bins/name.o, then the C program drv
// compiled by clang and linked with it by ld64.lld against a stub of
// libSystem: 0 when it links, bad when tic failed, odd when the build did.
// Nothing here runs macOS code
static int  machlib(char* name, char* src, bool stat, char* arch, char* drv) {
  char  at[64], out[64], cmd[512];
  char* cpu = arch[0] == 'a'? "arm64": "x86_64";
  FILE* fp = fopen("bins/libSystem.tbd", "wb");
  if(!fp) return odd;
  fprintf(fp, "--- !tapi-tbd\ntbd-version: 4\ntargets: [ %s-macos ]\n"
              "install-name: '/usr/lib/libSystem.B.dylib'\nexports:\n  - targets: [ %s-macos ]\n"
              "    symbols: [ dyld_stub_binder, _labs, _printf ]\n...\n", cpu, cpu);
  fclose(fp);
  snprintf(at, sizeof at, "bins/%s.c", name);
  snprintf(out, sizeof out, stat? "bins/%s.o": "bins/lib%s.dylib", name);
  if(!(fp = fopen(at, "wb"))) return odd;
  fputs(src, fp);
  fclose(fp);
  unlink(out);
  int null = open(NULL_DEV, O_WRONLY);
  int code = spawn(null, stat? (char*[]){tic(), "-f", "mach", "-a", arch, "-s", "-e", out, at, NULL}
                             : (char*[]){tic(), "-f", "mach", "-a", arch, "-e", out, at, NULL});
  close(null);
  if(code) return bad;
  snprintf(at, sizeof at, "bins/%s-drv.cin", name);
  if(!(fp = fopen(at, "wb"))) return odd;
  fputs(drv, fp);
  fclose(fp);
  snprintf(cmd, sizeof cmd, "clang -target %s-apple-macos11 -c -o bins/%s-drv.o -x c %s 2>" NULL_DEV
           " && ld64.lld -arch %s -platform_version macos 11.0 11.0 -o bins/%s-drv bins/%s-drv.o %s"
           " bins/libSystem.tbd >" NULL_DEV " 2>&1", cpu, name, at, cpu, name, name, out);
  return system(cmd)? odd: 0;
}

// 25-statement bodies: their branches are over 127 bytes on x86
#define RET5 "return 1; return 1; return 1; return 1; return 1; "
#define ADD5 "s += 1; s += 1; s += 1; s += 1; s += 1; "
#define ARR8 "{ int a[2]; } { int a[2]; } { int a[2]; } { int a[2]; } "\
             "{ int a[2]; } { int a[2]; } { int a[2]; } { int a[2]; } "
#define ARR64 ARR8 ARR8 ARR8 ARR8 ARR8 ARR8 ARR8 ARR8

spec(TAG) {
  context("declarations") {
    it("returns a constant")
      expect(42, "int main() {\n  return 42;\n}\n");
    it("returns 0 when main falls off the end")
      expect(0, "int main() {\n}\n");
    it("enters at main, not the first function")
      expect(2, "int f() { return 1; }\n\nint main() {\n  return 2;\n}\n");
    it("skips parameters")
      expect(3, "int f1(int a) {\n}\n\nint f2(long b, char c) {\n}\n\n"
                "int main() {\n  return 3;\n}\n");
    it("skips arrays")
      expect(6, "int arr[8];\nint main() { return 6; }\n");
    it("skips structs")
      expect(1, "struct point { int x; int y; };\nstruct { int z; };\n"
                "int main() { return 1; }\n");
  }

  context("lexer") {
    it("skips comments")
      expect(42, "/* block\n   comment */\n"
                 "int main() { // line comment\n  return /* inline */ 0x2a;\n}\n"
                 "// trailing comment without newline");
    it("reads binary numbers")
      expect(5, "int main() { return 0b101; }\n");
    it("reads octal numbers")
      expect(15, "int main() { return 0o17; }\n");
  }

  context("when") {
    it("takes a true branch")
      expect(4, "int main() {\n  when(3) return 4;\n  else return 2;\n  return 42;\n}\n");
    it("takes a false branch")
      expect(2, "int main() {\n  when(0) return 4;\n  else return 2;\n}\n");
    it("nests")
      expect(7, "int main() {\n  when(1) {\n    when(0) return 5;\n    return 7;\n  }\n"
                "  return 9;\n}\n");
    it("tests an expression")
      expect(7, "int main() {\n  when(2 > 1 && 3) return 7;\n  return 9;\n}\n");
  }

  context("expressions folded") {
    it("honours precedence")
      expect(9, "int main() { return 2 + 3 * 4 - 10 / 2; }\n");
    it("associates left")
      expect(58, "int main() { return 100 - 20 - 30 + 64 / 4 / 2; }\n");
    it("groups with parentheses")
      expect(15, "int main() { return (2 + 3) * (4 - 1); }\n");
    it("truncates division toward zero") // -7 / 2 == -3, -7 % 3 == -1
      expect(8, "int main() { return 17 % 5 + -7 / 2 + 10 + -7 % 3; }\n");
    it("encodes wide immediates")
      expect(10, "int main() { return 100000 * 3 - 299990; }\n");
    it("computes bitwise operators and shifts")
      expect(20, "int main() { return ((0xf0 | 0x0f) ^ (0xff & 0x3c)) - 190"
                 " + (1 << 4 | 3) + (-16 >> 2); }\n");
    it("compares")
      expect(173, "int main() {\n"
                  "  return (1 < 2) + (2 < 1) * 2 + (3 <= 3) * 4 + (4 > 3) * 8\n"
                  "       + (3 >= 4) * 16 + (5 == 5) * 32 + (5 != 5) * 64"
                  " + (-1 < 0) * 128;\n}\n");
    it("computes && and ||")
      expect(53, "int main() {\n"
                 "  return (2 && 3) + (0 && 1) * 2 + (0 || 5) * 4 + (0 || 0) * 8\n"
                 "       + (1 && 0 || 1) * 16 + (1 || 0 && 0) * 32;\n}\n");
    it("short-circuits") // the right side must not run: 1 / 0 traps on x86-64
      expect(2, "int main() { return (0 && 1 / 0) + (1 || 1 / 0) + 1; }\n");
    it("computes unary operators")
      expect(5, "int main() { return -(-5) + ~0 + !0 + !7; }\n");
    it("wraps at 32 bits when folding, like the target")
      expect(15, "int main() { return (0x7fffffff + 1 < 0) + (0x10000 * 0x10000 == 0) * 2"
                 " + (1 << 33 == 2) * 4 + 8; }\n");
  }

  context("expressions at run time") {
    it("honours precedence")
      expect(9, "int main() { int a = 2, b = 3, c = 4, d = 10; return a + b * c - d / a; }\n");
    it("associates left")
      expect(58, "int main() { int h = 100, s = 64; return h - 20 - 30 + s / 4 / 2; }\n");
    it("groups with parentheses")
      expect(15, "int main() { int a = 2, b = 3, c = 4; return (a + b) * (c - 1); }\n");
    it("truncates division toward zero")
      expect(8, "int main() { int a = 17, m = -7, two = 2;"
                " return a % 5 + m / two + 10 + m % 3; }\n");
    it("encodes wide immediates")
      expect(10, "int main() { int a = 100000; return a * 3 - 299990; }\n");
    it("computes bitwise operators and shifts")
      expect(20, "int main() { int f0 = 0xf0, ff = 0xff, one = 1, four = 4, m16 = -16;\n"
                 "  return ((f0 | 0x0f) ^ (ff & 0x3c)) - 190 + (one << four | 3) + (m16 >> 2); }\n");
    it("compares")
      expect(173, "int main() {\n  int x1 = 1, x3 = 3, x5 = 5, m = -1;\n"
                  "  return (x1 < 2) + (2 < x1) * 2 + (x3 <= x3) * 4 + (4 > x3) * 8\n"
                  "       + (x3 >= 4) * 16 + (x5 == 5) * 32 + (x5 != x5) * 64"
                  " + (m < 0) * 128;\n}\n");
    it("computes && and ||")
      expect(53, "int main() {\n  int z = 0, one = 1, two = 2, three = 3, five = 5;\n"
                 "  return (two && three) + (z && one) * 2 + (z || five) * 4 + (z || z) * 8\n"
                 "       + (one && z || one) * 16 + (one || z && z) * 32;\n}\n");
    it("short-circuits") // the right side must not run: x / 0 traps on x86-64
      expect(2, "int main() { int z = 0, one = 1; return (z && one / z) + (one || one / z) + 1; }\n");
    it("computes unary operators")
      expect(5, "int main() { int five = 5, z = 0, seven = 7; return -(-five) + ~z + !z + !seven; }\n");
    it("wraps at 32 bits, like folding")
      expect(63, "int main() {\n  int big = 0x7fffffff, k = 0x10000, one = 1;\n"
                 "  return (big + 1 < 0) + (k * k == 0) * 2 + (one << 33 == 2) * 4\n"
                 "       + (0x7fffffff + 1 < 0) * 8 + (0x10000 * 0x10000 == 0) * 16"
                 " + (1 << 33 == 2) * 32;\n}\n");
  }

  context("conditions and kept operands") {
    it("branches on each comparison, of ints and longs")
      expect(237, "int main() {\n  int a = 1, b = 2, r = 0;\n  long x = -1, one = 1, y = one << 32;\n"
                  "  when(a < b) r += 1; when(a > b) r += 2; when(a <= a) r += 4; when(b >= a) r += 8;\n"
                  "  when(a == b) r += 16; when(a != b) r += 32; when(x < y) r += 64; when(y > one) r += 128;\n"
                  "  return r;\n}\n");
    it("loops on a comparison of longs")
      expect(4, "int main() {\n  long one = 1, i = one << 32, n = i + 4; int c = 0;\n"
                "  while(i < n) { i += 1; c += 1; }\n  return c;\n}\n");
    it("branches on comparisons joined by && and ||")
      expect(14, "int main() {\n  int a = 1, b = 2, r = 0;\n"
                 "  when(a < b && b < a) r += 1; when(a < b || b < a) r += 2;\n"
                 "  when(a > b || a == 1) r += 4; when(!(a > b) && (a < b) == 1) r += 8;\n  return r;\n}\n");
    it("uses comparisons as values")
      expect(6, "int main() { int a = 1, b = 2; int c = a < b; return c + (b > a) * 2 + (a != b) * 3 + (a == b) * 4; }\n");
    it("nests kept operands past the scratch registers")
      expect(120, "int main() {\n  int a = 1;\n"
                  "  return (a + 1) * ((a + 2) * ((a + 3) * ((a + 4) * ((a + 6) - (a + 5)))));\n}\n");
    it("keeps operands across calls")
      expect(33, "int f(int x) { int a = x + 1; return (a + 1) * (a - 1); }\n"
                 "int main() {\n  int a = 1;\n"
                 "  return (a + 1) * f(a + 2) + ((a + 2) - f(a)) * (a + 1) + f(a) * (a * 2 - 1);\n}\n");
    it("keeps 64-bit operands whole")
      expect(2, "int main() { long one = 1, x = one << 32; long y = (x + 1) * ((x >> 32) + 1); return y >> 32; }\n");
    it("passes a computed last argument")
      expect(13, "int sub(int a, int b) { return a - b; }\n"
                 "int main() { int a = 3; return sub(30, (a + 1) * (a + 2) - 3); }\n");
  }

  context("locals") {
    it("declares, assigns and reads locals")
      expect(8, "int main() {\n  int a = 2;\n  int b;\n  b = a * 3;\n  return a + b;\n}\n");
    it("declares several in one statement")
      expect(7, "int main() { int a = 3, b, c = a + 1; b = c; return a + b; }\n");
    it("computes compound assignments")
      expect(3, "int main() {\n  int a = 10;\n"
                "  a += 5; a -= 3; a *= 2; a /= 4; a %= 4;\n"
                "  a <<= 3; a >>= 1; a |= 1; a &= 7; a ^= 2;\n  return a;\n}\n");
    it("chains assignments to the right")
      expect(8, "int main() { int a; int b; a = b = 4; return a + b; }\n");
    it("uses locals on either side of an operator")
      expect(13, "int main() { int a = 7; int b = 2; return a - b + (b - a) * -1 + 3 - b * 0 / a; }\n");
    it("shadows and restores in nested scopes")
      expect(57, "int main() {\n  int r = 0;\n  int a = 1;\n"
                 "  { int a = 2; { int a = 3; r = a; } r = r * 4 + a; }\n"
                 "  return r * 4 + a;\n}\n");
  }

  context("calls") {
    it("passes parameters")
      expect(7, "int sub(int a, int b) { return a - b; }\n"
                "int main() { return sub(10, 3); }\n");
    it("passes six parameters")
      expect(91, "int f(int a, int b, int c, int d, int e, int g) {\n"
                 "  return a + 2 * b + 3 * c + 4 * d + 5 * e + 6 * g;\n}\n"
                 "int main() { return f(1, 2, 3, 4, 5, 6); }\n");
    it("calls a function defined later")
      expect(9, "int main() { return twice(4) + 1; }\nint twice(int x) { return x + x; }\n");
    it("recurses")
      expect(144, "int fib(int n) {\n  when(n < 2) return n;\n"
                  "  return fib(n - 1) + fib(n - 2);\n}\n"
                  "int main() { return fib(12); }\n");
    it("keeps partial results across calls")
      expect(37, "int sq(int x) { return x * x; }\n"
                 "int main() { return sq(3) + sq(4) * 2 - sq(2); }\n");
    it("nests calls in arguments")
      expect(14, "int sq(int x) { return x * x; }\nint add(int a, int b) { return a + b; }\n"
                 "int main() { return add(sq(2), add(1, sq(3))); }\n");
    it("calls for effect and returns from void")
      expect(3, "void f() { return; }\nint main() { f(); ; return 3; }\n");
    it("drops a dead call without breaking the code after it")
      expect(5, "int main() { int a = 0 && g(); return a + 5; }\nint g() { return 1; }\n");
    it("sign extends int arguments to long parameters")
      expect(3, "long f(long a) { return a < 0; }\n"
                "int main() { int n = -1; return f(n) + f(-1) * 2; }\n");
    it("sign extends an int parameter where the callee widens it")
      expect(3, "int f(int a) { long x = a; return (x < 0) + (x == -1) * 2; }\n"
                "int main() { int n = -1; return f(n); }\n");
    it("sign extends int arguments in a forward call")
      expect(1, "int main() { int n = -1; return f(n); }\nint f(long a) { return a < 0; }\n");
    it("converts the returned value to the return type")
      expect(3, "long f() { int n = -1; return n; }\nint g() { long v = 0x100000000; return v; }\n"
                "int main() { long x = f(); return (x < 0) + !g() * 2; }\n");
  }

  context("loops") {
    it("runs while")
      expect(55, "int main() {\n  int s = 0;\n  int i = 1;\n"
                 "  while(i <= 10) { s += i; i += 1; }\n  return s;\n}\n");
    it("has no for")
      expect(bad, "int main() { int i = 0; for(;;) { i += 1; when(i == 9) break; } return i; }\n");
    it("skips to the condition of a while")
      expect(18, "int main() {\n  int i = 0; int s = 0;\n"
                 "  while(i < 10) { i += 1; when(i % 3) skip; s += i; }\n  return s;\n}\n");
    it("breaks the inner loop only")
      expect(6, "int main() {\n  int n = 0; int i = 0;\n  while(i < 3) {\n    int j = 0;\n"
                "    while(1) { when(j == 2) break; j += 1; n += 1; }\n    i += 1;\n  }\n"
                "  return n;\n}\n");
    it("jumps over 127 bytes")
      expect(1, "int main() {\n  when(1) { " RET5 RET5 RET5 RET5 RET5 "}\n  return 0;\n}\n");
    it("loops back over 127 bytes")
      expect(50, "int main() {\n  int s = 0;\n  int i = 0;\n"
                 "  while(i < 2) { " ADD5 ADD5 ADD5 ADD5 ADD5 "i += 1; }\n  return s;\n}\n");
  }

  context("globals") {
    it("reads and writes a global")
      expect(6, "int g;\nint main() { g = 5; return g + 1; }\n");
    it("initializes globals")
      expect(201, "int a = 3;\nlong b = -2;\nchar c = 200;\nint main() { return a + b + c; }\n");
    it("shares globals between functions")
      expect(3, "int n;\nvoid inc() { n += 1; }\nint main() { inc(); inc(); inc(); return n; }\n");
    it("initializes global arrays, the rest is zero")
      expect(81, "int t[4] = {1, 2, 3};\n"
                 "int main() { return t[0] + t[1] * 10 + t[2] * 20 + t[3]; }\n");
    it("zeroes bss")
      expect(4, "int z[100];\nint main() {\n  int s = 0;\n"
                "  int i = 0;\n  while(i < 100) { s += z[i]; i += 1; }\n  return s + 4;\n}\n");
    it("keeps a large bss")
      expect(7, "char big[1000000];\nint main() { big[999999] = 7; return big[999999] + big[0]; }\n");
    it("declares several in one statement")
      expect(9, "int a = 2, *p, b[2] = {3, 4};\nint main() { p = &a; return *p + b[0] + b[1]; }\n");
  }

  context("pointers") {
    it("takes addresses and dereferences")
      expect(7, "int main() { int a = 3; int* p = &a; *p = *p + 4; return a; }\n");
    it("passes pointers to functions")
      expect(9, "void set(int* p, int v) { *p = v; }\nint main() { int a; set(&a, 9); return a; }\n");
    it("points to pointers")
      expect(5, "int main() { int a = 1; int* p = &a; int** q = &p; **q = 5; return a; }\n");
    it("assigns through pointers with op=")
      expect(14, "int g = 3;\nint main() { int* p = &g; *p += 4; g *= 2; return *p; }\n");
    it("scales pointer arithmetic")
      expect(17, "int main() {\n  long t[3];\n  long* p = t;\n  *(p + 2) = 7;\n  p = p + 1;\n"
                 "  return t[2] + (p - t) * 10;\n}\n");
    it("indexes with a variable through a pointer")
      expect(41, "int main() {\n  long t[4];\n  long* p = t;\n  int i = 3;\n  p[i] = 40;\n"
                 "  p += 1; i -= 1;\n  return p[i] + (&t[3] - p) - 1;\n}\n");
    it("compares pointers")
      expect(5, "int main() {\n  int a[2];\n  int* p = a;\n  int* q = &a[1];\n"
                "  return (p < q) + (p == q) * 2 + (q != 0) * 4 + !p * 8;\n}\n");
    it("loads and stores 1, 2, 4 and 8 bytes")
      expect(41, "int main() {\n  long v = 0;\n  int* i = &v; i[1] = 1;\n  char* c = &v; c[0] = 3;\n"
                 "  nu16* h = &v; h[1] = 2;\n"
                 "  return (v >> 32) + (v >> 16 & 0xff) * 2 + c[0] * 4 + h[0] * 8;\n}\n");
    it("steps a void pointer by bytes")
      expect(9, "int g;\nint main() { void* p = &g; void* q = p + 4; q -= 1; return (q - p) * 3; }\n");
    it("truncates the value of an assignment to the target")
      expect(44, "int main() { char c = 0; int n = 300; int x = (c = n); return x; }\n");
    it("sign extends an int to a long")
      expect(3, "int main() { long a = -1; int b = -2; long c = a + b; return (c == -3) + (a < 0) * 2; }\n");
  }

  context("arrays") {
    it("indexes local arrays")
      expect(20, "int main() {\n  int a[5];\n  int i = 0;\n  while(i < 5) { a[i] = i * i; i += 1; }\n"
                 "  return a[2] + a[4];\n}\n");
    it("initializes local arrays, the rest is zero")
      expect(11, "int main() { int a[4] = {5, 6}; return a[0] + a[1] + a[2] + a[3]; }\n");
    it("zeroes the rest of nested arrays")
      expect(0, "void f() { int a[2][3]; a[1][2] = 77; a[0][1] = 55; }\n"
                "int g() { int a[2][3] = {}; return a[1][2] + a[0][1]; }\n"
                "int main() { f(); return g(); }\n");
    it("initializes nested local arrays")
      expect(13, "int main() { int a[3][2] = {{1, 2}, {3}}; int i = 0; int s = 0;\n"
                 "  while(i < 3) { s += a[i][0] * 2 + a[i][1]; i += 1; }\n  return s + a[2][1] + 3; }\n");
    it("reuses array types")
      expect(0, "int main() { " ARR64 ARR64 ARR64 ARR64 ARR64 ARR64 ARR64 ARR64 ARR64 "return 0; }\n");
    it("nests arrays")
      expect(13, "int g[2][3];\nint main() {\n  int a[2][3];\n  a[1][2] = 4;\n  g[1][0] = 5;\n"
                 "  int* p = a;\n  return a[1][2] + g[1][0] + p[5];\n}\n");
    it("passes arrays to functions")
      expect(6, "int sum(int* a, int n) {\n  int s = 0;\n"
                "  int i = 0;\n  while(i < n) { s += a[i]; i += 1; }\n  return s;\n}\n"
                "int main() { int a[3] = {1, 2, 3}; return sum(a, 3); }\n");
    it("reaches locals past a large array")
      expect(7, "int main() { int a[100]; int b = 3; a[99] = 4; return a[99] + b; }\n");
  }

  context("strings") {
    it("puts strings in data")
      expect(5, "int len(char* s) { int n = 0; while(s[n]) n += 1; return n; }\n"
                "int main() { return len(\"hello\") + len(\"\"); }\n");
    it("decodes escapes")
      expect(243, "int main() {\n  char* s = \"a\\n\\t\\\\\\\"\\0b\";\n"
                  "  return s[1] + s[2] + s[3] + s[4] + s[5] + s[6];\n}\n");
    it("initializes char arrays")
      expect(97, "char s[8] = \"abc\";\nint main() { return s[0] + s[3] + s[7]; }\n");
    it("initializes local char arrays, the rest is zero")
      expect(199, "void f() { char s[12]; int i = 0; while(i < 12) { s[i] = 9; i += 1; } }\n"
                  "int main() { f(); char s[12] = \"abcdefg\"; return s[0] + s[6] + s[7] + s[11] - 1; }\n");
  }

  context("structs") {
    it("aligns members")
      expect(6, "struct point { char c; int x; long y; };\npoint p;\n"
                "int main() { p.c = 1; p.x = 2; p.y = 3; return p.c + p.x + p.y; }\n");
    it("uses struct locals and pointers")
      expect(7, "struct pair { int a; int b; };\nint sum(pair* p) { return p->a + p->b; }\n"
                "int main() { struct pair q; q.a = 3; q.b = 4; return sum(&q); }\n");
    it("links nodes")
      expect(3, "struct node { int v; node* next; };\nint main() {\n  node a; node b;\n"
                "  a.v = 1; b.v = 2; a.next = &b; b.next = 0;\n  int s = 0;\n"
                "  node* n = &a;\n  while(n) { s += n->v; n = n->next; }\n  return s;\n}\n");
    it("nests structs and arrays")
      expect(12, "struct in { int t[3]; };\nstruct out { char c; in i[2]; };\nout g;\n"
                 "int main() { out* p = &g; g.i[1].t[2] = 9; p->c = 3; return p->i[1].t[2] + g.c; }\n");
    it("initializes struct locals, the rest is zero")
      expect(23, "struct p { char c; int x; long y; };\n"
                 "void f() { p v; v.c = 9; v.x = 9; v.y = 9; }\n"
                 "int main() { f(); p v = {2, 3}; return v.c * 10 + v.x + v.y; }\n");
    it("initializes struct globals")
      expect(123, "struct p { char c; int x; long y; };\np g = {1, 2, 3};\n"
                  "int main() { return g.c * 100 + g.x * 10 + g.y; }\n");
    it("initializes members by name, the rest is zero")
      expect(21, "struct p { char c; int x; long y; };\n"
                 "void f() { p v; v.c = 9; v.x = 9; v.y = 9; }\n"
                 "int main() { f(); p v = {.x = 2, 1}; return v.c + v.x * 10 + v.y; }\n");
    it("initializes global members by name")
      expect(30, "struct p { char c; int x; long y; };\np g = {.y = 3};\n"
                 "int main() { return g.c + g.x + g.y * 10; }\n");
    it("initializes elements by index")
      expect(46, "int main() { int a[5] = {[3] = 4, 6, [0] = 1};\n"
                 "  return a[0] * 0 + a[3] * 10 + a[4] + a[1] + a[2]; }\n");
    it("follows designator chains")
      expect(123, "struct in { int a; int b; };\nstruct out { int k; in i[2]; };\n"
                  "out g = {.i[1].a = 2, 3, .k = 1};\n"
                  "int main() { out o = {.i[1].a = 2, 3, .k = 1};\n"
                  "  return (o.k * 100 + o.i[1].a * 10 + o.i[1].b) * (g.k * 100 + g.i[1].a * 10 + g.i[1].b == 123); }\n");
    it("initializes nested structs and arrays without braces")
      expect(237, "struct in { int a; int b; };\nstruct out { int k; in i[2]; };\n"
                   "int main() { out o = {1, 2, 3, 4};\n"
                   "  return o.i[0].a * 100 + o.i[0].b * 10 + o.i[1].a * 2 + o.i[1].b - o.k; }\n");
    it("initializes nested structs and arrays in braces")
      expect(237, "struct in { int a; int b; };\nstruct out { int k; in i[2]; };\n"
                   "int main() { out o = {1, {{2, 3}, {.a = 4},},};\n"
                   "  return o.i[0].a * 100 + o.i[0].b * 10 + o.i[1].a * 2 + o.i[1].b - o.k; }\n");
    it("sets a braced member again as a whole")
      expect(10, "struct in { int a; int b; };\nstruct out { int k; in i[2]; };\n"
                 "out g = {.i[0].b = 5, .i[0] = {.a = 1}};\n"
                 "int main() { out o = {.i[0].b = 5, .i[0] = {.a = 1}};\n"
                 "  return (o.i[0].a * 10 + o.i[0].b) * (g.i[0].a == 1 && !g.i[0].b); }\n");
    it("lays out and reads bit-fields")
      expect(29, "struct s { byte b:1, x:1, r:1, w:1, _:4; };\n"
                 "int main() { s v = {.x = 1, .w = 1}; byte* p = &v; v._ = 1;\n"
                 "  return *p + v.x + v.w * 2 + v.b + v.r; }\n");
    it("sign extends int bit-fields, not narrower ones")
      expect(7, "struct s { int a:3; int b:5; byte c:2; long d:4; };\n"
                "int main() { s v = {-3, 0, 3, 0}; v.b = 17; v.d = 15; v.c = v.c + 1;\n"
                "  return (v.a == -3) + (v.b == -15) * 2 + (v.d == -1) * 4 + v.c * 8; }\n");
    it("writes bit-fields through pointers and globals, with op=")
      expect(40, "struct s { int a:4; int b:12; nu16 c:3; };\ns g = {1, 2, 3};\n"
                 "void set(s* p) { p->b += 30; p->a = p->a * 3; p->c = (p->b = p->b + 1) + 2; }\n"
                 "int main() { set(&g); nu16* h = &g; return g.b + g.a + g.c + (h[0] == 0x213); }\n");
    it("puts a bit-field in the next unit when it would cross one")
      expect(14, "struct s { int a:30; int b:4; char c; int :0; char d; };\n"
                 "int main() { s v; byte* p = &v; int i = 0; while(i < 12) { p[i] = 0; i += 1; }\n"
                 "  v.b = 7; v.c = 2; v.d = 5; return p[4] + p[5] + p[8]; }\n");
    it("crosses 32 bits in a long bit-field")
      expect(7, "struct s { int a:10; long c:40; long d:30; };\n"
                "int main() { long one = 1; s v = {3, 0, -5}; v.c = (one << 38) + 9;\n"
                "  long* p = &v; v.a = -1;\n"
                "  return (v.c == (one << 38) + 9) + (v.d == -5) * 2 + (p[0] >> 10 == v.c) * 4; }\n");
    it("uses anonymous struct types")
      expect(21, "struct { int x; int y; } pt = {.y = 4};\n"
                 "int main() { struct { int a; int b; } q = {2, 3};\n"
                 "  return pt.y + q.a * q.b + (struct { int a; int b; }){5, 11}.b + pt.x; }\n");
    it("gives the members of an anonymous struct member to its struct")
      expect(31, "struct o { char k; struct { int a; byte b:2, c:3; }; long z; };\n"
                 "o g = {1, 2, 3, 4, 5};\n"
                 "int main() { o v = {.c = 7, .a = 9}; return g.k + g.a + g.b + g.c + g.z + v.a + v.c + v.k + v.z; }\n");
    it("defines a struct in a function and with its declarators")
      expect(23, "struct n { int v; n* next; } n1 = {3};\n"
                 "int main() { struct w { int a; } wv = {11}; struct p { int b; }; p v; v.b = 9; return wv.a + v.b + n1.v; }\n");
    it("makes compound literals")
      expect(16, "struct p { int a; int b; };\nint sum(p* q) { return q->a + q->b; }\n"
                 "int main() { return (p){3, 4}.a + (p){.b = 4}.b + sum(&(p){2, 7}); }\n");
    it("makes compound literals of values and arrays")
      expect(11, "int main() { int x = 3; return x * (int){2} + (int[3]){1, 5, 3}[1]; }\n");
    it("keeps a compound literal to the end of its block")
      expect(12, "struct p { int a; int b; };\nint main() { p* q; q = &(p){5, 7};\n"
                 "  int x = 9; int y = 9; int z[4] = {9, 9, 9, 9}; return q->a + q->b; }\n");
    it("initializes values and strings in braces")
      expect(105, "int main() { int x = {7}; char s[4] = {\"abc\",}; return x + s[1]; }\n");
    it("initializes char array members with strings")
      expect(205, "struct s { char n[4]; int v; };\ns t[2] = {{\"ab\", 1}, {.v = 9}};\n"
                  "int main() { s x = {\"ab\", 1}; return x.n[0] + x.n[1] + x.n[3] + t[1].v + t[1].n[0] + t[0].v; }\n");
  }

  context("function pointers") {
    it("calls through a local, a global and a parameter")
      expect(17, "int add(int a, int b) { return a + b; }\nint mul(int a, int b) { return a * b; }\n"
                 "int (*g)(int, int);\nint apply(int (*f)(int, int), int a, int b) { return f(a, b); }\n"
                 "int main() { int (*f)(int, int) = add; g = mul; int x = 2;\n"
                 "  return f(x, 3) + g(x, 3) + apply(add, x, 4); }\n");
    it("takes the address with & and calls through *")
      expect(9, "int sq(int a) { return a * a; }\n"
                "int main() { int (*f)(int) = &sq; int (**p)(int) = &f; int x = 3; return (*f)(x) - (**p)(x) + (*p)(x); }\n");
    it("calls through struct members and arrays")
      expect(37, "int add(int a, int b) { return a + b; }\nint mul(int a, int b) { return a * b; }\n"
                 "long neg(long x) { return 0 - x; }\n"
                 "struct ops { int (*bin)(int, int); long (*un)(long); };\n"
                 "int main() { ops o = {add, neg}; ops* p = &o; int (*t[2][2])(int, int) = {{add, mul}, {mul, add}};\n"
                 "  int i = 1; long v = -5; p->bin = mul;\n"
                 "  return o.bin(4, 5) + p->un(v) + t[i][0](i, 3) + t[0][i](i, 9); }\n");
    it("passes and returns 64-bit values")
      expect(3, "long half(long x) { return x / 2; }\n"
                "int main() { long (*f)(long) = half; long one = 1; return (f(one << 40) == one << 39) + (f(-6) == -3) * 2; }\n");
    it("recurses through a pointer to itself")
      expect(120, "int fact(int n) { int (*me)(int) = fact; when(n < 2) return 1; return n * me(n - 1); }\n"
                  "int main() { int n = 5; return fact(n); }\n");
    it("compares pointers to functions")
      expect(7, "int f() { return 0; }\nint h() { return 0; }\n"
                "int main() { int (*p)() = f; int (*z)(); z = 0; return (p == f) + (p != h) * 2 + !z * 4 - (p == h); }\n");
    it("keeps a left operand across a call through a pointer")
      expect(26, "int two(int a) { return a * 2; }\n"
                 "int main() { int (*f)(int) = two; int x = 5; return x * 3 + f(x - 1) * f(x - 4) / 2 + (x + f(x)) - 12; }\n");
    it("calls an import through a pointer")
      here(7, "load \"c\";\nlong labs(long n);\nint main() { long (*f)(long) = labs; long v = -7; return f(v); }\n");
    it("calls a variadic import through a pointer")
      here(5, "int printf(char* f, ...);\nint main() { int (*p)(char*, ...) = printf; int v = 5; return p(\"%d\\n\", v) + 3; }\n");
    it("gives an otic function to C as a callback")
      here(49, "load \"c\";\nvoid qsort(void* b, long n, long s, int (*)(void*, void*));\n"
               "int cmp(void* a, void* b) { int* x = a; int* y = b; return *x - *y; }\n"
               "int main() { int a[4] = {4, 1, 3, 2}; qsort(a, 4, 4, cmp); return a[0] + a[1] * 2 + a[2] * 4 + a[3] * 8; }\n");
    it("rejects a call with the wrong argument count")
      expect(bad, "int f(int a) { return a; }\nint main() { int (*g)(int) = f; return g(1, 2); }\n");
    it("rejects arithmetic on a pointer to a function")
      expect(bad, "int f(int a) { return a; }\nint main() { int (*g)(int) = f; g = g + 1; return 0; }\n");
    it("rejects a call through what is not a function")
      expect(bad, "int main() { int x = 1; return x(1); }\n");
    it("rejects the address of a function not defined yet")
      expect(bad, "int main() { int (*g)(int) = h; return 0; }\nint h(int a) { return a; }\n");
    it("rejects an assignment to a function")
      expect(bad, "int f(int a) { return a; }\nint main() { int (*g)(int) = f; *g = 0; return 0; }\n");
    it("rejects a pointer to a function returning an array")
      expect(bad, "int (*g)(int)[3];\nint main() { return 0; }\n");
  }

  context("link") {
    it("calls a linked function") {
      module("add", "int add(int a, int b) { return a + b; }\n");
      expect(5, "link \"add\";\nint main() { return add(2, 3); }\n");
    }
    it("uses a linked global and struct") {
      module("pt", "struct pt { int x; int y; };\npt org;\nint k = 4;\n");
      expect(7, "link \"pt\";\nint main() { pt* p = &org; p->y = k; return p->y + 3; }\n");
    }
    it("links a module once") { // a second parse would define three() twice
      module("one-c", "int three() { return 3; }\n");
      module("one-a", "link \"one-c\";\nint a() { return three(); }\n");
      module("one-b", "link \"one-c\";\nint b() { return three() * 2; }\n");
      expect(9, "link \"one-a\";\nlink \"one-b\";\nint main() { return a() + b(); }\n");
    }
    it("links modules in a cycle") {
      module("cyc-a", "link \"cyc-b\";\nint a(int n) { when(n) return b(n - 1) + 2; return 0; }\n");
      module("cyc-b", "link \"cyc-a\";\nint b(int n) { when(n) return a(n - 1) + 1; return 0; }\n");
      expect(6, "link \"cyc-a\";\nint main() { return a(4); }\n");
    }
    it("finds a module beside the one that links it") {
      mkdir("bins/sub", 0755);
      module("sub/out", "link \"in\";\nint out() { return in() + 1; }\n");
      module("sub/in", "int in() { return 1; }\n");
      expect(2, "link \"sub/out\";\nint main() { return out(); }\n");
    }
    it("keeps static names in their module") {
      module("st-a", "static int n = 1;\nint a() { return get() + n; }\n"
                     "static int get() { return 1; }\n");
      module("st-b", "static int n = 2;\nstatic int get() { return 2; }\n"
                     "int b() { return get() + n; }\n");
      expect(34, "link \"st-a\";\nlink \"st-b\";\nint n = 4;\nint get() { return 8; }\n"
                 "int main() { return a() + b() * 3 + get() * 2 + n; }\n");
    }
    it("shows static names again after a link") {
      module("add", "int add(int a, int b) { return a + b; }\n");
      expect(3, "static int s = 2;\nlink \"add\";\nint main() { return add(s, 1); }\n");
    }
    it("allows a static name beside a public one in another module") {
      module("pub", "int v = 5;\nint pv() { return v; }\n");
      expect(6, "link \"pub\";\nstatic int v = 1;\nint main() { return pv() + v; }\n");
    }
    it("folds . and .. in module paths") { // a cycle through .. would grow the path
      mkdir("bins/sub", 0755);
      module("dot-a", "link \"sub/dot-b\";\nint a() { return b() + 1; }\n");
      module("sub/dot-b", "link \"../dot-a\";\nint b() { return 2; }\n");
      expect(3, "link \"dot-a\";\nlink \"./dot-a\";\nint main() { return a(); }\n");
    }
    it("finds a module in a -p directory") {
      mkdir("bins/plat-a", 0755);
      module("plat-a/pio", "int pio() { return 4; }\n");
      dirs_at[0] = "bins/plat-a";
      expect(4, "link \"pio\";\nint main() { return pio(); }\n");
      dirs_at[0] = NULL;
    }
    it("takes a module from the first -p directory that has it") {
      mkdir("bins/plat-a", 0755);
      mkdir("bins/plat-b", 0755);
      module("plat-a/pio", "int pio() { return 4; }\n");
      module("plat-b/pio", "int pio() { return 5; }\n");
      dirs_at[0] = "bins/plat-b/", dirs_at[1] = "bins/plat-a";
      expect(5, "link \"pio\";\nint main() { return pio(); }\n");
      dirs_at[0] = dirs_at[1] = NULL;
    }
    it("prefers a module beside the source to one in a -p directory") {
      mkdir("bins/plat-a", 0755);
      module("plat-a/pnear", "int pnear() { return 4; }\n");
      module("pnear", "int pnear() { return 6; }\n");
      dirs_at[0] = "bins/plat-a";
      expect(6, "link \"pnear\";\nint main() { return pnear(); }\n");
      dirs_at[0] = NULL;
    }
    it("finds the links of a -p module beside it") {
      mkdir("bins/plat-a", 0755);
      module("plat-a/pout", "link \"pin\";\nint pout() { return pin() + 1; }\n");
      module("plat-a/pin", "int pin() { return 2; }\n");
      dirs_at[0] = "bins/plat-a";
      expect(3, "link \"pout\";\nint main() { return pout(); }\n");
      dirs_at[0] = NULL;
    }
    it("prefers a module in a -p directory to a host library") {
      mkdir("bins/plat-a", 0755);
      module("plat-a/m", "int m3() { return 3; }\n");
      dirs_at[0] = "bins/plat-a";
      expect(3, "link \"m\";\nint main() { return m3(); }\n");
      dirs_at[0] = NULL;
    }
  }

  context("load") { // exec binds imports; the forms only record them so far
    it("declares imports after a load")
      expect(3, "load \"c\";\nint puts(char*);\nlong labs(long n);\n"
                "int main() { return 3; }\n");
    it("declares an import in two modules") {
      module("imp", "load \"c\";\nint puts(char* s);\nint one() { return 1; }\n");
      expect(1, "link \"imp\";\nload \"c\";\nint puts(char*);\nint main() { return one(); }\n");
    }
    it("imports from c without a load") {
      module("ld", "load \"m\";\n");
      expect(2, "link \"ld\";\nint puts(char*);\nload \"c\";\nint puts(char*);\n"
                "int main() { return 2; }\n");
    }
    it("declares variadic, (void) and extern imports")
      expect(4, "int printf(char* f, ...);\nint getpid(void);\nextern char** environ;\n"
                "extern int a, b[2];\nint main() { return 4; }\n");
    it("calls an import")
      here(7, "load \"c\";\nlong labs(long n);\nint main() { long v = -7; return labs(v); }\n");
    it("calls a variadic import")
      here(5, "int printf(char* f, ...);\nint main() { int v = 5; return printf(\"%d\\n\", v) + 3; }\n");
    it("compiles a dead call to an import")
      here(0, "load \"c\";\nint puts(char*);\nint main() { return 0 && puts(\"x\"); }\n");
    it("reads an extern import")
      here(1, "extern char** environ;\nint main() { return environ != 0; }\n");
    it("binds only the imports it uses")
      here(2, "load \"zzz\";\nint f(int);\nload \"c\";\nlong labs(long n);\n"
              "int main() { long v = -2; return labs(v); }\n");
    it("runs a link of a shared library as its load")
      here(6, "link \"c\";\nlong labs(long n);\nint main() { long v = -6; return labs(v); }\n");
    it("rejects a link of a library the host does not have")
      expect(bad, "link \"zzz\";\nint main() { return 0; }\n");
  }

  context("objects") { // built by cc: an object, or the members of an archive
    elfs_it("calls between members") {
      archive("libar-pull", (char*[]){
        "int twice(int);\nint quad(int x) { return twice(twice(x)); }\n",
        "int twice(int x) { return x * 2; }\n", NULL});
      expect(12, "link \"ar-pull\";\nint main() { return quad(3); }\n");
    }
    elfs_it("reaches data, bss and the globals of other members") {
      archive("libar-data", (char*[]){
        "static const int tab[] = {10, 20, 30, 40};\nint counter;\nextern int base;\n"
        "int get(int i) { counter += i; return tab[i] + counter + base; }\n",
        "int base = 100;\n", NULL});
      expect(54, "link \"ar-data\";\nint main() { return get(1) + get(2) - 200; }\n");
    }
    elfs_it("calls back into the program") {
      archive("libar-back", (char*[]){
        "int tic_cb(int);\nint call_back(int x) { return tic_cb(x) + 1; }\n", NULL});
      expect(13, "link \"ar-back\";\nint tic_cb(int x) { return x * 3; }\nint main() { return call_back(4); }\n");
    }
    elfs_it("lets a strong definition replace a weak one") {
      archive("libar-weak", (char*[]){
        "__attribute__((weak)) int pick(void) { return 1; }\nint use(void) { return pick(); }\n",
        "int pick(void) { return 2; }\nint other(void) { return 5; }\n", NULL});
      expect(25, "link \"ar-weak\";\nint main() { return use() * 10 + other(); }\n");
    }
    elfs_it("aligns the stack for C code") { // pushed operands: 0, 2, then 1
      archive("libar-align", (char*[]){
        "int aligned(void) { return ((long)__builtin_frame_address(0) & 15) == 0; }\n", NULL});
      expect(111, "link \"ar-align\";\nint main() { int a = 1; return aligned() * 100 + (a + 9) * aligned() + aligned(); }\n");
    }
    elfs_it("passes variadic arguments to C code") {
      archive("libar-vary", (char*[]){"#include <stdarg.h>\n"
        "int sum(int n, ...) { va_list ap; va_start(ap, n); int a = va_arg(ap, int);\n"
        "  int b = va_arg(ap, int); int c = va_arg(ap, int); va_end(ap); return n + a + b * 2 + c * 4; }\n",
        NULL}); // no loop: cc would align its text to 32
      expect(24, "link \"ar-vary\";\nint sum(int n, ...);\nint main() { return sum(3, 1, 2, 4); }\n");
    }
    elfs_it("cuts C results to their type") { // the asm leaves the upper bytes set
      archive("libar-cut", (char*[]){
        "int cut(long x) { int r; __asm__(\"mov %1, %q0\" : \"=r\"(r) : \"r\"(x)); return r; }\n"
        "char low(long x) { char r; __asm__(\"mov %1, %q0\" : \"=r\"(r) : \"r\"(x)); return r; }\n",
        NULL});
      expect(2, "link \"ar-cut\";\nint cut(long x);\nchar low(long x);\nint main() { long b = 65536; b = b * 65536; int r = 0;\n"
                "  when(cut(b)) r = 1;\n  return r + (low(511) == 255) * 2;\n}\n");
    }
    elfs_it("takes the result type of C code from a prototype") {
      archive("libar-long", (char*[]){"long big(void) { return 1L << 40; }\n", NULL});
      expect(1, "link \"ar-long\";\nlong big(void);\nlong big(void);\nint main() { return big() >> 40; }\n");
    }
    elfs_it("pulls only the members it uses") { // the second one would be undefined
      archive("libar-lazy", (char*[]){"int add(int a, int b) { return a + b; }\n",
        "int nowhere(void);\nint broken(void) { return nowhere(); }\n", NULL});
      expect(5, "link \"ar-lazy\";\nint main() { return add(2, 3); }\n");
    }
    elfs_it("keeps a weak definition when no strong one is pulled") {
      archive("libar-keep", (char*[]){
        "__attribute__((weak)) int pick(void) { return 1; }\nint use(void) { return pick(); }\n",
        "int pick(void) { return 2; }\n", NULL});
      expect(1, "link \"ar-keep\";\nint main() { return use(); }\n");
    }
    elfs_it("reads data of a member") {
      archive("libar-glob", (char*[]){"int seven = 7;\n", NULL});
      expect(9, "link \"ar-glob\";\nint main() { return seven + 2; }\n");
    }
    elfs_it("aligns code, data and bss over 16") {
      archive("libar-wide", (char*[]){
        "_Alignas(64) int tab[4] = {1, 2, 3, 4};\n_Alignas(128) char buf[8];\n"
        "__attribute__((aligned(64))) int wide(void) { return tab[3]; }\n"
        "int ok(void) { long volatile a = (long)tab, b = (long)buf, c = (long)wide;\n" // not folded by cc
        "  return !(a & 63) + !(b & 127) + !(c & 63) + wide(); }\n", NULL});
      expect(7, "link \"ar-wide\";\nint main() { return ok(); }\n");
    }
    elfs_it("fills pointers in data") {
      archive("libar-addr", (char*[]){
        "static int v[2] = {3, 4};\nint* p = &v[1];\nint two(void) { return 2; }\n"
        "int (*fp)(void) = two;\nint deref(void) { return *p * 10 + fp(); }\n", NULL});
      expect(42, "link \"ar-addr\";\nint main() { return deref(); }\n");
    }
    elfs_it("puts a weak undefined name at address 0") {
      archive("libar-null", (char*[]){
        "extern __attribute__((weak)) int missing(void);\nextern __attribute__((weak)) char gone[];\n"
        "int has(void) { char* volatile a = (char*)missing; char* volatile b = gone;\n" // GOT loads
        "  return 7 + (a != 0) + (b != 0); }\n", NULL});
      expect(7, "link \"ar-null\";\nint main() { return has(); }\n");
    }
    elfs_it("rejects an undefined symbol") {
      archive("libar-undef", (char*[]){
        "int nowhere(void);\nint broken(void) { return nowhere(); }\n", NULL});
      expect(bad, "link \"ar-undef\";\nint main() { return broken(); }\n");
    }
    elfs_it("rejects a function defined twice") {
      archive("libar-twice", (char*[]){
        "int add(int a, int b) { return a + b; }\nint sub(int a, int b) { return a - b; }\n", NULL});
      expect(bad, "link \"ar-twice\";\nint add(int a, int b) { return 0; }\nint main() { return sub(3, 1); }\n");
    }
    elfs_it("links an object beside the source") {
      archive("lk-obj", (char*[]){"int add(int a, int b) { return a + b; }\n", NULL});
      expect(5, "link \"lk-obj-0\";\nint main() { return add(2, 3); }\n");
    }
    elfs_it("links an archive as libname.a, once") {
      archive("liblka", (char*[]){"int add(int a, int b) { return a + b; }\n",
                                 "long big(void) { return 1L << 40; }\n", NULL});
      expect(6, "link \"lka\";\nlink \"./lka\";\nlong big(void);\n"
                "int main() { return add(2, 3) + (big() >> 40); }\n");
    }
    elfs_it("keeps the static names of the source that links an object") {
      archive("libpk", (char*[]){"int pick(void) { return 1; }\nint use(void) { return pick(); }\n", NULL});
      expect(8, "static int pick() { return 7; }\nlink \"pk\";\nint main() { return pick() + use(); }\n");
    }
    elfs_it("starts a C library linked in") { // it exits with main's result + 1
      archive("libst", (char*[]){"static int up;\n"
        "int __libc_start_main(int (*m)(int, char**, char**), int argc, char** argv,\n"
        "                      long a, long b, long c) {\n"
        "  up = argc == 1 && !argv[1] && !a && !b && !c\n"
        "       && !((long)__builtin_frame_address(0) & 15);\n"
        "  int r = m(argc, argv, argv + argc + 1) + 1;\n"
        "  __asm__ volatile(\"syscall\" :: \"a\"(60), \"D\"(r));\n"
        "  __builtin_unreachable();\n}\n"
        "int started(void) { return up; }\n", NULL});
      only(12, "link \"st\";\nint started(void);\n"
               "int main(int argc, char** argv) { return started() * 10 + argc; }\n");
    }
    elfs_it("calls an archive member from a module") {
      archive("libmd", (char*[]){"int add(int a, int b) { return a + b; }\n", NULL});
      module("mdl", "link \"md\";\nint md() { return add(4, 5); }\n");
      expect(9, "link \"mdl\";\nint main() { return md(); }\n");
    }
    elfs_it("rejects a forward call to an object") {
      archive("lk-fwd", (char*[]){"int add(int a, int b) { return a + b; }\n", NULL});
      expect(bad, "int main() { return add(2, 3); }\nlink \"lk-fwd-0\";\n");
    }
    elfs_it("rejects an import declared before the object") {
      archive("lk-dec", (char*[]){"int add(int a, int b) { return a + b; }\n", NULL});
      expect(bad, "int add(int a, int b);\nlink \"lk-dec-0\";\nint main() { return add(2, 3); }\n");
    }
  }

  context("libraries") { // without main: shared by default, an object with -s
    #define USE "int twice(int); int total(int);\n" \
                "int main() { return twice(4) * 10 + total(3); }\n"
    #define LIB "link \"lib-part\";\nint total(int n) { int s = 0; while(n) { s += n; n -= 1; } return s; }\n"
    elfs_it("writes a shared library") {
      module("lib-part", "int twice(int x) { return x * 2; }\n");
      check(library("lb-sh", LIB, 0, "", USE) == 86);
    }
    elfs_it("writes one object for all its sources, with -s") {
      module("lib-part", "int twice(int x) { return x * 2; }\n");
      check(library("lb-st", LIB, 1, "", USE) == 86);
      check(library("lb-st", LIB, 1, "-no-pie", USE) == 86);
    }
    elfs_it("loads a shared library at run time") {
      check(library("lb-dl", "int seven() { return 7; }\n", 0, "", "#include <dlfcn.h>\n"
        "int main(int c, char** v) { void* h = dlopen(v[1], RTLD_NOW); if(!h) return 99;\n"
        "  int (*f)(void) = (int (*)(void))dlsym(h, \"seven\"); return f? f(): 98; }\n") == 7);
    }
    elfs_it("keeps one copy of a global of a shared library") {
      // a program built without -fPIC copies a global out of the library: protected, it may not
      char* src = "int count = 5;\nint bump() { count += 1; return count; }\n";
      char* drv = "extern int count; int bump(void);\n"
                  "int main() { count = 10; return bump() * 10 + (count == 11); }\n";
      check(library("lb-gl", src, 0, "-fPIC", drv) == 111);
      check(library("lb-gl", src, 0, "-fno-PIC -no-pie", drv) == odd);
      check(library("lb-glo", src, 1, "", drv) == 111);
    }
    elfs_it("binds the imports of a library") {
      char* src = "load \"c\";\nlong labs(long n);\nint printf(char* f, ...);\nextern char** environ;\n"
                  "int abs3(int v) { long n = v; return labs(n) + (environ != 0) + (printf(\"\") == 0); }\n";
      char* drv = "int abs3(int);\nint main() { return abs3(-5); }\n";
      check(library("lb-im", src, 0, "", drv) == 7);
      check(library("lb-imo", src, 1, "", drv) == 7);
    }
    elfs_it("relocates the addresses in data of its objects") {
      archive("libar-lib", (char*[]){
        "static int v[2] = {3, 4};\nint* p = &v[1];\nint two(void) { return 2; }\n"
        "int (*fp)(void) = two;\nint deref(void) { return *p * 10 + fp(); }\n", NULL});
      char* src = "link \"ar-lib\";\nint get() { return deref(); }\n";
      char* drv = "int get(void);\nint main() { return get(); }\n";
      check(library("lb-dr", src, 0, "", drv) == 42);
      check(library("lb-dro", src, 1, "", drv) == 42);
    }
    elfs_it("leaves a weak name nothing defines to the linker of an object") {
      archive("libar-libnull", (char*[]){
        "extern __attribute__((weak)) int missing(void);\n"
        "int has(void) { char* volatile a = (char*)missing; return 7 + (a != 0); }\n", NULL});
      char* src = "link \"ar-libnull\";\nint get() { return has(); }\n";
      char* drv = "int get(void);\nint main() { return get(); }\n";
      check(library("lb-nul", src, 1, "", drv) == 7);
      check(library("lb-nul", src, 1, "-no-pie", drv) == 7);
      check(library("lb-nuls", src, 0, "", drv) == 7);
    }
    #undef USE
    #undef LIB
  }

  context("Mach-O libraries") { // linked by ld64.lld, not run: skipped without clang and lld
    #define USE "int twice(int); int total(int); extern int count;\n" \
                "int main() { return twice(4) * 10 + total(3) + count; }\n"
    #define LIB "link \"lib-part\";\nint count = 1;\n" \
                "int total(int n) { int s = 0; while(n) { s += n; n -= 1; } return s + count; }\n"
    it("links a dylib and one object for all its sources", apple()) {
      module("lib-part", "int twice(int x) { return x * 2; }\n");
      check(machlib("lb-msh", LIB, 0, "ia64", USE) == 0);
      check(machlib("lb-mst", LIB, 1, "ia64", USE) == 0);
    }
    it("links an arm64 dylib, but no arm64 object yet", apple()) {
      module("lib-part", "int twice(int x) { return x * 2; }\n");
      check(machlib("lb-msha", LIB, 0, "arm8", USE) == 0);
      check(machlib("lb-msta", LIB, 1, "arm8", USE) == bad);
    }
    it("leaves its imports to the program", apple()) {
      char* src = "load \"c\";\nlong labs(long n);\nint printf(char* f, ...);\n"
                  "int abs3(int v) { long n = v; return labs(n) + (printf(\"\") == 0); }\n";
      char* drv = "int abs3(int);\nint main() { return abs3(-5); }\n";
      check(machlib("lb-mim", src, 0, "ia64", drv) == 0);
      check(machlib("lb-mimo", src, 1, "ia64", drv) == 0);
      check(machlib("lb-mima", src, 0, "arm8", drv) == 0);
    }
    it("fails to link what the library does not export", apple()) {
      check(machlib("lb-mno", "static int hidden() { return 1; }\nint shown() { return hidden(); }\n", 0, "ia64",
                    "int hidden(void);\nint main() { return hidden(); }\n") == odd);
    }
    #undef USE
    #undef LIB
  }

  context("syscall") {
    linux_it("calls the kernel") // write(99, "x", 1) fails with EBADF
      expect(9, "int main() { return -syscall(" WRITE ", 99, \"x\", 1); }\n");
    linux_it("passes a string to the kernel")
      expect(3, "int main() { return syscall(" WRITE ", 1, \"\", 0) + 3; }\n");
  }

  context("errors") {
    it("rejects an unknown character")
      expect(bad, "int main() { return $; }\n");
    it("rejects an unterminated comment")
      expect(bad, "int main() { return 1; }\n/* never closed\n");
    it("rejects an unterminated string")
      expect(bad, "int main() {\n  \"abc\n}\n");
    it("rejects a missing operand")
      expect(bad, "int main() { return (1 + ); }\n");
    it("rejects an unclosed parenthesis")
      expect(bad, "int main() { return (1 + 2; }\n");
    it("rejects a number as a type")
      expect(bad, "42 main() { return 1; }\n");
    it("rejects a number as a parameter name")
      expect(bad, "int f(int 5) { }\n");
    it("rejects running a program without main") { // -e writes a library
      module("nomain", "int f() { return 1; }\n");
      snprintf(src_at, sizeof src_at, "bins/nomain.c");
      check(jit() == bad);
    }
    it("rejects running main as a variable") {
      module("nomain", "int main;\n");
      snprintf(src_at, sizeof src_at, "bins/nomain.c");
      check(jit() == bad);
    }
    it("rejects an undefined variable")
      expect(bad, "int main() { return x; }\n");
    it("rejects a variable out of scope")
      expect(bad, "int main() { { int a = 1; } return a; }\n");
    it("rejects assigning to a value")
      expect(bad, "int main() { int a; 1 = a; return 0; }\n");
    it("rejects assigning to an expression")
      expect(bad, "int main() { int a; a + 1 = 2; return 0; }\n");
    it("rejects an undefined function")
      expect(bad, "int main() { return g(); }\n");
    it("rejects a dead call to an undefined function")
      expect(bad, "int main() { return 0 && g(); }\n");
    it("rejects calling a variable")
      expect(bad, "int main() { int a = 1; return a(); }\n");
    it("rejects a wrong argument count")
      expect(bad, "int f(int a) { return a; }\nint main() { return f(1, 2); }\n");
    it("rejects a wrong argument count in a forward call")
      expect(bad, "int main() { return f(); }\nint f(int a) { return a; }\n");
    it("rejects more than 6 arguments")
      expect(bad, "int main() { return f(1, 2, 3, 4, 5, 6, 7); }\n");
    it("rejects break outside a loop")
      expect(bad, "int main() { break; }\n");
    it("rejects skip outside a loop")
      expect(bad, "int main() { when(1) skip; }\n");
    it("rejects dereferencing a non-pointer")
      expect(bad, "int main() { int a; return *a; }\n");
    it("rejects taking the address of a value")
      expect(bad, "int main() { return &1; }\n");
    it("rejects adding pointers")
      expect(bad, "int main() { int* p; int* q; return p + q; }\n");
    it("rejects assigning to an array")
      expect(bad, "int main() { int a[2]; int b[2]; a = b; return 0; }\n");
    it("rejects an unknown member")
      expect(bad, "struct p { int x; };\nint main() { p v; return v.y; }\n");
    it("rejects a struct containing itself")
      expect(bad, "struct p { int x; p q; };\nint main() { return 0; }\n");
    it("rejects a non-constant global initializer")
      expect(bad, "int a = 1;\nint b = a;\nint main() { return b; }\n");
    it("rejects a non-constant array size")
      expect(bad, "int main() { int n = 2; int a[n]; return 0; }\n");
    it("rejects too many initializers")
      expect(bad, "int a[2] = {1, 2, 3};\nint main() { return 0; }\n");
    it("rejects a string too long for its array")
      expect(bad, "char s[3] = \"abc\";\nint main() { return 0; }\n");
    it("rejects a local string too long for its array")
      expect(bad, "int main() { char s[3] = \"abc\"; return 0; }\n");
    it("rejects too many nested local initializers")
      expect(bad, "int main() { int a[2][2] = {{1, 2, 3}}; return 0; }\n");
    it("rejects too many struct initializers")
      expect(bad, "struct p { int x; int y; };\nint main() { p v = {1, 2, 3}; return 0; }\n");
    it("rejects an unknown member designator")
      expect(bad, "struct p { int x; };\nint main() { p v = {.z = 1}; return 0; }\n");
    it("rejects an index designator out of bounds")
      expect(bad, "int main() { int a[2] = {[2] = 1}; return 0; }\n");
    it("rejects a non-constant index designator")
      expect(bad, "int main() { int n = 0; int a[2] = {[n] = 1}; return 0; }\n");
    it("rejects a member designator into a non-struct")
      expect(bad, "struct p { int x; };\nint main() { p v = {.x.y = 1}; return 0; }\n");
    it("rejects an index designator into a struct")
      expect(bad, "struct p { int x; };\nint main() { p v = {[0] = 1}; return 0; }\n");
    it("rejects a bit-field of no integer")
      expect(bad, "struct s { int* p : 3; };\nint main() { return 0; }\n");
    it("rejects a bit-field wider than its type")
      expect(bad, "struct s { byte a : 9; };\nint main() { return 0; }\n");
    it("rejects a named bit-field of width 0")
      expect(bad, "struct s { int a : 0; };\nint main() { return 0; }\n");
    it("rejects the address of a bit-field")
      expect(bad, "struct s { int a : 3; };\nint main() { s v; int* p = &v.a; return 0; }\n");
    it("rejects an unknown struct")
      expect(bad, "int main() { struct nope v; return 0; }\n");
    it("rejects a cast")
      expect(bad, "int main() { long x = 3; return (int)x; }\n");
    it("rejects a compound literal in a global initializer")
      expect(bad, "struct p { int a; };\np g = (p){1};\nint main() { return 0; }\n");
    it("rejects a non-constant global member")
      expect(bad, "struct p { int x; };\nint n;\np g = {.x = n};\nint main() { return 0; }\n");
    it("rejects a void variable")
      expect(bad, "int main() { void v; return 0; }\n");
    it("rejects a struct operand")
      expect(bad, "struct pr { int a; int b; int c; };\nint main() { pr p; int x = 1 + p; return x; }\n");
    it("rejects a forward call to a function returning a pointer")
      expect(bad, "int g = 4;\nint main() { int* p = get(); return *p; }\nint* get() { return &g; }\n");
    it("rejects a missing module")
      expect(bad, "link \"none\";\nint main() { return 0; }\n");
    it("rejects a module name that is not a string")
      expect(bad, "link add;\nint main() { return 0; }\n");
    it("rejects a function defined twice")
      expect(bad, "int f() { return 1; }\nint f() { return 2; }\nint main() { return f(); }\n");
    it("rejects a function defined in two modules") {
      module("dup", "int add(int a, int b) { return a - b; }\n");
      module("add", "int add(int a, int b) { return a + b; }\n");
      expect(bad, "link \"add\";\nlink \"dup\";\nint main() { return add(2, 3); }\n");
    }
    it("rejects a static name from another module") {
      module("hid", "static int hid() { return 1; }\n");
      expect(bad, "link \"hid\";\nint main() { return hid(); }\n");
    }
    it("rejects a static function called from another module") {
      module("ma", "int ma() { return get(); }\n");
      expect(bad, "link \"ma\";\nstatic int get() { return 7; }\nint main() { return ma(); }\n");
    }
    it("hides static names from a linked module") {
      module("lk", "int lk() { return sv(); }\n");
      expect(bad, "static int sv() { return 9; }\nlink \"lk\";\nint main() { return lk(); }\n");
    }
    it("rejects a global defined twice")
      expect(bad, "int g = 1;\nint g = 2;\nint main() { return g; }\n");
    it("rejects a global and a function of the same name")
      expect(bad, "int g;\nint g() { return 1; }\nint main() { return 0; }\n");
    it("rejects a function and a global of the same name")
      expect(bad, "int g() { return 1; }\nint g;\nint main() { return 0; }\n");
    it("rejects a global defined in two modules") {
      module("k1", "int k = 1;\n");
      module("k2", "int k = 2;\n");
      expect(bad, "link \"k1\";\nlink \"k2\";\nint main() { return k; }\n");
    }
    it("rejects a library name that is not a string")
      expect(bad, "load c;\nint main() { return 0; }\n");
    it("rejects a use of an extern global")
      expect(bad, "extern int g;\nint main() { return g; }\n");
    it("rejects an extern global from two libraries")
      expect(bad, "load \"c\";\nextern int g;\nload \"m\";\nextern int g;\nint main() { return 0; }\n");
    it("rejects an initialized extern global")
      expect(bad, "extern int g = 1;\nint main() { return 0; }\n");
    it("rejects a variadic function with a body")
      expect(bad, "int f(int n, ...) { return n; }\nint main() { return f(1); }\n");
    it("rejects a call to an import that is not in its library")
      expect(bad, "load \"c\";\nint nosuch(int);\nint main() { return nosuch(1); }\n");
    it("rejects a call to an import from a missing library")
      expect(bad, "load \"zzz\";\nint f(int);\nint main() { return f(1); }\n");
    it("rejects a forward call to an import")
      expect(bad, "int main() { return puts(\"x\"); }\nload \"c\";\nint puts(char*);\n");
    it("rejects an import from two libraries")
      expect(bad, "load \"c\";\nint f();\nload \"m\";\nint f();\nint main() { return 0; }\n");
    it("rejects an import with another argument count")
      expect(bad, "load \"c\";\nint f(int);\nint f(int, int);\nint main() { return 0; }\n");
    it("rejects a body for an import")
      expect(bad, "load \"c\";\nint f();\nint f() { return 1; }\nint main() { return f(); }\n");
    it("rejects an import of c and of another library") {
      module("ld", "load \"m\";\nint puts(char*);\n");
      expect(bad, "link \"ld\";\nint puts(char*);\nint main() { return 0; }\n");
    }
    it("rejects a parameter without a name in a definition")
      expect(bad, "int f(int) { return 1; }\nint main() { return 0; }\n");
    it("rejects a call to a function without a body")
      expect(bad, "int f(int a);\nint main() { return f(1); }\n");
  }
}
