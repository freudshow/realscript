// ============================================================================
// compiler.c -- Bytecode Compiler Implementation
//
// Walks the AST and emits bytecode instructions into the current function's
// Chunk.  Key responsibilities:
//   1. Scope tracking (local variables vs. globals)
//   2. Emitting the correct opcodes and operands
//   3. Handling short-circuiting (&& / ||) and control flow (if/while/for)
//   4. Compiling nested functions in their own Compiler context
//
// The global variable symbol table (globalNames[]) is static to this module
// and persists across compilations (for post-run inspection in main.c).
// ============================================================================

#include "compiler.h"
#include <stdlib.h>
#include <string.h>
#include <stdio.h>

// ---------------------------------------------------------------------------
// Global variable symbol table
//
// When the compiler encounters a variable declared at depth 0 (global scope),
// it registers it here by name and references it by index in the bytecode.
// The index is used by OP_GET_GLOBAL / OP_SET_GLOBAL / OP_DEFINE_GLOBAL.
// ---------------------------------------------------------------------------
static char* globalNames[512];     // Names of all globals (by index)
static int globalNameCount = 0;    // Number of globally declared variables

// ---------------------------------------------------------------------------
// duplicate_string: malloc + strcpy helper
// ---------------------------------------------------------------------------
static char* duplicate_string(const char* s) {
    size_t len = strlen(s);
    char* copy = malloc(len + 1);
    if (copy) strcpy(copy, s);
    return copy;
}

// ============================================================================
// Chunk management
// ============================================================================

void init_chunk(Chunk* chunk) {
    chunk->count = 0;
    chunk->capacity = 0;
    chunk->code = NULL;
    chunk->lines = NULL;
    chunk->valueCount = 0;
    chunk->valueCapacity = 0;
    chunk->values = NULL;
}

void write_chunk(Chunk* chunk, uint8_t byte, int line) {
    if (chunk->count + 1 > chunk->capacity) {
        chunk->capacity = chunk->capacity < 8 ? 8 : chunk->capacity * 2;
        chunk->code = realloc(chunk->code, chunk->capacity);
        chunk->lines = realloc(chunk->lines, chunk->capacity * sizeof(int));
    }
    chunk->code[chunk->count] = byte;
    chunk->lines[chunk->count] = line;
    chunk->count++;
}

int add_constant(Chunk* chunk, Value value) {
    if (chunk->valueCount + 1 > chunk->valueCapacity) {
        chunk->valueCapacity = chunk->valueCapacity < 8 ? 8 : chunk->valueCapacity * 2;
        chunk->values = realloc(chunk->values, chunk->valueCapacity * sizeof(Value));
    }
    chunk->values[chunk->valueCount] = value;
    return chunk->valueCount++;
}

// ============================================================================
// ObjFunction management
// ============================================================================

ObjFunction* new_function(const char* name) {
    ObjFunction* function = malloc(sizeof(ObjFunction));
    function->arity = 0;
    init_chunk(&function->chunk);
    function->name = name ? duplicate_string(name) : NULL;
    return function;
}

void free_function(ObjFunction* function) {
    if (!function) return;
    free_chunk(&function->chunk);
    free(function->name);
    free(function);
}

// ============================================================================
// Global index helpers
// ============================================================================

// get_global_index: return index for a global name (create if new)
static int get_global_index(const char* name) {
    for (int i = 0; i < globalNameCount; i++) {
        if (strcmp(globalNames[i], name) == 0) {
            return i;       // Already exists
        }
    }
    if (globalNameCount >= 512) {
        fprintf(stderr, "[Compiler Error] Too many global variables.\n");
        return 0;
    }
    globalNames[globalNameCount] = duplicate_string(name);
    return globalNameCount++;
}

// ---------------------------------------------------------------------------
// free_chunk -- frees bytecode, line array, and any functions in the
//               constant pool (which recursively frees their chunks too)
// ---------------------------------------------------------------------------
void free_chunk(Chunk* chunk) {
    free(chunk->code);
    free(chunk->lines);
    for (int i = 0; i < chunk->valueCount; i++) {
        if (chunk->values[i].type == VAL_FUNC) {
            free_function((ObjFunction*)chunk->values[i].as.obj);
        }
    }
    free(chunk->values);
    init_chunk(chunk);   // Reset to safe empty state
}

