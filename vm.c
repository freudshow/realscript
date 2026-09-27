// ============================================================================
// vm.c -- Virtual Machine Implementation
//
// The VM is a bytecode interpreter that reads OpCode instructions from a
// Chunk and manipulates a value stack.  It uses a classic fetch-decode-execute
// loop with a switch on the instruction byte.
//
// Key design:
//   - Stack holds both operand values AND local variables (each call frame
//     gets a contiguous region of the stack)
//   - Call frames are stored in a fixed-size array (FRAMES_MAX = 64)
//   - Globals are stored in a fixed-size array of 512 Values
//   - Database access (OP_GET_DB_*, OP_SET_DB_*) calls the mock DB module
// ============================================================================

#include "vm.h"
#include "ttu_script_runtime.h"
#include "db.h"
#include <stdio.h>
#include <stdarg.h>
#include <string.h>

// ---------------------------------------------------------------------------
// DEBUG_TRACE_EXECUTION
// Set to 1 to print each instruction and the stack state as it executes.
// Useful for debugging the compiler and VM.
// ---------------------------------------------------------------------------
#define DEBUG_TRACE_EXECUTION 0

// ============================================================================
// init_vm — initialise a fresh VM (empty stack, no frames, nil globals)
// ============================================================================
void init_vm(VM* vm) {
    vm->host = NULL;
    vm->host_context = NULL;
    vm->capabilities = 0;
    vm->instruction_budget = 100000;
    vm->executed_instructions = 0;
    vm->script_name = NULL;
    vm->stackTop = vm->stack;
    vm->frameCount = 0;             // No call frames
    for (int i = 0; i < 512; i++) {
        vm->globals[i] = nil_val(); // All globals initially nil
    }
}

void init_vm_with_host(VM* vm, const TTUScriptHostAPI* host, void* host_context,
                       uint32_t capabilities, const char* script_name)
{
    init_vm(vm);
    vm->host = host;
    vm->host_context = host_context;
    vm->capabilities = capabilities;
    vm->script_name = script_name;
}

void vm_set_instruction_budget(VM* vm, int budget) {
    if (vm != NULL && budget > 0) vm->instruction_budget = budget;
}

void free_vm(VM* vm) {
    (void)vm;
    // All heap-allocated resources (functions/chunks) are owned by the
    // ObjFunction returned from compile().  main.c frees them after the
    // VM finishes, so there's nothing to free here.
}

// ============================================================================
// Stack operations
// ============================================================================

// push: place a value on top of the stack
static void push(VM* vm, Value value) {
    *vm->stackTop = value;
    vm->stackTop++;
}

// pop: remove and return the top value from the stack
static Value pop(VM* vm) {
    vm->stackTop--;
    return *vm->stackTop;
}

// peek: look at a value down the stack without popping
//   distance 0 = top, 1 = one below top, etc.
static Value peek(VM* vm, int distance) {
    return vm->stackTop[-1 - distance];
}

// ============================================================================
// runtime_error — print an error with call-stack traceback, then reset VM
// ============================================================================
static void runtime_error(VM* vm, const char* format, ...) {
    va_list args;
    va_start(args, format);
    vfprintf(stderr, format, args);
    va_end(args);
    fputs("\n", stderr);

    // Print call-stack traceback (innermost frame first)
    for (int i = vm->frameCount - 1; i >= 0; i--) {
        CallFrame* frame = &vm->frames[i];
        ObjFunction* function = frame->function;
        size_t instruction = frame->ip - function->chunk.code - 1;
        fprintf(stderr, "[line %d] in %s()\n",
                function->chunk.lines[instruction],
                function->name ? function->name : "script");
    }

    // Reset VM state
    vm->stackTop = vm->stack;
    vm->frameCount = 0;
}

