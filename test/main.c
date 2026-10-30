// vim: ts=2:sw=2
// tic's spec: each case is written to bins/ and compiled by the tic binary in
// $TIC (default bins/one) in a child process (fail() exits, code may crash),
// run in memory and as an executable written with `-o` (ELF, Mach-O
// or PE). Constant operands are folded, so the cases that test generated code
// take their operands from locals. It tests the target it is built for, which
// must be tic's: ./check cross-builds both for arm8.
#include "test.h"

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
static char* lib; // an object or a static library tic gets after the case

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
  int code = spawn(fd[1], (char*[]){tic(), src_at, lib, NULL});
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

// `tic -o`: the written executable's exit code
static int  exe(void) {
  int null = open(NULL_DEV, O_WRONLY);
  unlink(exe_at);
  int code = spawn(null, (char*[]){tic(), "-o", exe_at, src_at, lib, NULL});
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

// compile src both ways: NULL when exec gives run and -o gives want, else why
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
// only(want, src): exec rejects it, the executable from -o returns want
#define only(want, src) saw(bad, want, src)
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

  context("if") {
    it("takes a true branch")
      expect(4, "int main() {\n  if(3) return 4;\n  else return 2;\n  return 42;\n}\n");
    it("takes a false branch")
      expect(2, "int main() {\n  if(0) return 4;\n  else return 2;\n}\n");
    it("nests")
      expect(7, "int main() {\n  if(1) {\n    if(0) return 5;\n    return 7;\n  }\n"
                "  return 9;\n}\n");
    it("tests an expression")
      expect(7, "int main() {\n  if(2 > 1 && 3) return 7;\n  return 9;\n}\n");
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
      expect(144, "int fib(int n) {\n  if(n < 2) return n;\n"
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
      expect(bad, "int main() { int i = 0; for(;;) { i += 1; if(i == 9) break; } return i; }\n");
    it("continues while at its condition")
      expect(18, "int main() {\n  int i = 0; int s = 0;\n"
                 "  while(i < 10) { i += 1; if(i % 3) continue; s += i; }\n  return s;\n}\n");
    it("breaks the inner loop only")
      expect(6, "int main() {\n  int n = 0; int i = 0;\n  while(i < 3) {\n    int j = 0;\n"
                "    while(1) { if(j == 2) break; j += 1; n += 1; }\n    i += 1;\n  }\n"
                "  return n;\n}\n");
    it("jumps over 127 bytes")
      expect(1, "int main() {\n  if(1) { " RET5 RET5 RET5 RET5 RET5 "}\n  return 0;\n}\n");
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
      module("cyc-a", "link \"cyc-b\";\nint a(int n) { if(n) return b(n - 1) + 2; return 0; }\n");
      module("cyc-b", "link \"cyc-a\";\nint b(int n) { if(n) return a(n - 1) + 1; return 0; }\n");
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
  }

  context("load") { // imports are only recorded until the forms bind them
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
  }

  context("objects") { // built by cc: an object, or the members of an archive
    elfs_it("calls a function from an object") {
      archive("ar-obj", (char*[]){"int add(int a, int b) { return a + b; }\n", NULL});
      lib = "bins/ar-obj-0.o";
      expect(5, "int main() { return add(2, 3); }\n");
    }
    elfs_it("calls a function from an archive") {
      lib = archive("ar-add", (char*[]){"int add(int a, int b) { return a + b; }\n", NULL});
      expect(5, "int main() { return add(2, 3); }\n");
    }
    elfs_it("calls between members") {
      lib = archive("ar-pull", (char*[]){
        "int twice(int);\nint quad(int x) { return twice(twice(x)); }\n",
        "int twice(int x) { return x * 2; }\n", NULL});
      expect(12, "int main() { return quad(3); }\n");
    }
    elfs_it("reaches data, bss and the globals of other members") {
      lib = archive("ar-data", (char*[]){
        "static const int tab[] = {10, 20, 30, 40};\nint counter;\nextern int base;\n"
        "int get(int i) { counter += i; return tab[i] + counter + base; }\n",
        "int base = 100;\n", NULL});
      expect(54, "int main() { return get(1) + get(2) - 200; }\n");
    }
    elfs_it("calls back into the program") {
      lib = archive("ar-back", (char*[]){
        "int tic_cb(int);\nint call_back(int x) { return tic_cb(x) + 1; }\n", NULL});
      expect(13, "int tic_cb(int x) { return x * 3; }\nint main() { return call_back(4); }\n");
    }
    elfs_it("lets a strong definition replace a weak one") {
      lib = archive("ar-weak", (char*[]){
        "__attribute__((weak)) int pick(void) { return 1; }\nint use(void) { return pick(); }\n",
        "int pick(void) { return 2; }\nint other(void) { return 5; }\n", NULL});
      expect(25, "int main() { return use() * 10 + other(); }\n");
    }
    elfs_it("aligns the stack for C code") { // pushed operands: 0, 2, then 1
      lib = archive("ar-align", (char*[]){
        "int aligned(void) { return ((long)__builtin_frame_address(0) & 15) == 0; }\n", NULL});
      expect(111, "int main() { int a = 1; return aligned() * 100 + (a + 9) * aligned() + aligned(); }\n");
    }
    elfs_it("passes variadic arguments to C code") {
      lib = archive("ar-vary", (char*[]){"#include <stdarg.h>\n"
        "int sum(int n, ...) { va_list ap; va_start(ap, n); int a = va_arg(ap, int);\n"
        "  int b = va_arg(ap, int); int c = va_arg(ap, int); va_end(ap); return n + a + b * 2 + c * 4; }\n",
        NULL}); // no loop: cc would align its text to 32
      expect(24, "int sum(int n, ...);\nint main() { return sum(3, 1, 2, 4); }\n");
    }
    elfs_it("cuts C results to their type") { // the asm leaves the upper bytes set
      lib = archive("ar-cut", (char*[]){
        "int cut(long x) { int r; __asm__(\"mov %1, %q0\" : \"=r\"(r) : \"r\"(x)); return r; }\n"
        "char low(long x) { char r; __asm__(\"mov %1, %q0\" : \"=r\"(r) : \"r\"(x)); return r; }\n",
        NULL});
      expect(2, "int cut(long x);\nchar low(long x);\nint main() { long b = 65536; b = b * 65536; int r = 0;\n"
                "  if(cut(b)) r = 1;\n  return r + (low(511) == 255) * 2;\n}\n");
    }
    elfs_it("takes the result type of C code from a prototype") {
      lib = archive("ar-long", (char*[]){"long big(void) { return 1L << 40; }\n", NULL});
      expect(1, "long big(void);\nlong big(void);\nint main() { return big() >> 40; }\n");
    }
    elfs_it("pulls only the members it uses") { // the second one would be undefined
      lib = archive("ar-lazy", (char*[]){"int add(int a, int b) { return a + b; }\n",
        "int nowhere(void);\nint broken(void) { return nowhere(); }\n", NULL});
      expect(5, "int main() { return add(2, 3); }\n");
    }
    elfs_it("keeps a weak definition when no strong one is pulled") {
      lib = archive("ar-keep", (char*[]){
        "__attribute__((weak)) int pick(void) { return 1; }\nint use(void) { return pick(); }\n",
        "int pick(void) { return 2; }\n", NULL});
      expect(1, "int main() { return use(); }\n");
    }
    elfs_it("reads data of a member") {
      lib = archive("ar-glob", (char*[]){"int seven = 7;\n", NULL});
      expect(9, "int main() { return seven + 2; }\n");
    }
    elfs_it("aligns code, data and bss over 16") {
      lib = archive("ar-wide", (char*[]){
        "_Alignas(64) int tab[4] = {1, 2, 3, 4};\n_Alignas(128) char buf[8];\n"
        "__attribute__((aligned(64))) int wide(void) { return tab[3]; }\n"
        "long at[3] = {(long)tab, (long)buf, (long)wide};\n" // not folded by cc
        "int ok(void) { return !(at[0] & 63) + !(at[1] & 127) + !(at[2] & 63) + wide(); }\n", NULL});
      expect(7, "int main() { return ok(); }\n");
    }
    elfs_it("fills pointers in data") {
      lib = archive("ar-addr", (char*[]){
        "static int v[2] = {3, 4};\nint* p = &v[1];\nint two(void) { return 2; }\n"
        "int (*fp)(void) = two;\nint deref(void) { return *p * 10 + fp(); }\n", NULL});
      expect(42, "int main() { return deref(); }\n");
    }
    elfs_it("puts a weak undefined name at address 0") {
      lib = archive("ar-null", (char*[]){
        "extern __attribute__((weak)) int missing(void);\nextern __attribute__((weak)) char gone[];\n"
        "int has(void) { char* volatile a = (char*)missing; char* volatile b = gone;\n" // GOT loads
        "  return 7 + (a != 0) + (b != 0); }\n", NULL});
      expect(7, "int main() { return has(); }\n");
    }
    elfs_it("rejects an undefined symbol") {
      lib = archive("ar-undef", (char*[]){
        "int nowhere(void);\nint broken(void) { return nowhere(); }\n", NULL});
      expect(bad, "int main() { return broken(); }\n");
    }
    elfs_it("rejects a function defined twice") {
      lib = archive("ar-twice", (char*[]){
        "int add(int a, int b) { return a + b; }\nint sub(int a, int b) { return a - b; }\n", NULL});
      expect(bad, "int add(int a, int b) { return 0; }\nint main() { return sub(3, 1); }\n");
    }
    lib = NULL;
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
    it("rejects a program without main")
      expect(bad, "int f() { return 1; }\n");
    it("rejects main as a variable")
      expect(bad, "int main;\n");
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
    it("rejects continue outside a loop")
      expect(bad, "int main() { if(1) continue; }\n");
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
    it("rejects a call to an import")
      expect(bad, "load \"c\";\nint puts(char*);\nint main() { return puts(\"x\"); }\n");
    it("rejects a dead call to an import")
      expect(bad, "load \"c\";\nint puts(char*);\nint main() { return 0 && puts(\"x\"); }\n");
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
