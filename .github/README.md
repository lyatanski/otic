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

## Tests

```sh
gcc -std=gnu2x -o spec test/main.c && TIC=./tic ./spec
```
