# otic

A small, portable, retargetable toolchain written in C: compiler, assembler
and linker in one binary, with no external dependencies.

It can run a source file directly (compile & load into memory) or write an
executable, shared library or object file.

- CPUs: `ia64` (x86-64), `arm8` (AArch64)
- Binary formats: `elfs` (ELF), `mach` (Mach-O), `pe32` (PE/COFF)

## Build

```sh
gcc -std=gnu2x -Os -o tic main.c
```

## Usage

```sh
./tic hello.c                 # compile and run in memory
./tic -e hello hello.c        # write an executable for the host
chmod +x hello && ./hello
./tic -a arm8 -f mach -e hello hello.c   # cross-compile
```

| option | description |
| --- | --- |
| `-a`, `--arch` | cpu architecture: `ia64`, `arm8` |
| `-f`, `--form` | binary format: `elfs`, `mach`, `pe32` |
| `-e`, `--emit` | write a file instead of running the code |
| `-s`, `--static` | without `main`, write an object instead of a shared library |
| `-p`, `--path` | directory to look for libraries |
| `-h`, `--help` | display help |

## Language

Source files are linked with `link` and shared libraries are loaded with
`load`, instead of a preprocessor.

```c
// vec.c
struct vec { int x; int y; };

int dot(vec* a, vec* b) { return a->x * b->x + a->y * b->y; }
```

```c
// hello.c
load "c";                    // a shared library: libc
int printf(char* f, ...);    // its functions, declared after the load

link "vec";                  // vec.c beside this file, compiled once

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

- types: `void`, `bool`, `byte`, `char`, `nu16`, `int`, `long`, pointers,
  arrays, structs (anonymous, bit-fields)
- function pointers: `int (*f)(int, long)`,
  `int (*ops[4])(int)`; a function's name (or `&name`) is its address. Calls
  through a pointer follow the C ABI, so imports and C callbacks (`qsort`)
  work both ways (on Windows x86-64 an otic function's address is a small
  Win64 entry); a global can't be initialized with one yet
- statements: `when`/`else`, `while`, `break`, `skip`, `return`
- initializers with designators, compound literals, `static`, `extern`
- `syscall(n, ...)` goes straight to the kernel

## Tests

```sh
gcc -std=gnu2x -o spec test/main.c && TIC=./tic ./spec
```
