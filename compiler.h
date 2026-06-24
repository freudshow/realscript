#ifndef COMPILER_H
#define COMPILER_H

#include "value.h"
#include "ast.h"

typedef enum {
    OP_CONSTANT,
    OP_NIL,
    OP_TRUE,
    OP_FALSE,
    OP_POP,
    OP_GET_LOCAL,
    OP_SET_LOCAL,
    OP_GET_GLOBAL,
    OP_SET_GLOBAL,
    OP_DEFINE_GLOBAL,
    OP_GET_DB_ID,
    OP_SET_DB_ID,
    OP_GET_DB_LINK,
    OP_SET_DB_LINK,
    OP_ADD,
    OP_SUB,
    OP_MUL,
    OP_DIV,
    OP_MOD,
    OP_BIT_AND,
    OP_BIT_OR,
    OP_BIT_XOR,
    OP_BIT_NOT,
    OP_SHL,
    OP_SHR,
    OP_EQUAL,
    OP_LESS,
    OP_GREATER,
    OP_NOT,
    OP_JUMP,
    OP_JUMP_IF_FALSE,
    OP_LOOP,
    OP_CALL,
    OP_RETURN
} OpCode;

typedef struct {
    int count;
    int capacity;
    uint8_t* code;
    int* lines;
    
    int valueCount;
    int valueCapacity;
    Value* values;
} Chunk;

typedef struct {
    int arity;
    Chunk chunk;
    char* name;
} ObjFunction;

void init_chunk(Chunk* chunk);
void free_chunk(Chunk* chunk);
void write_chunk(Chunk* chunk, uint8_t byte, int line);
int add_constant(Chunk* chunk, Value value);

ObjFunction* new_function(const char* name);
void free_function(ObjFunction* function);

// Compiles the program AST (a list of top-level declarations and statements)
// into a single top-level ObjFunction representing the script.
ObjFunction* compile(AstNodeList* ast);

const char* get_global_name(int index);
int get_global_count(void);

#endif // COMPILER_H
