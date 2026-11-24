# otic

A small, portable, retargetable toolchain written in C: compiler, assembler
and linker in one binary, with no external dependencies.

It can run a source file directly (compile & load into memory) or write an
executable, shared library or object file.

- CPUs: `ia64` (x86-64), `arm8` (AArch64)
- Binary formats: `elfs` (ELF), `mach` (Mach-O), `pe32` (PE/COFF)

## Build

```sh
gcc -O3 -o otic main.c
```

With MSVC, from a Developer Command Prompt:

```bat
cl /std:clatest /O2 /Feotic main.c
```

## Usage

```sh
./otic hello.ot                 # compile and run in memory
chmod +x hello.ot && ./hello.ot # or run it as a script, through its #! line
./otic -e hello hello.ot        # write an executable for the host
chmod +x hello && ./hello
./otic -a arm8 -f mach -e hello hello.ot   # cross-compile
```

| option | description |
| --- | --- |
| `-a`, `--arch` | cpu architecture: `ia64`, `arm8` |
| `-f`, `--form` | binary format: `elfs`, `mach`, `pe32` |
| `-e`, `--emit` | write a file instead of running the code |
| `-s`, `--static` | without `main`, write an object instead of a shared library |
| `-g`, `--debug` | write debug information, DWARF 5: in an ELF or PE program, beside a Mach-O one or a dylib in a `.dSYM` bundle (not for objects yet; ignored when running) |
| `-p`, `--path` | directory to look for modules (`.ot`, `.o`) and libraries |
| `-l`, `--libc` | C library of a Linux program: `glibc`, `musl`, `bionic` (default: the host's) |
| `-h`, `--help` | display help |

## Language

Source files are linked with `link` and shared libraries are loaded with
`load`, instead of a preprocessor.

```c
// vec.ot
struct vec { int x; int y; };

int dot(vec* a, vec* b) { return a->x * b->x + a->y * b->y; }
```

```c
#!/usr/bin/env otic
// hello.ot
load "c";                    // a shared library: libc
int printf(char* f, ...);    // its functions, declared after the load

link "vec";                  // vec.ot beside this file, compiled once

struct node { int v; node* next; };   // a struct names a type, no typedef

int sum(node* n) {
  int s = 0;
  while(n) { s += n->v; n = n->next; }
  return s;
}

int main() {
  node c = {3}, b = {2, &c}, a = {.next = &b, .v = 1};
  int t[4] = {[2] = 0b101, 0o17};
  vec p = {1, 2};
  printf("%d %d %d\n", sum(&a), t[2] + t[3], dot(&p, &(vec){3, 4}));
  return 0;
}
```

- types: `void`, `bool`, `byte`, `char`, `nu16`, `int`, `long`, `real`,
  `code` (source text: a `char*` by name, which `#` declares),
  pointers, arrays, structs (anonymous, bit-fields)
- a struct member without a declarator gives its members to the outer struct
  at its own offset and alignment, which allows basic inheritance:
  `struct foo { int a; int b; }; struct bar { foo; int c; };` makes `bar`
  start with a `foo`, so `bar` has `a`, `b` and `c`, and a `bar*` can be
  used where a `foo*` is expected. An anonymous `struct { int a; };` member
  works the same way; a member name given twice is an error
- `real` is a 64-bit floating point number, C's `double`: literals `1.5`,
  `.5`, `1e3`; with an integer it computes as a real, and an assignment,
  argument or `return` converts between them (toward zero). It takes
  `+ - * /`, comparisons (a NaN compares unordered) and conditions (`-0.0` is
  false), and passes to and from C code the C way
- function pointers: `int (*f)(int, long)`,
  `int (*ops[4])(int)`; a function's name (or `&name`) is its address. Calls
  through a pointer follow the C ABI, so imports and C callbacks (`qsort`)
  work both ways (an otic function's address is a small entry for C code on
  Windows x86-64, or when it takes or gives a real)
- a global pointer, or a `static` local one, may start with an address: of a
  global (`&g`, `&t[2]`, `&p.y`), an array (`t + 1`), a string or a function
  defined before it, plus or minus constants. The loader moves it with the
  image (a relocation in ELF, a rebase in Mach-O, `.reloc` in PE); an
  import's address can't start one
- overloads: functions, and imports, may share a name when their parameters
  differ in count or in kind: int (32 bits or narrower), long (`long`, a
  pointer, an array) or real. A call picks the one its arguments fit best
  (the same kind, then an int for a long, then any other conversion), and
  must come after its definition; two that fit alike are an error. Each
  overload is named for the linker by its parameter types: `aton(int)` is
  `aton_int`, `put(char*, int)` is `put_charp_int`, `f(void)` is `f_void`,
  which C calls and otic code may name to call one or take its address; a
  name defined once keeps its plain symbol
- statements: `when`/`else`, `while`, `break`, `skip`, `return`
- `#sizeof` of a type name or an operand (not computed), and
  `#offsetof(type, member)`: the bytes from the start of a struct to a member,
  also on through its members and elements (`#offsetof(s, in.q[2].y)`). The
  compiler gives both as constants where they are, with no code run, for
  every target. Character literals (`'a'`, `'\n'`) are numbers
- designators set one field or element and zero the rest: `{.y = 5}`,
  `{[2] = 7}` (as in `a` and `t` above); a compound literal builds a value
  in place: `&(vec){3, 4}`
- `static` and `extern` at the top level: a `static` name stays in its
  source file; an `extern` global lives in a loaded library and has no
  storage of its own. A `static` local is a global that only its block
  sees: it keeps its value between calls, set once by constants
- after a `load`, a call of an undeclared name imports it from that library
  (`sprintf(b, "%d", 42)`): it gives an int and takes each call's arguments
  as they are, as if not variadic. That fits C functions that give an int or
  nothing, except variadic ones on Apple arm64 (and their real arguments on
  Windows arm64); a pointer result needs a declaration. So past a `load`, a
  function of the source is defined before its calls
- `syscall(n, ...)` goes straight to the kernel
- a `#!` first line is skipped, so a source can be a script: `#!/usr/bin/env otic`
- `#f(args)` calls `f` while compiling, as host code, and takes its result in
  place of the source: an int, long or real is a constant (an array size, an
  initializer, a condition), a `char*` is a string (as a literal would be,
  also starting a global), and `code` at the top level is source text,
  compiled there as declarations. The arguments are constants or other `#`
  calls; a local of the enclosing function is out of scope. Globals keep what
  earlier `#` calls did to them while compiling, and the program starts with
  its own. It works for every target: a build for another cpu or format takes
  its results from a host build of the same source

```c
#!/usr/bin/env otic
load "c";
int sprintf(char* out, char* fmt, ...);

int fib(int n) { when (n < 2) return n; return fib(n - 1) + fib(n - 2); }

// returns "int get_<name>() { return <value>; }"
code getter(char* name, int value) {
  static char text[256];     // outlives the call, as the result must
  sprintf(text, "int get_%s() { return %d; }", name, value);
  return text;
}

int table[#fib(10)];        // 55 elements
#getter("x", 3)             // declares get_x
#getter("y", #fib(40))      // declares get_y, returning 102334155: its
                            // 331 million calls are made once, while compiling

int main() { return get_x() + #sizeof table / 4 + get_y() % 100; }  // 113
```

- code run by `#` has the compiler's own functions, built in: function
  pointers that a `#` points at the compiler before it runs. A source may
  declare the same names for its own use, which hide them; a program that
  names none of them carries nothing of them. They run as a call would: they
  may change the registers a C function may, and keep the others and the
  stack. A program that calls them itself crashes: they are for `#` code only.
  A `#` may call one straight (`#arch()`). A type handle is good for the `#`
  run that got it, and sees the types declared before that `#`

| function | gives |
| --- | --- |
| `int arch()` | the cpu being compiled for: 0 `ia64`, 1 `arm8` (also in a cross build) |
| `void emit(byte* b, int n)` | places n bytes of machine code where the `#` is, in a function |
| `long type(char* name)` | a type's handle by its name (a struct, `int`, `code`...), 0 when there is none |
| `int members(long t)` | the members of struct t, those of a member without a declarator among them; -1 for no struct |
| `int member(long t, int i, field* f)` | fills `f` with member i of struct t; 0 past the end |
| `char* spell(long t)` | t as a declaration spells it: `"long[2][3]"`, `"vec*"`, `"int (*)(int, long)"` |
| `int offset(char* t, char* name)` | the offset of member name in the struct named t, an error when there is none |

| `field` member | |
| --- | --- |
| `char* name` | the member's name |
| `long type` | its type, for `members` and `spell` (a bit-field's: its integer type) |
| `int kind` | `'i'` integer, `'r'` real, `'p'` pointer, `'f'` function pointer, `'a'` array, `'s'` struct, `'b'` bit-field |
| `int offset` | bytes from the start of the struct (a bit-field: of its unit) |
| `int size` | bytes (a bit-field: of its unit) |
| `int count` | elements of an array, else 1 |
| `int bit`, `int width` | a bit-field's first bit in its unit, and its width |

```c
void cli() { static byte c[1] = {0xfa}; emit(c, 1); }   // x86-64
void halt() { while(1) { #cli(); } }

struct regs { long rax; long rsp; long rip; };
int at_sp = #offsetof(regs, rsp);                      // 8
```

- `#{ code }` is `code`, for a `#` to give as source: its text as
  written, with `@name` (and its members, `@f.name`, `@p->k`, no spaces) or
  `@(expression)` spliced in, also inside string literals (escaped there);
  `@@` is an `@`. A `char*` or `code` splices as it is, an integer or a real as its
  literal; next to a name it pastes onto it (`show_@name`). The code must lex
  as otic and its braces match. It runs at compile time only, through the
  compiler's functions: the loop that builds a quote stays outside it

```c
load "c";
int printf(char* fmt, ...);

struct vec { int x; int y; };

code shower(char* name) {
  long  t = type(name);
  field f;
  int   i = 0;
  code  body = #{};
  while(member(t, i, &f)) {
    body = #{ @body printf("@f.name = %d\n", p->@f.name); };
    i += 1;
  }
  return #{ void show_@name(@name* p) { @body } };
}

#shower("vec")                                         // declares show_vec

int main() { vec v = {1, 2}; show_vec(&v); return 0; }
```

## Tests

```sh
gcc -o spec test/main.c && OTIC=./otic ./spec
```
