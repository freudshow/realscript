// ============================================================================
// vm.h -- Virtual Machine Header
//
// The VM is a register-based stack machine that interprets bytecode (OpCode
// instructions) produced by the compiler.  Architecture:
//
//   - 64 call frames (FRAMES_MAX) for nested function calls
//   - 16384-value stack (STACK_MAX)
//   - 512 fixed-size global variable slots
//
// Each call frame tracks the executing function, the instruction pointer (ip),
// and the base pointer (slots) into the value stack for local variables.
// ============================================================================

#ifndef VM_H
#define VM_H

#include "compiler.h"
#include "value.h"
#include <stdint.h>

typedef struct TTUScriptHostAPI TTUScriptHostAPI;


#define FRAMES_MAX 64
#define STACK_MAX (FRAMES_MAX * 256)
#define TTU_CAP_RTD_READ (1u << 0)
#define TTU_CAP_RTD_WRITE (1u << 1)
#define TTU_CAP_YK_PREPARE (1u << 4)
#define TTU_CAP_YK_EXECUTE (1u << 5)
#define TTU_CAP_YK_CANCEL (1u << 6)
#define TTU_CAP_LOG (1u << 16)

// ---------------------------------------------------------------------------
// CallFrame — represents one active function invocation
//
//   function : the ObjFunction being executed
//   ip       : instruction pointer (next byte to execute)
//   slots    : pointer into the VM's stack where this frame's locals begin
// ---------------------------------------------------------------------------
typedef struct {
    ObjFunction* function;   // The compiled function being executed
    uint8_t* ip;             // Instruction pointer (points into function->chunk.code)
    Value* slots;            // Base pointer for this frame's local variables
} CallFrame;

// ---------------------------------------------------------------------------
// VM — the virtual machine state
//
//   frames[]   : call-stack (up to FRAMES_MAX deep)
//   frameCount : current number of active frames
//   stack[]    : operand stack (values being computed)
//   stackTop   : pointer to the next free slot on the stack
//   globals[]  : global variable storage (indexed by compiler-allocated index)
// ---------------------------------------------------------------------------
typedef struct {
    CallFrame frames[FRAMES_MAX];   // Call frame stack (fixed-size array)
    int frameCount;                 // Current number of active frames

    Value stack[STACK_MAX];         // Operand / local-variable stack
    Value* stackTop;                // Points to the next free stack slot

    Value globals[512];             // Global variable array (indexed by name)
    const TTUScriptHostAPI* host;
    void* host_context;
    uint32_t capabilities;
    int instruction_budget;
    int executed_instructions;
    const char* script_name;
} VM;

// ---------------------------------------------------------------------------
// InterpretResult — what interpret() returns
// ---------------------------------------------------------------------------
typedef enum {
    INTERPRET_OK,               // Execution completed without error
    INTERPRET_COMPILE_ERROR,    // (not used yet — compile errors caught earlier)
    INTERPRET_RUNTIME_ERROR     // A runtime error occurred (e.g. type error)
} InterpretResult;

// ---------------------------------------------------------------------------
// Public API
// ---------------------------------------------------------------------------
void init_vm(VM* vm);
void init_vm_with_host(VM* vm, const TTUScriptHostAPI* host, void* host_context,
                       uint32_t capabilities, const char* script_name);
void vm_set_instruction_budget(VM* vm, int budget);
void free_vm(VM* vm);
InterpretResult interpret(VM* vm, ObjFunction* function);  // Execute a function

#endif // VM_H