// ============================================================================
// Compiler state -- tracks local variables and scope depth
//
// Local variables are allocated on the VM stack in order.  Each local is
// identified by its stack slot index.  When a scope ends, all locals with
// depth > current scope are popped from the stack via OP_POP and removed
// from the tracker.
//
// Slot 0 is reserved for the call frame's internal use (it holds the
// function object on the stack during a call), so user locals start at 1.
// ============================================================================
typedef struct {
    const char* name;    // Variable name
    int depth;           // Scope depth at which this local was declared
} Local;

typedef struct Compiler {
    struct Compiler* enclosing;  // Parent compiler (NULL for top-level)
    ObjFunction* function;       // The function being compiled
    Local locals[256];           // Local variable tracker (max 256 per function)
    int localCount;              // Number of locals currently tracked
    int scopeDepth;              // Current nesting depth (0 = global)
} Compiler;

// ---------------------------------------------------------------------------
// init_compiler -- set up a new Compiler context
// ---------------------------------------------------------------------------
static void init_compiler(Compiler* compiler, Compiler* enclosing, const char* name) {
    compiler->enclosing = enclosing;
    compiler->function = new_function(name);
    compiler->localCount = 0;
    compiler->scopeDepth = 0;

    // Slot 0 is reserved for the call frame's function object
    compiler->locals[0].name = "";
    compiler->locals[0].depth = 0;
    compiler->localCount = 1;
}

// ---------------------------------------------------------------------------
// emit_b — shorthand to write one byte to the current function's chunk
//          using the enclosing function's line number (from 'node->line')
// ---------------------------------------------------------------------------
#define emit_b(b) write_chunk(&compiler->function->chunk, (b), node->line)

// ---------------------------------------------------------------------------
// emit_constant -- write an OP_CONSTANT instruction with a pool index
// ---------------------------------------------------------------------------
static void emit_constant(Compiler* compiler, Value value, int line) {
    int index = add_constant(&compiler->function->chunk, value);
    if (index > 255) {
        fprintf(stderr, "[Compiler Error] Constant pool limit exceeded.\n");
        return;
    }
    write_chunk(&compiler->function->chunk, OP_CONSTANT, line);
    write_chunk(&compiler->function->chunk, (uint8_t)index, line);
}

// ---------------------------------------------------------------------------
// Jump helpers
//
// emit_jump   : write a forward jump (OP_JUMP or OP_JUMP_IF_FALSE) with
//               placeholder 2-byte offset; returns offset for patching
// patch_jump  : fill in the two-byte offset at a previously emitted jump site
// emit_loop   : write a backward jump (OP_LOOP) with computed offset
// ---------------------------------------------------------------------------
static int emit_jump(Compiler* compiler, uint8_t op, int line) {
    write_chunk(&compiler->function->chunk, op, line);
    write_chunk(&compiler->function->chunk, 0xff, line);   // High byte placeholder
    write_chunk(&compiler->function->chunk, 0xff, line);   // Low byte placeholder
    return compiler->function->chunk.count - 2;            // Point to high byte
}

static void patch_jump(Compiler* compiler, int offset) {
    int jump = compiler->function->chunk.count - offset - 2;
    if (jump > 65535) {
        fprintf(stderr, "[Compiler Error] Too much code to jump over.\n");
    }
    compiler->function->chunk.code[offset]     = (jump >> 8) & 0xff;
    compiler->function->chunk.code[offset + 1] = jump & 0xff;
}

static void emit_loop(Compiler* compiler, int loop_start, int line) {
    write_chunk(&compiler->function->chunk, OP_LOOP, line);
    int offset = compiler->function->chunk.count - loop_start + 2;
    if (offset > 65535) {
        fprintf(stderr, "[Compiler Error] Loop body too large.\n");
    }
    write_chunk(&compiler->function->chunk, (offset >> 8) & 0xff, line);
    write_chunk(&compiler->function->chunk, offset & 0xff, line);
}

// ============================================================================
// Scope management
// ============================================================================

static void begin_scope(Compiler* compiler) {
    compiler->scopeDepth++;
}

static void end_scope(Compiler* compiler) {
    compiler->scopeDepth--;
    // Pop all locals whose depth is deeper than the new current scope
    while (compiler->localCount > 0 &&
           compiler->locals[compiler->localCount - 1].depth > compiler->scopeDepth) {
        write_chunk(&compiler->function->chunk, OP_POP,
                     compiler->function->chunk.lines[compiler->function->chunk.count - 1]);
        free((char*)compiler->locals[compiler->localCount - 1].name);
        compiler->localCount--;
    }
}

