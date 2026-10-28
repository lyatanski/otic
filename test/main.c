// vim: ts=2:sw=2
// tic's spec: each case is written to bins/, compiled by tic's own main in a
// forked child (fail() exits, code may crash) and run through `-m exec` and
// as a static executable written with `-o`. Constant operands are folded, so
// the cases that test generated code take their operands from locals.
// It tests the target it is built for: ./check cross-builds it for arm8.
#define main tic
#include "../main.c"
#undef main
#include "test.h"

#include <fcntl.h>    /* open */
#include <limits.h>   /* INT_MIN */
#include <sys/stat.h> /* chmod, mkdir */
#include <sys/wait.h> /* waitpid */
#include <unistd.h>   /* fork, pipe, execv, alarm */

#ifdef __aarch64__
#define TAG "arm8"
#define pick(x86, arm) (arm)
#else
#define TAG "x86"
#define pick(x86, arm) (x86)
#endif

enum { bad = INT_MIN, odd }; // compile error, no result (crash, timeout, ...)

static char src_at[64], exe_at[64];

// run tic with args, or exec args[0], in a child with stdout on fd for at
// most 5s: the child's exit status, or -1 when it was killed
static int  spawn(int fd, char** args, bool ext) {
  fflush(NULL);
  pid_t pid = fork();
  if(!pid) {
    alarm(5);
    dup2(fd, 1);
    if(ext) execv(args[0], args);
    else {
      int argc = 0;
      while(args[argc]) argc++;
      exit(tic(argc, args));
    }
    _exit(127);
  }
  int status;
  if(pid < 0 || waitpid(pid, &status, 0) < 0) return -1;
  return WIFEXITED(status)? WEXITSTATUS(status): -1;
}

// `tic -m exec`: the value main returned, as printed on the last line
static int  jit(void) {
  int fd[2];
  if(pipe(fd)) return odd;
  int code = spawn(fd[1], (char*[]){"tic", "-m", "exec", src_at, NULL}, 0);
  close(fd[1]);
  char out[256], *last = out;
  ssize_t n = read(fd[0], out, sizeof out - 1);
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
static int  elf(void) {
  int null = open("/dev/null", O_WRONLY);
  unlink(exe_at);
  int code = spawn(null, (char*[]){"tic", "-o", exe_at, src_at, NULL}, 0);
  close(null);
  if(code == 1) return access(exe_at, F_OK)? bad: odd; // and no executable
  if(code || chmod(exe_at, 0755)) return odd;
  code = spawn(1, (char*[]){exe_at, NULL}, 1);
  return code < 0? odd: code;
}

static char* shown(int value, char* buf) {
  if(value == bad) return "error";
  if(value == odd) return "no result";
  sprintf(buf, "%d", value);
  return buf;
}

// compile src both ways: NULL when each gives want, else why not
static char* tried(char* src, int want) {
  mkdir("bins", 0755);
  sprintf(src_at, "bins/" TAG "-%03zu.c", test_count);
  sprintf(exe_at, "bins/" TAG "-%03zu", test_count);
  FILE* fp = fopen(src_at, "w");
  if(!fp) return "cannot write the case to bins/";
  fputs(src, fp);
  fclose(fp);

  static char why[64];
  char w[16], g[16];
  int got = jit();
  char* how = "exec";
  if(got == want) { got = elf(); how = "elf"; }
  if(got == want) return NULL;
  snprintf(why, sizeof why, "%s: want %s, got " TEST_COLOR_FAIL("%s"),
           how, shown(want, w), shown(got, g));
  return why;
}

// expect(want, src): main returns want, or tic exits 1 when want is bad
#define expect(want, src) \
  if((error = tried(src, want))) location = "at " __FILE__ ":" TEST_STRING(__LINE__);

// 25-statement bodies: their branches are over 127 bytes on x86
#define RET5 "return 1; return 1; return 1; return 1; return 1; "
#define ADD5 "s += 1; s += 1; s += 1; s += 1; s += 1; "

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
    it("skips lists")
      expect(1, "list point { int x; int y; };\nlist { int z; };\n"
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
  }

  context("loops") {
    it("runs while")
      expect(55, "int main() {\n  int s = 0;\n  int i = 1;\n"
                 "  while(i <= 10) { s += i; i += 1; }\n  return s;\n}\n");
    it("runs for")
      expect(10, "int main() { int s = 0; for(int i = 0; i < 5; i += 1) s += i; return s; }\n");
    it("scopes the for variable")
      expect(7, "int main() { int i = 7; for(int i = 0; i < 3; i += 1) {} return i; }\n");
    it("runs for without parts")
      expect(9, "int main() { int i = 0; for(;;) { i += 1; if(i == 9) break; } return i; }\n");
    it("breaks and continues for")
      expect(30, "int main() {\n  int s = 0;\n  for(int i = 0; i < 100; i += 1) {\n"
                 "    if(i % 2) continue;\n    if(i > 10) break;\n    s += i;\n  }\n"
                 "  return s;\n}\n");
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
    it("rejects a function without a body")
      expect(bad, "int f(int a);\nint main() { return 0; }\n");
  }
}