// ============================================================================
// DEBUG_TRACE_EXECUTION block — instruction disassembler for debugging
// ============================================================================
#if DEBUG_TRACE_EXECUTION
static void disassemble_instruction(Chunk* chunk, int offset) {
    printf("%04d ", offset);
    if (offset > 0 && chunk->lines[offset] == chunk->lines[offset - 1]) {
        printf("   | ");
    } else {
        printf("%4d ", chunk->lines[offset]);
    }

    uint8_t instruction = chunk->code[offset];
    switch (instruction) {
        case OP_CONSTANT: {
            uint8_t constant = chunk->code[offset + 1];
            printf("%-16s %4d '", "OP_CONSTANT", constant);
            print_value(chunk->values[constant]);
            printf("'\n");
            break;
        }
        case OP_NIL:      printf("OP_NIL\n"); break;
        case OP_TRUE:     printf("OP_TRUE\n"); break;
        case OP_FALSE:    printf("OP_FALSE\n"); break;
        case OP_POP:      printf("OP_POP\n"); break;
        case OP_GET_LOCAL: {
            uint8_t slot = chunk->code[offset + 1];
            printf("%-16s %4d\n", "OP_GET_LOCAL", slot);
            break;
        }
        case OP_SET_LOCAL: {
            uint8_t slot = chunk->code[offset + 1];
            printf("%-16s %4d\n", "OP_SET_LOCAL", slot);
            break;
        }
        case OP_GET_GLOBAL: {
            uint8_t slot = chunk->code[offset + 1];
            printf("%-16s %4d\n", "OP_GET_GLOBAL", slot);
            break;
        }
        case OP_SET_GLOBAL: {
            uint8_t slot = chunk->code[offset + 1];
            printf("%-16s %4d\n", "OP_SET_GLOBAL", slot);
            break;
        }
        case OP_DEFINE_GLOBAL: {
            uint8_t slot = chunk->code[offset + 1];
            printf("%-16s %4d\n", "OP_DEFINE_GLOBAL", slot);
            break;
        }
        case OP_GET_DB_ID: {
            uint16_t id = (chunk->code[offset + 1] << 8) | chunk->code[offset + 2];
            printf("%-16s #%d\n", "OP_GET_DB_ID", id);
            break;
        }
        case OP_SET_DB_ID: {
            uint16_t id = (chunk->code[offset + 1] << 8) | chunk->code[offset + 2];
            printf("%-16s #%d\n", "OP_SET_DB_ID", id);
            break;
        }
        case OP_GET_DB_LINK: {
            uint16_t l = (chunk->code[offset + 1] << 8) | chunk->code[offset + 2];
            uint16_t d = (chunk->code[offset + 3] << 8) | chunk->code[offset + 4];
            uint16_t r = (chunk->code[offset + 5] << 8) | chunk->code[offset + 6];
            printf("%-16s #(%d, %d, %d)\n", "OP_GET_DB_LINK", l, d, r);
            break;
        }
        case OP_SET_DB_LINK: {
            uint16_t l = (chunk->code[offset + 1] << 8) | chunk->code[offset + 2];
            uint16_t d = (chunk->code[offset + 3] << 8) | chunk->code[offset + 4];
            uint16_t r = (chunk->code[offset + 5] << 8) | chunk->code[offset + 6];
            printf("%-16s #(%d, %d, %d)\n", "OP_SET_DB_LINK", l, d, r);
            break;
        }
        case OP_ADD:      printf("OP_ADD\n"); break;
        case OP_SUB:      printf("OP_SUB\n"); break;
        case OP_MUL:      printf("OP_MUL\n"); break;
        case OP_DIV:      printf("OP_DIV\n"); break;
        case OP_MOD:      printf("OP_MOD\n"); break;
        case OP_BIT_AND:  printf("OP_BIT_AND\n"); break;
        case OP_BIT_OR:   printf("OP_BIT_OR\n"); break;
        case OP_BIT_XOR:  printf("OP_BIT_XOR\n"); break;
        case OP_BIT_NOT:  printf("OP_BIT_NOT\n"); break;
        case OP_SHL:      printf("OP_SHL\n"); break;
        case OP_SHR:      printf("OP_SHR\n"); break;
        case OP_EQUAL:    printf("OP_EQUAL\n"); break;
        case OP_LESS:     printf("OP_LESS\n"); break;
        case OP_GREATER:  printf("OP_GREATER\n"); break;
        case OP_NOT:      printf("OP_NOT\n"); break;
        case OP_JUMP: {
            uint16_t jump = (chunk->code[offset + 1] << 8) | chunk->code[offset + 2];
            printf("%-16s -> %d\n", "OP_JUMP", offset + 3 + jump);
            break;
        }
        case OP_JUMP_IF_FALSE: {
            uint16_t jump = (chunk->code[offset + 1] << 8) | chunk->code[offset + 2];
            printf("%-16s -> %d\n", "OP_JUMP_IF_FALSE", offset + 3 + jump);
            break;
        }
        case OP_LOOP: {
            uint16_t jump = (chunk->code[offset + 1] << 8) | chunk->code[offset + 2];
            printf("%-16s -> %d\n", "OP_LOOP", offset + 3 - jump);
            break;
        }
        case OP_CALL_NATIVE: printf("OP_CALL_NATIVE\n"); break;
        case OP_GET_FIELD: printf("OP_GET_FIELD\n"); break;
        case OP_INDEX: printf("OP_INDEX\n"); break;
        case OP_RETURN:   printf("OP_RETURN\n"); break;
        default:          printf("Unknown opcode %d\n", instruction); break;
    }
}
#endif // DEBUG_TRACE_EXECUTION