// ---------------------------------------------------------------------------
// resolve_local -- search the local tracker for a variable by name
// Returns the stack slot index, or -1 if not found (meaning it's a global).
// ---------------------------------------------------------------------------
static int resolve_local(Compiler* compiler, const char* name) {
    for (int i = compiler->localCount - 1; i >= 0; i--) {
        Local* local = &compiler->locals[i];
        if (strcmp(local->name, name) == 0) {
            return i;      // Found! Return stack slot index
        }
    }
    return -1;             // Not local — must be a global
}

// ============================================================================
// Forward declaration
// ============================================================================
static void compile_node(Compiler* compiler, AstNode* node);

// ============================================================================
// compile_node -- recursive AST-to-bytecode compilation
//
// This is the heart of the compiler.  Each case handles a different
// AstNodeType and emits the appropriate sequence of opcodes.
// ============================================================================
static void compile_node(Compiler* compiler, AstNode* node) {
    if (!node) return;

    switch (node->type) {

        // ================================================================
        // AST_LITERAL — push a constant value onto the stack
        // ================================================================
        case AST_LITERAL: {
            if (node->as.literal.type == VAL_NIL) {
                emit_b(OP_NIL);
            } else if (node->as.literal.type == VAL_BOOL) {
                emit_b(node->as.literal.as.boolean ? OP_TRUE : OP_FALSE);
            } else {
                emit_constant(compiler, node->as.literal, node->line);
            }
            break;
        }

        // ================================================================
        // AST_VAR_REF — push a variable's value onto the stack
        // ================================================================
        case AST_VAR_REF: {
            int arg = resolve_local(compiler, node->as.var.name);
            if (arg != -1) {
                emit_b(OP_GET_LOCAL);
                emit_b((uint8_t)arg);
            } else {
                int index = get_global_index(node->as.var.name);
                emit_b(OP_GET_GLOBAL);
                emit_b((uint8_t)index);
            }
            break;
        }

        // ================================================================
        // AST_ASSIGN_VAR — assign a value to a variable
        // ================================================================
        case AST_ASSIGN_VAR: {
            compile_node(compiler, node->as.assign_var.value);
            int arg = resolve_local(compiler, node->as.assign_var.name);
            if (arg != -1) {
                emit_b(OP_SET_LOCAL);
                emit_b((uint8_t)arg);
            } else {
                int index = get_global_index(node->as.assign_var.name);
                emit_b(OP_SET_GLOBAL);
                emit_b((uint8_t)index);
            }
            break;
        }

        // ================================================================
        // AST_DB_REF_ID — read from database by flat index (#N)
        // ================================================================
        case AST_DB_REF_ID: {
            int realNo = node->as.db_ref_id.realNo;
            emit_b(OP_GET_DB_ID);
            emit_b((realNo >> 8) & 0xff);   // High byte
            emit_b(realNo & 0xff);           // Low byte
            break;
        }

        // ================================================================
        // AST_ASSIGN_DB_REF_ID — write to database by flat index (#N = val)
        // ================================================================
        case AST_ASSIGN_DB_REF_ID: {
            compile_node(compiler, node->as.assign_db_id.value);
            int realNo = node->as.assign_db_id.realNo;
            emit_b(OP_SET_DB_ID);
            emit_b((realNo >> 8) & 0xff);
            emit_b(realNo & 0xff);
            break;
        }

        // ================================================================
        // AST_DB_REF_LINK — read from database by (#(L, D, R))
        // ================================================================
        case AST_DB_REF_LINK: {
            int l = node->as.db_ref_link.linkNo;
            int d = node->as.db_ref_link.devNo;
            int r = node->as.db_ref_link.regNo;
            emit_b(OP_GET_DB_LINK);
            emit_b((l >> 8) & 0xff); emit_b(l & 0xff);
            emit_b((d >> 8) & 0xff); emit_b(d & 0xff);
            emit_b((r >> 8) & 0xff); emit_b(r & 0xff);
            break;
        }

        // ================================================================
        // AST_ASSIGN_DB_REF_LINK — write to database by (#(L, D, R) = val)
        // ================================================================
        case AST_ASSIGN_DB_REF_LINK: {
            compile_node(compiler, node->as.assign_db_link.value);
            int l = node->as.assign_db_link.linkNo;
            int d = node->as.assign_db_link.devNo;
            int r = node->as.assign_db_link.regNo;
            emit_b(OP_SET_DB_LINK);
            emit_b((l >> 8) & 0xff); emit_b(l & 0xff);
            emit_b((d >> 8) & 0xff); emit_b(d & 0xff);
            emit_b((r >> 8) & 0xff); emit_b(r & 0xff);
            break;
        }

        // ================================================================
        // AST_BINARY — binary operator
        //
        // Special cases for && and || which require short-circuit branch logic.
        // All other operators push both operands then emit the appropriate op.
        // ================================================================
        case AST_BINARY: {
            if (node->as.binary.op == TOKEN_AMPERSAND_AMPERSAND) {
                // x && y  →  if (!x) jump to end; pop x; y
                compile_node(compiler, node->as.binary.left);
                int end_jump = emit_jump(compiler, OP_JUMP_IF_FALSE, node->line);
                emit_b(OP_POP);
                compile_node(compiler, node->as.binary.right);
                patch_jump(compiler, end_jump);

            } else if (node->as.binary.op == TOKEN_PIPE_PIPE) {
                // x || y  →  if (x) jump to end; pop x; y
                compile_node(compiler, node->as.binary.left);
                int else_jump = emit_jump(compiler, OP_JUMP_IF_FALSE, node->line);
                int end_jump = emit_jump(compiler, OP_JUMP, node->line);
                patch_jump(compiler, else_jump);
                emit_b(OP_POP);
                compile_node(compiler, node->as.binary.right);
                patch_jump(compiler, end_jump);

            } else {
                compile_node(compiler, node->as.binary.left);
                compile_node(compiler, node->as.binary.right);

                switch (node->as.binary.op) {
                    case TOKEN_PLUS:              emit_b(OP_ADD); break;
                    case TOKEN_MINUS:             emit_b(OP_SUB); break;
                    case TOKEN_STAR:              emit_b(OP_MUL); break;
                    case TOKEN_SLASH:             emit_b(OP_DIV); break;
                    case TOKEN_PERCENT:           emit_b(OP_MOD); break;
                    case TOKEN_AMPERSAND:         emit_b(OP_BIT_AND); break;
                    case TOKEN_PIPE:              emit_b(OP_BIT_OR); break;
                    case TOKEN_CARET:             emit_b(OP_BIT_XOR); break;
                    case TOKEN_LESS_LESS:         emit_b(OP_SHL); break;
                    case TOKEN_GREATER_GREATER:   emit_b(OP_SHR); break;
                    case TOKEN_EQUAL_EQUAL:       emit_b(OP_EQUAL); break;
                    // a != b  →  a == b; not
                    case TOKEN_BANG_EQUAL:        emit_b(OP_EQUAL); emit_b(OP_NOT); break;
                    case TOKEN_LESS:              emit_b(OP_LESS); break;
                    // a <= b  →  a > b; not
                    case TOKEN_LESS_EQUAL:        emit_b(OP_GREATER); emit_b(OP_NOT); break;
                    case TOKEN_GREATER:           emit_b(OP_GREATER); break;
                    // a >= b  →  a < b; not
                    case TOKEN_GREATER_EQUAL:     emit_b(OP_LESS); emit_b(OP_NOT); break;
                    default: break;
                }
            }
            break;
        }

        // ================================================================
        // AST_UNARY — unary operator (-expr, !expr, ~expr)
        // ================================================================
        case AST_UNARY: {
            if (node->as.unary.op == TOKEN_MINUS) {
                // Negation: compile as "0 - expr"
                emit_constant(compiler, int_val(0), node->line);
                compile_node(compiler, node->as.unary.operand);
                emit_b(OP_SUB);
            } else if (node->as.unary.op == TOKEN_BANG) {
                compile_node(compiler, node->as.unary.operand);
                emit_b(OP_NOT);
            } else if (node->as.unary.op == TOKEN_TILDE) {
                compile_node(compiler, node->as.unary.operand);
                emit_b(OP_BIT_NOT);
            }
            break;
        }

        // ================================================================
        // AST_CALL — function call
        //
        // Emits: push callee, push args..., OP_CALL N
        // ================================================================
        case AST_CALL: {
            int argIndex = resolve_local(compiler, node->as.call.callee);
            if (argIndex != -1) {
                emit_b(OP_GET_LOCAL);
                emit_b((uint8_t)argIndex);
            } else {
                int index = get_global_index(node->as.call.callee);
                emit_b(OP_GET_GLOBAL);
                emit_b((uint8_t)index);
            }

            int argCount = 0;
            AstNodeList* arg = node->as.call.arguments;
            while (arg != NULL) {
                compile_node(compiler, arg->node);
                argCount++;
                arg = arg->next;
            }

            emit_b(OP_CALL);
            emit_b((uint8_t)argCount);
            break;
        }

        // ================================================================
        // AST_EXPR_STMT — expression used as statement (discard result)
        // ================================================================
        case AST_EXPR_STMT: {
            compile_node(compiler, node->as.expr_stmt.expr);
            emit_b(OP_POP);
            break;
        }

        // ================================================================
        // AST_VAR_DECL — variable declaration
        //
        // If scopeDepth > 0, this is a local variable (assigned a stack slot).
        // Otherwise it is a global variable (stored in VM.globals[]).
        // ================================================================
        case AST_VAR_DECL: {
            if (compiler->scopeDepth > 0) {
                // ---- Local variable ----
                // Re-declaration check in current scope
                for (int i = compiler->localCount - 1; i >= 0; i--) {
                    Local* local = &compiler->locals[i];
                    if (local->depth < compiler->scopeDepth) break;
                    if (strcmp(local->name, node->as.var.name) == 0) {
                        fprintf(stderr, "[Compiler Error] Variable '%s' already declared in this scope.\n",
                                node->as.var.name);
                        return;
                    }
                }

                // Register local in tracker
                compiler->locals[compiler->localCount].name = duplicate_string(node->as.var.name);
                compiler->locals[compiler->localCount].depth = compiler->scopeDepth;
                compiler->localCount++;

                // Compile initialiser (or default to nil)
                if (node->as.var.initializer != NULL) {
                    compile_node(compiler, node->as.var.initializer);
                } else {
                    emit_b(OP_NIL);
                }
            } else {
                // ---- Global variable ----
                if (node->as.var.initializer != NULL) {
                    compile_node(compiler, node->as.var.initializer);
                } else {
                    emit_b(OP_NIL);
                }
                int index = get_global_index(node->as.var.name);
                emit_b(OP_DEFINE_GLOBAL);
                emit_b((uint8_t)index);
            }
            break;
        }

        // ================================================================
        // AST_FUN_DECL — function declaration
        //
        // Creates a nested Compiler, compiles the body, then wraps the
        // resulting ObjFunction into a constant and stores it in a global.
        // ================================================================
        case AST_FUN_DECL: {
            Compiler fnCompiler;
            init_compiler(&fnCompiler, compiler, node->as.fun_decl.name);

            // Register parameters as local variables at depth 0
            ParamList* param = node->as.fun_decl.parameters;
            while (param != NULL) {
                fnCompiler.locals[fnCompiler.localCount].name = duplicate_string(param->name);
                fnCompiler.locals[fnCompiler.localCount].depth = 0;
                fnCompiler.localCount++;
                fnCompiler.function->arity++;
                param = param->next;
            }

            compile_node(&fnCompiler, node->as.fun_decl.body);

            // Implicit return nil at end of function
            write_chunk(&fnCompiler.function->chunk, OP_NIL, node->line);
            write_chunk(&fnCompiler.function->chunk, OP_RETURN, node->line);

            // Package the compiled function as a Value
            Value fnVal;
            fnVal.type = VAL_FUNC;
            fnVal.as.obj = fnCompiler.function;

            // Emit function as constant, then define it as a global
            emit_constant(compiler, fnVal, node->line);
            int index = get_global_index(node->as.fun_decl.name);
            emit_b(OP_DEFINE_GLOBAL);
            emit_b((uint8_t)index);

            // Clean up inner compiler's local names (slot 0 is reserved)
            for (int i = 1; i < fnCompiler.localCount; i++) {
                free((char*)fnCompiler.locals[i].name);
            }
            break;
        }

        // ================================================================
        // AST_IF — if/else control flow
        //
        //   ┌─ condition
        //   ├─ OP_JUMP_IF_FALSE ──→ else_branch (or end)
        //   ├─ OP_POP
        //   ├─ then_branch
        //   ├─ OP_JUMP ───────→ end
        //   ├─ else_branch (if present)
        //   └─ end
        // ================================================================
        case AST_IF: {
            compile_node(compiler, node->as.if_stmt.condition);
            int then_jump = emit_jump(compiler, OP_JUMP_IF_FALSE, node->line);
            emit_b(OP_POP);

            compile_node(compiler, node->as.if_stmt.then_branch);

            int else_jump = emit_jump(compiler, OP_JUMP, node->line);
            patch_jump(compiler, then_jump);
            emit_b(OP_POP);

            if (node->as.if_stmt.else_branch != NULL) {
                compile_node(compiler, node->as.if_stmt.else_branch);
            }

            patch_jump(compiler, else_jump);
            break;
        }

        // ================================================================
        // AST_WHILE — while loop
        //
        //   loop_start:
        //   ├─ condition
        //   ├─ OP_JUMP_IF_FALSE ──→ exit
        //   ├─ OP_POP
        //   ├─ body
        //   ├─ OP_LOOP ─────→ loop_start
        //   ├─ exit:
        //   └─ OP_POP
        // ================================================================
        case AST_WHILE: {
            int loop_start = compiler->function->chunk.count;
            compile_node(compiler, node->as.while_stmt.condition);
            int exit_jump = emit_jump(compiler, OP_JUMP_IF_FALSE, node->line);
            emit_b(OP_POP);

            compile_node(compiler, node->as.while_stmt.body);

            emit_loop(compiler, loop_start, node->line);

            patch_jump(compiler, exit_jump);
            emit_b(OP_POP);
            break;
        }

        // ================================================================
        // AST_FOR — for loop
        //
        //   begin_scope
        //   ├─ initializer
        //   ├─ loop_start:
        //   ├─ ├─ condition
        //   ├─ ├─ OP_JUMP_IF_FALSE ──→ exit
        //   ├─ ├─ OP_POP
        //   ├─ ├─ body
        //   ├─ ├─ increment
        //   ├─ └─ OP_POP
        //   ├─ OP_LOOP ─────→ loop_start
        //   ├─ exit:
        //   ├─ OP_POP  (if condition existed)
        //   └─ end_scope
        // ================================================================
        case AST_FOR: {
            begin_scope(compiler);

            if (node->as.for_stmt.initializer != NULL) {
                compile_node(compiler, node->as.for_stmt.initializer);
            }

            int loop_start = compiler->function->chunk.count;
            int exit_jump = -1;

            if (node->as.for_stmt.condition != NULL) {
                compile_node(compiler, node->as.for_stmt.condition);
                exit_jump = emit_jump(compiler, OP_JUMP_IF_FALSE, node->line);
                emit_b(OP_POP);
            }

            compile_node(compiler, node->as.for_stmt.body);

            if (node->as.for_stmt.increment != NULL) {
                compile_node(compiler, node->as.for_stmt.increment);
                emit_b(OP_POP);
            }

            emit_loop(compiler, loop_start, node->line);

            if (exit_jump != -1) {
                patch_jump(compiler, exit_jump);
                emit_b(OP_POP);
            }

            end_scope(compiler);
            break;
        }

        // ================================================================
        // AST_BLOCK — enter new scope, compile statements, leave scope
        // ================================================================
        case AST_BLOCK: {
            begin_scope(compiler);
            AstNodeList* item = node->as.block.statements;
            while (item != NULL) {
                compile_node(compiler, item->node);
                item = item->next;
            }
            end_scope(compiler);
            break;
        }

        // ================================================================
        // AST_RETURN — return from function
        // ================================================================
        case AST_RETURN: {
            if (node->as.expr_stmt.expr != NULL) {
                compile_node(compiler, node->as.expr_stmt.expr);
            } else {
                emit_b(OP_NIL);
            }
            emit_b(OP_RETURN);
            break;
        }
    }
}

// ============================================================================
// compile — entry point: compile an entire top-level AST list
//
// Compiles each top-level statement/declaration, then appends an implicit
// return nil.  Returns the main ObjFunction for the VM to interpret.
// ============================================================================
ObjFunction* compile(AstNodeList* ast) {
    Compiler compiler;
    init_compiler(&compiler, NULL, "<main>");

    AstNodeList* item = ast;
    while (item != NULL) {
        compile_node(&compiler, item->node);
        item = item->next;
    }

    // Top-level script implicitly returns nil
    write_chunk(&compiler.function->chunk, OP_NIL, 0);
    write_chunk(&compiler.function->chunk, OP_RETURN, 0);

    ObjFunction* mainFn = compiler.function;

    // Clean up local tracker (slot 0 is reserved)
    for (int i = 1; i < compiler.localCount; i++) {
        free((char*)compiler.locals[i].name);
    }

    return mainFn;
}

// ============================================================================
// Global symbol table queries (used by main.c for post-run inspection)
// ============================================================================

const char* get_global_name(int index) {
    if (index < 0 || index >= globalNameCount) return NULL;
    return globalNames[index];
}

int get_global_count(void) {
    return globalNameCount;
}
