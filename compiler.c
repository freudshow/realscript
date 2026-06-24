#include "compiler.h"
#include <stdlib.h>
#include <string.h>
#include <stdio.h>

// Global variable symbol table
static char* globalNames[512];
static int globalNameCount = 0;

static char* duplicate_string(const char* s) {
    size_t len = strlen(s);
    char* copy = malloc(len + 1);
    if (copy) strcpy(copy, s);
    return copy;
}

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

static int get_global_index(const char* name) {
    for (int i = 0; i < globalNameCount; i++) {
        if (strcmp(globalNames[i], name) == 0) {
            return i;
        }
    }
    if (globalNameCount >= 512) {
        fprintf(stderr, "[Compiler Error] Too many global variables.\n");
        return 0;
    }
    globalNames[globalNameCount] = duplicate_string(name);
    return globalNameCount++;
}

void free_chunk(Chunk* chunk) {
    free(chunk->code);
    free(chunk->lines);
    for (int i = 0; i < chunk->valueCount; i++) {
        if (chunk->values[i].type == VAL_FUNC) {
            free_function((ObjFunction*)chunk->values[i].as.obj);
        }
    }
    free(chunk->values);
    init_chunk(chunk);
}

// Compiler state for tracking locals and scopes
typedef struct {
    const char* name;
    int depth;
} Local;

typedef struct Compiler {
    struct Compiler* enclosing;
    ObjFunction* function;
    Local locals[256];
    int localCount;
    int scopeDepth;
} Compiler;

static void init_compiler(Compiler* compiler, Compiler* enclosing, const char* name) {
    compiler->enclosing = enclosing;
    compiler->function = new_function(name);
    compiler->localCount = 0;
    compiler->scopeDepth = 0;
    
    // Local stack slot 0 is reserved for internal VM use (like function call frames)
    compiler->locals[0].name = "";
    compiler->locals[0].depth = 0;
    compiler->localCount = 1;
}

// Helper macros to write to compiler's current function chunk
#define emit_b(b) write_chunk(&compiler->function->chunk, (b), node->line)

static void emit_constant(Compiler* compiler, Value value, int line) {
    int index = add_constant(&compiler->function->chunk, value);
    if (index > 255) {
        fprintf(stderr, "[Compiler Error] Constant pool limit exceeded.\n");
        return;
    }
    write_chunk(&compiler->function->chunk, OP_CONSTANT, line);
    write_chunk(&compiler->function->chunk, (uint8_t)index, line);
}

static int emit_jump(Compiler* compiler, uint8_t op, int line) {
    write_chunk(&compiler->function->chunk, op, line);
    write_chunk(&compiler->function->chunk, 0xff, line);
    write_chunk(&compiler->function->chunk, 0xff, line);
    return compiler->function->chunk.count - 2;
}

