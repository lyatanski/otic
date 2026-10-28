// vim: ts=2:sw=2
// tic's spec: each case is written to bins/, compiled by tic's own main in a
// forked child (fail() exits, code may crash) and run through `-m exec` and
// as a static executable written with `-o`, each with and without -O0 (no
// constant folding, so the runtime code paths are tested too).
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
static int  jit(bool O0) {
  int fd[2];
  if(pipe(fd)) return odd;
  int code = spawn(fd[1], (char*[]){"tic", "-m", "exec", src_at, O0? "-O0": NULL, NULL}, 0);
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
static int  elf(bool O0) {
  int null = open("/dev/null", O_WRONLY);
  unlink(exe_at);
  int code = spawn(null, (char*[]){"tic", "-o", exe_at, src_at, O0? "-O0": NULL, NULL}, 0);
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

// compile src all four ways: NULL when each gives want, else why not
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
  for(int O0 = 0; O0 < 2; O0++) {
    int got = jit(O0);
    char* how = "exec";
    if(got == want) { got = elf(O0); how = "elf"; }
    if(got != want) {
      snprintf(why, sizeof why, "%s%s: want %s, got " TEST_COLOR_FAIL("%s"),
               how, O0? " -O0": "", shown(want, w), shown(got, g));
      return why;
    }
  }
  return NULL;
}

// expect(want, src): main returns want, or tic exits 1 when want is bad
#define expect(want, src) \
  if((error = tried(src, want))) location = "at " __FILE__ ":" TEST_STRING(__LINE__);

// a 25-statement body: its forward branch is over 127 bytes on x86
#define RET5 "return 1; return 1; return 1; return 1; return 1; "

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

  context("expressions") {
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
    it("rejects a jump out of rel8 range") // arm8 branches reach further
      expect(pick(bad, 1), "int main() {\n  if(1) { " RET5 RET5 RET5 RET5 RET5 "}\n"
                           "  return 0;\n}\n");
  }
}
