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
- Compilation order matters for header deps: `db → value → lexer → ast → parser → compiler → vm → main`

## Build & test commands

```sh
make                    # builds realscript
./realscript            # runs built-in test suite (7 test cases)
./realscript file.real  # runs a .real source file
make clean              # removes build artifacts
```

No separate test framework — the test suite lives in `main.c` (`run_test_case`). Run `./realscript` with no args to execute the full suite (7 test cases covering arithmetic, bitwise, logical/short-circuit, control flow, loops, functions/recursion, DB integration).

## Key constraints & limits

- C99 (`-std=c99`), `-Wall -Wextra -g`
- Globals array: fixed 512 entries (`VM.globals[512]`)
- Call frames: 64 max, stack: 16384 max
- Source file extension: `.real`
- Compilation order in Makefile matters for header dependencies

## Source file layout

```
db.c/h        → Mock RealDataBase API (get/set by real number or link/dev/reg)
value.c/h     → Value type (int/double/bool/nil/func), arithmetic, bitwise, comparison
lexer.c/h     → Tokenizer
ast.c/h       → AST node types and constructors
parser.c/h    → Recursive descent parser → AstNodeList
compiler.c/h  → Bytecode compiler (OpCode enum, Chunk, ObjFunction)
vm.c/h        → Stack VM (64 call frames, 16K stack, 512 globals)
main.c        → Entry point: file runner or built-in test suite
```

## Pipeline detail (main.c:run_source)

```
parse(source) → AstNodeList*
compile(ast)  → ObjFunction*
init_vm(&vm)
interpret(&vm, function) → InterpretResult
print globals on success
free_vm, free_function, free_ast_list
```

## VM limits (vm.h)

- `FRAMES_MAX = 64`
- `STACK_MAX = 16384`
- `GLOBALS_MAX = 512`

## Source file extension

Source files use `.real` extension (see `text.real` in repo).

## Build artifacts (gitignored)

- `realscript` (executable)
- `*.o` object files
- Eclipse project files (`.project`, `.cproject`, `.settings/`)

## Technical Stack

- **Language**: C99 (`-std=c99`, `-Wall -Wextra -g`)
- **Build System**: GNU Make (explicit compilation order due to header dependencies)
- **Architecture**: Bytecode compiler + stack-based VM (clox-style)
- **VM Limits**: 64 call frames, 16384 stack slots, 512 global variables
- **Source Extension**: `.real`
- **Testing**: Built-in test suite in `main.c` (7 test cases), no external test framework
- **Compiler**: GCC with `-std=c99 -Wall -Wextra -g`
- **Dependencies**: Zero external dependencies (stdlib only)
- **Build Artifacts**: `realscript` executable, `*.o` objects (gitignored)
- **IDE Artifacts**: Eclipse project files (gitignored)
- **Source Extension**: `.real` (see `text.real` in repo)