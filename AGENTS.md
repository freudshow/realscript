# RealScript

A custom scripting language engine written in C99 — bytecode compiler + stack VM, inspired by clox (Crafting Interpreters).

## Build & run

```sh
make                    # gcc -Wall -Wextra -std=c99 -g -o realscript *.c
./realscript            # run built-in verification test suite
./realscript file.real  # run a source file
make clean              # rm -f *.o realscript
```

No test framework — the test suite lives in `main.c` (`run_test_case`). Run `./realscript` with no args to execute it.

## Pipeline

```
source → lexer → parser (AST) → compiler (bytecode Chunk / ObjFunction) → VM
```

## Module layout (compilation order in Makefile)

| Module | Role |
|---|---|
| `db` | Mock RealDataBase API (get/set by real number or link/dev/reg) |
| `value` | Value type (int/double/bool/nil/func), arithmetic, bitwise, comparison |
| `lexer` | Tokenizer |
| `ast` | AST node types and constructors |
| `parser` | Recursive descent parser → `AstNodeList` |
| `compiler` | Bytecode compiler (OpCode enum, Chunk, ObjFunction) |
| `vm` | Stack VM (64 call frames, 16K stack, 512 globals) |
| `main` | Entry point: file runner or built-in test suite |

## Key constraints

- C99 (`-std=c99`), no compiler-specific extensions
- Globals array fixed at 512 entries (`VM.globals[512]`)
- Call frames: 64 max, stack: 16384 max
- Source file extension: `.real`
