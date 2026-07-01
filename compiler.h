// ============================================================================
// compiler.h -- Bytecode Compiler Header
//
// The compiler walks the AST produced by the parser and emits bytecode
// instructions into a Chunk embedded in an ObjFunction.
//
// Pipeline:  AST → compiler → ObjFunction (containing a Chunk of OpCodes)
//
// Key data structures:
//   OpCode       — every instruction the VM can execute
//   Chunk        — a dynamic array of bytes (opcodes + operand bytes) with
//                  a parallel line-number array (for debug info) and a
//                  constant pool (Value array)
//   ObjFunction  — a compiled function (name, arity, chunk of bytecode)
//   Compiler     — compilation context tracking locals, scope depth, and
//                  the enclosing (parent) compiler for nested functions
// ============================================================================

#ifndef COMPILER_H
#define COMPILER_H

#include "value.h"
#include "ast.h"

// ---------------------------------------------------------------------------
// OpCode enum — every bytecode instruction
// ---------------------------------------------------------------------------
typedef enum {
    // ---- Stack / Value manipulation ----
    OP_CONSTANT,          // Push a constant from the chunk's pool  (operand: byte index)
    OP_NIL,               // Push nil
    OP_TRUE,              // Push true
    OP_FALSE,             // Push false
    OP_POP,               // Pop and discard the top of stack

    // ---- Variable access ----
    OP_GET_LOCAL,         // Read local variable   (operand: byte slot index)
    OP_SET_LOCAL,         // Write local variable  (operand: byte slot index)
    OP_GET_GLOBAL,        // Read global variable  (operand: byte global index)
    OP_SET_GLOBAL,        // Write global variable (operand: byte global index)
    OP_DEFINE_GLOBAL,     // Define a new global   (pop value, store at global index)

    // ---- Database access ----
    OP_GET_DB_ID,         // Read DB by real number  (operand: uint16 realNo)
    OP_SET_DB_ID,         // Write DB by real number (operand: uint16 realNo)
    OP_GET_DB_LINK,       // Read DB by link-dev-reg (operand: 3× uint16)
    OP_SET_DB_LINK,       // Write DB by link-dev-reg (operand: 3× uint16)

    // ---- Arithmetic ----
    OP_ADD, OP_SUB, OP_MUL, OP_DIV, OP_MOD,

    // ---- Bitwise ----
    OP_BIT_AND, OP_BIT_OR, OP_BIT_XOR, OP_BIT_NOT, OP_SHL, OP_SHR,

    // ---- Comparison ----
    OP_EQUAL,             // a == b
    OP_LESS,              // a < b
    OP_GREATER,           // a > b

    // ---- Logical / Jump ----
    OP_NOT,               // Logical not (!)
    OP_JUMP,              // Unconditional jump     (operand: int16 offset)
    OP_JUMP_IF_FALSE,     // Conditional jump       (operand: int16 offset)
    OP_LOOP,              // Loop backward jump     (operand: int16 offset)

    // ---- Functions ----
    OP_CALL,              // Call a function        (operand: byte arg count)
    OP_RETURN             // Return from function   (no operand)
} OpCode;

// ---------------------------------------------------------------------------
// Chunk — a dynamic array of bytecode + metadata
//
//   code/values     : dynamic arrays (doubling when full)
//   lines           : parallel array mapping each code byte to a source line
//   valueCount/capacity : managing the constant pool
// ---------------------------------------------------------------------------
typedef struct {
    int count;          // Number of bytes used in code[]
    int capacity;       // Allocated capacity of code[] and lines[]
    uint8_t* code;      // Bytecode instruction stream
    int* lines;         // Line number for each byte (debug info)

    int valueCount;     // Number of constants in pool
    int valueCapacity;  // Allocated capacity of values[]
    Value* values;      // Constant pool (referenced by OP_CONSTANT)
} Chunk;

// ---------------------------------------------------------------------------
// ObjFunction — a compiled function ready to be executed by the VM
//
//   name  : function name ("<main>" for the top-level script)
//   arity : number of parameters the function expects
//   chunk : the bytecode for this function's body
// ---------------------------------------------------------------------------
typedef struct {
    int arity;          // Number of parameters
    Chunk chunk;        // Bytecode chunk for this function
    char* name;         // Function name (heap-allocated)
} ObjFunction;

// ---------------------------------------------------------------------------
// Chunk management
// ---------------------------------------------------------------------------
void init_chunk(Chunk* chunk);                // Initialise an empty chunk
void free_chunk(Chunk* chunk);                // Free all resources
void write_chunk(Chunk* chunk, uint8_t byte, int line);  // Append a byte
int add_constant(Chunk* chunk, Value value);  // Add constant, return its index

// ---------------------------------------------------------------------------
// Function management
// ---------------------------------------------------------------------------
ObjFunction* new_function(const char* name);  // Create a new function
void free_function(ObjFunction* function);    // Free the function and its chunk

// ---------------------------------------------------------------------------
// compile — entry point: compile an AST into a top-level ObjFunction
//
// The returned ObjFunction represents the entire script and can be passed
// directly to interpret() in the VM.
// ---------------------------------------------------------------------------
ObjFunction* compile(AstNodeList* ast);

// ---------------------------------------------------------------------------
// Global symbol table access (for debugging and post-execution inspection)
// ---------------------------------------------------------------------------
const char* get_global_name(int index);   // Look up a global's name by index
int get_global_count(void);               // Number of globals defined

#endif // COMPILER_H