// ============================================================================
// run — the main execution loop
//
// Reads instructions from the current frame's Chunk, decodes operands, and
// performs the corresponding operation.
// ============================================================================
static InterpretResult run(VM* vm) {
    // Set the current frame to the most recently pushed one
    CallFrame* frame = &vm->frames[vm->frameCount - 1];

    // ---- Instruction read macros ----
#define READ_BYTE()       (*frame->ip++)
#define READ_SHORT()      (frame->ip += 2, (uint16_t)((frame->ip[-2] << 8) | frame->ip[-1]))
#define READ_CONSTANT()   (frame->function->chunk.values[READ_BYTE()])

    // ---- Binary operation macro: pop b, pop a, push a op b ----
#define BINARY_OP(op_func) \
    do { \
        Value b = pop(vm); \
        Value a = pop(vm); \
        push(vm, op_func(a, b)); \
    } while (false)

    // ====================================================================
    // Main fetch-decode-execute loop
    // ====================================================================
    for (;;) {
        if (vm->instruction_budget <= 0) {
            runtime_error(vm, "Instruction budget exceeded.");
            return INTERPRET_RUNTIME_ERROR;
        }
        vm->instruction_budget--;
#if DEBUG_TRACE_EXECUTION
        // Print stack contents
        printf("          ");
        for (Value* slot = vm->stack; slot < vm->stackTop; slot++) {
            printf("[ ");
            print_value(*slot);
            printf(" ]");
        }
        printf("\n");
        // Print current instruction
        disassemble_instruction(&frame->function->chunk,
            (int)(frame->ip - frame->function->chunk.code));
#endif

        uint8_t instruction = READ_BYTE();
        switch (instruction) {

            // ------------------------------------------------------------
            // Literals / Constants
            // ------------------------------------------------------------
            case OP_CONSTANT: {
                Value constant = READ_CONSTANT();
                push(vm, constant);
                break;
            }
            case OP_NIL:    push(vm, nil_val()); break;
            case OP_TRUE:   push(vm, bool_val(true)); break;
            case OP_FALSE:  push(vm, bool_val(false)); break;
            case OP_POP:    pop(vm); break;

            // ------------------------------------------------------------
            // Local variable access
            // ------------------------------------------------------------
            case OP_GET_LOCAL: {
                uint8_t slot = READ_BYTE();
                push(vm, frame->slots[slot]);
                break;
            }
            case OP_SET_LOCAL: {
                uint8_t slot = READ_BYTE();
                frame->slots[slot] = peek(vm, 0);  // Copy top of stack to local
                break;
            }

            // ------------------------------------------------------------
            // Global variable access
            // ------------------------------------------------------------
            case OP_GET_GLOBAL: {
                uint8_t index = READ_BYTE();
                push(vm, vm->globals[index]);
                break;
            }
            case OP_SET_GLOBAL: {
                uint8_t index = READ_BYTE();
                vm->globals[index] = peek(vm, 0);
                break;
            }
            case OP_DEFINE_GLOBAL: {
                uint8_t index = READ_BYTE();
                vm->globals[index] = pop(vm);  // Pop value and store
                break;
            }

            // ------------------------------------------------------------
            // Database access — flat index  (#N)
            // ------------------------------------------------------------
            case OP_GET_DB_ID: {
                uint16_t id = READ_SHORT();
                double val = GetValueByRealNo((int)id);
                push(vm, double_val(val));
                break;
            }
            case OP_SET_DB_ID: {
                uint16_t id = READ_SHORT();
                Value val = peek(vm, 0);
                SetValueByRealNo((int)id, as_double(val));
                break;
            }

            // ------------------------------------------------------------
            // Database access — link-dev-reg  (#(L, D, R))
            // ------------------------------------------------------------
            case OP_GET_DB_LINK: {
                uint16_t l = READ_SHORT();
                uint16_t d = READ_SHORT();
                uint16_t r = READ_SHORT();
                double val = GetValueByLinkDevReg((int)l, (int)d, (int)r);
                push(vm, double_val(val));
                break;
            }
            case OP_SET_DB_LINK: {
                uint16_t l = READ_SHORT();
                uint16_t d = READ_SHORT();
                uint16_t r = READ_SHORT();
                Value val = peek(vm, 0);
                SetValueByLinkDevReg((int)l, (int)d, (int)r, as_double(val));
                break;
            }

            // ------------------------------------------------------------
            // Arithmetic
            // ------------------------------------------------------------
            case OP_ADD:      BINARY_OP(add_values); break;
            case OP_SUB:      BINARY_OP(sub_values); break;
            case OP_MUL:      BINARY_OP(mul_values); break;
            case OP_DIV:      BINARY_OP(div_values); break;
            case OP_MOD:      BINARY_OP(mod_values); break;

            // ------------------------------------------------------------
            // Bitwise
            // ------------------------------------------------------------
            case OP_BIT_AND:  BINARY_OP(bitwise_and_values); break;
            case OP_BIT_OR:   BINARY_OP(bitwise_or_values); break;
            case OP_BIT_XOR:  BINARY_OP(bitwise_xor_values); break;
            case OP_SHL:      BINARY_OP(bitwise_shl_values); break;
            case OP_SHR:      BINARY_OP(bitwise_shr_values); break;

            // ------------------------------------------------------------
            // Comparison
            // ------------------------------------------------------------
            case OP_EQUAL:    BINARY_OP(eq_values); break;
            case OP_LESS:     BINARY_OP(lt_values); break;
            case OP_GREATER:  BINARY_OP(gt_values); break;

            // ------------------------------------------------------------
            // Logical / Bitwise NOT
            // ------------------------------------------------------------
            case OP_NOT: {
                Value val = pop(vm);
                push(vm, logical_not_value(val));
                break;
            }
            case OP_BIT_NOT: {
                Value val = pop(vm);
                push(vm, bitwise_not_value(val));
                break;
            }

            // ------------------------------------------------------------
            // Jump instructions
            // ------------------------------------------------------------
            case OP_JUMP: {
                uint16_t offset = READ_SHORT();
                frame->ip += offset;               // Forward jump
                break;
            }
            case OP_JUMP_IF_FALSE: {
                uint16_t offset = READ_SHORT();
                if (!is_truthy(peek(vm, 0))) {
                    frame->ip += offset;            // Jump if falsy
                }
                break;
            }
            case OP_LOOP: {
                uint16_t offset = READ_SHORT();
                frame->ip -= offset;                // Backward jump (loop)
                break;
            }

            // ------------------------------------------------------------
            // Function call
            // ------------------------------------------------------------
        case OP_CALL_NATIVE: {
            uint16_t id = READ_SHORT();
            uint8_t argc = READ_BYTE();
            const TTUNativeFunction* native = ttu_script_get_native(id);
            Value args[256];
            Value result;
            int i;
            if (native == NULL || argc < native->min_arity || argc > native->max_arity) {
                runtime_error(vm, "Invalid native call id or argument count.");
                return INTERPRET_RUNTIME_ERROR;
            }
            if ((vm->capabilities & native->capability) != native->capability) {
                runtime_error(vm, "Native capability denied: %s.", native->name);
                return INTERPRET_RUNTIME_ERROR;
            }
            for (i = 0; i < argc; ++i) args[i] = vm->stackTop[-argc + i];
            result = native->function(vm, argc, args);
            vm->stackTop -= argc;
            push(vm, result);
            break;
        }
        case OP_GET_FIELD: {
            uint8_t field_index = READ_BYTE();
            if (field_index >= frame->function->chunk.valueCount) {
                runtime_error(vm, "Invalid field constant index.");
                return INTERPRET_RUNTIME_ERROR;
            }
            Value result;
            Value object = pop(vm);
            Value field_name = frame->function->chunk.values[field_index];
            if (field_name.type != VAL_STRING || !value_get_field(object, field_name.as.obj, &result)) {
                runtime_error(vm, "Unknown field access.");
                return INTERPRET_RUNTIME_ERROR;
            }
            free_value(object);
            push(vm, result);
            break;
        }
        case OP_INDEX: {
            Value index = pop(vm);
            Value object = pop(vm);
            int64_t i = as_int(index);
            Value result = nil_val();
            if (object.type == VAL_ARRAY) {
                ObjArray* array = object.as.obj;
                if (i < 0 || (uint64_t)i >= array->count) { free_value(object); runtime_error(vm, "Array index out of bounds."); return INTERPRET_RUNTIME_ERROR; }
                result = array->items[i];
            } else if (object.type == VAL_BYTES) {
                ObjBytes* bytes = object.as.obj;
                if (i < 0 || (uint64_t)i >= bytes->length) { free_value(object); runtime_error(vm, "Bytes index out of bounds."); return INTERPRET_RUNTIME_ERROR; }
                result = int_val(bytes->data[i]);
            } else { free_value(object); runtime_error(vm, "Value is not indexable."); return INTERPRET_RUNTIME_ERROR; }
            push(vm, result);
            break;
        }

        case OP_CALL: {
            uint8_t argCount = READ_BYTE();

                // The callee function object is argCount positions below top
                Value callee = peek(vm, argCount);

                if (callee.type != VAL_FUNC) {
                    runtime_error(vm, "Can only call functions (got type %d).", callee.type);
                    return INTERPRET_RUNTIME_ERROR;
                }

                ObjFunction* function = (ObjFunction*)callee.as.obj;
                if (argCount != function->arity) {
                    runtime_error(vm, "Expected %d arguments but got %d.",
                                  function->arity, argCount);
                    return INTERPRET_RUNTIME_ERROR;
                }

                if (vm->frameCount >= FRAMES_MAX) {
                    runtime_error(vm, "Stack overflow (max call frames reached).");
                    return INTERPRET_RUNTIME_ERROR;
                }

                // Set up new call frame
                CallFrame* newFrame = &vm->frames[vm->frameCount++];
                newFrame->function = function;
                newFrame->ip = function->chunk.code;        // Start at beginning
                newFrame->slots = vm->stackTop - argCount - 1;  // Base of frame

                frame = &vm->frames[vm->frameCount - 1];
                break;
            }

            // ------------------------------------------------------------
            // Return from function
            // ------------------------------------------------------------
            case OP_RETURN: {
                Value result = pop(vm);    // Pop the return value
                vm->frameCount--;          // Pop this call frame

                if (vm->frameCount == 0) {
                    // Returned from the main script — we're done
                    pop(vm);               // Pop the main function object
                    return INTERPRET_OK;
                }

                // Restore the caller's stack and push the return value
                vm->stackTop = frame->slots;
                push(vm, result);

                // Update current frame pointer to the caller
                frame = &vm->frames[vm->frameCount - 1];
                break;
            }
        }
    }

    // Cleanup macros
#undef READ_BYTE
#undef READ_SHORT
#undef READ_CONSTANT
#undef BINARY_OP
}

// ============================================================================
// interpret — entry point: push the main function and start execution
//
// Sets up the initial call frame for the compiled script and calls run().
// ============================================================================
InterpretResult interpret(VM* vm, ObjFunction* function) {
    vm->instruction_budget = vm->instruction_budget > 0 ? vm->instruction_budget : 100000;
    vm->executed_instructions = 0;
    vm->instruction_budget = vm->instruction_budget > 0 ? vm->instruction_budget : 100000;
    // Push the function object as the stack bottom (frame slot 0)
    push(vm, (Value){VAL_FUNC, {.obj = function}});
    CallFrame* frame = &vm->frames[vm->frameCount++];
    frame->function = function;
    frame->ip = function->chunk.code;
    frame->slots = vm->stack;   // Slots start at bottom of stack

    return run(vm);
}