static void patch_jump(Compiler* compiler, int offset) {
    // Jump relative to the instruction after the jump offset operands (which is offset + 2)
    int jump = compiler->function->chunk.count - offset - 2;
    if (jump > 65535) {
        fprintf(stderr, "[Compiler Error] Too much code to jump over.\n");
    }
    compiler->function->chunk.code[offset] = (jump >> 8) & 0xff;
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

static void begin_scope(Compiler* compiler) {
    compiler->scopeDepth++;
}

static void end_scope(Compiler* compiler) {
    compiler->scopeDepth--;
    while (compiler->localCount > 0 &&
           compiler->locals[compiler->localCount - 1].depth > compiler->scopeDepth) {
        // Pop locals when they go out of scope to balance the stack
        write_chunk(&compiler->function->chunk, OP_POP, compiler->function->chunk.lines[compiler->function->chunk.count - 1]);
        free((char*)compiler->locals[compiler->localCount - 1].name);
        compiler->localCount--;
    }
}

static int resolve_local(Compiler* compiler, const char* name) {
    for (int i = compiler->localCount - 1; i >= 0; i--) {
        Local* local = &compiler->locals[i];
        if (strcmp(local->name, name) == 0) {
            return i;
        }
    }
    return -1;
}

// Forward declaration of recursive compiler function
static void compile_node(Compiler* compiler, AstNode* node);

static void compile_node(Compiler* compiler, AstNode* node) {
    if (!node) return;

    switch (node->type) {
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

        case AST_DB_REF_ID: {
            int realNo = node->as.db_ref_id.realNo;
            emit_b(OP_GET_DB_ID);
            emit_b((realNo >> 8) & 0xff);
            emit_b(realNo & 0xff);
            break;
        }

        case AST_ASSIGN_DB_REF_ID: {
            compile_node(compiler, node->as.assign_db_id.value);
            int realNo = node->as.assign_db_id.realNo;
            emit_b(OP_SET_DB_ID);
            emit_b((realNo >> 8) & 0xff);
            emit_b(realNo & 0xff);
            break;
        }

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

        case AST_BINARY: {
            // Logical && and || require short-circuit logic
            if (node->as.binary.op == TOKEN_AMPERSAND_AMPERSAND) {
                compile_node(compiler, node->as.binary.left);
                int end_jump = emit_jump(compiler, OP_JUMP_IF_FALSE, node->line);
                emit_b(OP_POP); // Pop the left truth value if true
                compile_node(compiler, node->as.binary.right);
                patch_jump(compiler, end_jump);
            } else if (node->as.binary.op == TOKEN_PIPE_PIPE) {
                compile_node(compiler, node->as.binary.left);
                int else_jump = emit_jump(compiler, OP_JUMP_IF_FALSE, node->line);
                int end_jump = emit_jump(compiler, OP_JUMP, node->line);
                patch_jump(compiler, else_jump);
                emit_b(OP_POP); // Pop the left truth value if false
                compile_node(compiler, node->as.binary.right);
                patch_jump(compiler, end_jump);
            } else {
                compile_node(compiler, node->as.binary.left);
                compile_node(compiler, node->as.binary.right);

                switch (node->as.binary.op) {
                    case TOKEN_PLUS:      emit_b(OP_ADD); break;
                    case TOKEN_MINUS:     emit_b(OP_SUB); break;
                    case TOKEN_STAR:      emit_b(OP_MUL); break;
                    case TOKEN_SLASH:     emit_b(OP_DIV); break;
                    case TOKEN_PERCENT:   emit_b(OP_MOD); break;
                    case TOKEN_AMPERSAND: emit_b(OP_BIT_AND); break;
                    case TOKEN_PIPE:      emit_b(OP_BIT_OR); break;
                    case TOKEN_CARET:     emit_b(OP_BIT_XOR); break;
                    case TOKEN_LESS_LESS: emit_b(OP_SHL); break;
                    case TOKEN_GREATER_GREATER: emit_b(OP_SHR); break;
                    case TOKEN_EQUAL_EQUAL: emit_b(OP_EQUAL); break;
                    case TOKEN_BANG_EQUAL:  emit_b(OP_EQUAL); emit_b(OP_NOT); break;
                    case TOKEN_LESS:        emit_b(OP_LESS); break;
                    case TOKEN_LESS_EQUAL:  emit_b(OP_GREATER); emit_b(OP_NOT); break;
                    case TOKEN_GREATER:     emit_b(OP_GREATER); break;
                    case TOKEN_GREATER_EQUAL: emit_b(OP_LESS); emit_b(OP_NOT); break;
                    default: break;
                }
            }
            break;
        }

        case AST_UNARY: {
            if (node->as.unary.op == TOKEN_MINUS) {
                // Compile -expr as 0 - expr
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

        case AST_CALL: {
            // Load callee function object onto the stack first
            int argIndex = resolve_local(compiler, node->as.call.callee);
            if (argIndex != -1) {
                emit_b(OP_GET_LOCAL);
                emit_b((uint8_t)argIndex);
            } else {
                int index = get_global_index(node->as.call.callee);
                emit_b(OP_GET_GLOBAL);
                emit_b((uint8_t)index);
            }

            // Push arguments onto the stack
            int argCount = 0;
            AstNodeList* arg = node->as.call.arguments;
            while (arg != NULL) {
                compile_node(compiler, arg->node);
                argCount++;
                arg = arg->next;
            }

            // Call with arity argument
            emit_b(OP_CALL);
            emit_b((uint8_t)argCount);
            break;
        }

        case AST_EXPR_STMT: {
            compile_node(compiler, node->as.expr_stmt.expr);
            emit_b(OP_POP); // Semicolon statement discards its result
            break;
        }

        case AST_VAR_DECL: {
            if (compiler->scopeDepth > 0) {
                // Local Variable Declaration
                // Check if variable is already defined in the current local scope
                for (int i = compiler->localCount - 1; i >= 0; i--) {
                    Local* local = &compiler->locals[i];
                    if (local->depth < compiler->scopeDepth) break;
                    if (strcmp(local->name, node->as.var.name) == 0) {
                        fprintf(stderr, "[Compiler Error] Variable '%s' already declared in this scope.\n", node->as.var.name);
                        return;
                    }
                }
                
                // Add to local variables stack tracker
                compiler->locals[compiler->localCount].name = duplicate_string(node->as.var.name);
                compiler->locals[compiler->localCount].depth = compiler->scopeDepth;
                compiler->localCount++;

                // Compile initializer, or push NIL
                if (node->as.var.initializer != NULL) {
                    compile_node(compiler, node->as.var.initializer);
                } else {
                    emit_b(OP_NIL);
                }
            } else {
                // Global Variable Declaration
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

        case AST_FUN_DECL: {
            // Functions are compiled in their own compiler context
            Compiler fnCompiler;
            init_compiler(&fnCompiler, compiler, node->as.fun_decl.name);
            
            // Set parameters as local variables at depth 0
            ParamList* param = node->as.fun_decl.parameters;
            while (param != NULL) {
                fnCompiler.locals[fnCompiler.localCount].name = duplicate_string(param->name);
                fnCompiler.locals[fnCompiler.localCount].depth = 0;
                fnCompiler.localCount++;
                fnCompiler.function->arity++;
                param = param->next;
            }

            // Compile the function body
            compile_node(&fnCompiler, node->as.fun_decl.body);

            // Ensure returning nil if function ends without explicit return
            write_chunk(&fnCompiler.function->chunk, OP_NIL, node->line);
            write_chunk(&fnCompiler.function->chunk, OP_RETURN, node->line);

            // Wrap the compiled function inside a value
            Value fnVal;
            fnVal.type = VAL_FUNC;
            fnVal.as.obj = fnCompiler.function;

            // Push function object as constant in outer chunk
            emit_constant(compiler, fnVal, node->line);
            
            // Define global variable with function name pointing to this function object
            int index = get_global_index(node->as.fun_decl.name);
            emit_b(OP_DEFINE_GLOBAL);
            emit_b((uint8_t)index);

            // Clean up the inner compiler's local parameters tracker
            for (int i = 1; i < fnCompiler.localCount; i++) {
                free((char*)fnCompiler.locals[i].name);
            }
            break;
        }

        case AST_IF: {
            compile_node(compiler, node->as.if_stmt.condition);
            int then_jump = emit_jump(compiler, OP_JUMP_IF_FALSE, node->line);
            emit_b(OP_POP); // Pop condition in then branch

            compile_node(compiler, node->as.if_stmt.then_branch);

            int else_jump = emit_jump(compiler, OP_JUMP, node->line);
            patch_jump(compiler, then_jump);
            emit_b(OP_POP); // Pop condition in else branch

            if (node->as.if_stmt.else_branch != NULL) {
                compile_node(compiler, node->as.if_stmt.else_branch);
            }

            patch_jump(compiler, else_jump);
            break;
        }

        case AST_WHILE: {
            int loop_start = compiler->function->chunk.count;
            compile_node(compiler, node->as.while_stmt.condition);
            int exit_jump = emit_jump(compiler, OP_JUMP_IF_FALSE, node->line);
            emit_b(OP_POP); // Pop condition in body

            compile_node(compiler, node->as.while_stmt.body);

            emit_loop(compiler, loop_start, node->line);

            patch_jump(compiler, exit_jump);
            emit_b(OP_POP); // Pop condition when exiting
            break;
        }

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
                emit_b(OP_POP); // Pop condition in body
            }

            compile_node(compiler, node->as.for_stmt.body);

            if (node->as.for_stmt.increment != NULL) {
                compile_node(compiler, node->as.for_stmt.increment);
                emit_b(OP_POP); // Pop increment result
            }

            emit_loop(compiler, loop_start, node->line);

            if (exit_jump != -1) {
                patch_jump(compiler, exit_jump);
                emit_b(OP_POP); // Pop condition when exiting
            }

            end_scope(compiler);
            break;
        }

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

ObjFunction* compile(AstNodeList* ast) {
    Compiler compiler;
    init_compiler(&compiler, NULL, "<main>");

    AstNodeList* item = ast;
    while (item != NULL) {
        compile_node(&compiler, item->node);
        item = item->next;
    }

    // Emit top level script return
    write_chunk(&compiler.function->chunk, OP_NIL, 0);
    write_chunk(&compiler.function->chunk, OP_RETURN, 0);

    // Check if compilation failed
    ObjFunction* mainFn = compiler.function;
    
    // Clean up local tracker
    for (int i = 1; i < compiler.localCount; i++) {
        free((char*)compiler.locals[i].name);
    }

    return mainFn;
}

const char* get_global_name(int index) {
    if (index < 0 || index >= globalNameCount) return NULL;
    return globalNames[index];
}

int get_global_count(void) {
    return globalNameCount;
}
