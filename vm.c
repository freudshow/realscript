#include "vm.h"
#include "db.h"
#include <stdio.h>
#include <stdarg.h>
#include <string.h>

// Debug execution tracing flag
#define DEBUG_TRACE_EXECUTION 0

void init_vm(VM* vm) {
    vm->stackTop = vm->stack;
    vm->frameCount = 0;
    for (int i = 0; i < 512; i++) {
        vm->globals[i] = nil_val();
    }
}

void free_vm(VM* vm) {
    (void)vm;
    // Globals holding functions can be cleaned up if desired,
    // but the main compiled chunk contains all functions as constants,
    // which are automatically freed recursively.
}

static void push(VM* vm, Value value) {
    *vm->stackTop = value;
    vm->stackTop++;
}

static Value pop(VM* vm) {
    vm->stackTop--;
    return *vm->stackTop;
}

static Value peek(VM* vm, int distance) {
    return vm->stackTop[-1 - distance];
}

static void runtime_error(VM* vm, const char* format, ...) {
    va_list args;
    va_start(args, format);
    vfprintf(stderr, format, args);
    va_end(args);
    fputs("\n", stderr);

    for (int i = vm->frameCount - 1; i >= 0; i--) {
        CallFrame* frame = &vm->frames[i];
        ObjFunction* function = frame->function;
        // Find line number
        size_t instruction = frame->ip - function->chunk.code - 1;
        fprintf(stderr, "[line %d] in %s()\n",
                function->chunk.lines[instruction],
                function->name ? function->name : "script");
    }
    
    // Reset stack
    vm->stackTop = vm->stack;
    vm->frameCount = 0;
}

#if DEBUG_TRACE_EXECUTION
// Disassemble instruction for tracing
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
        case OP_CALL: {
            uint8_t args = chunk->code[offset + 1];
            printf("%-16s %4d args\n", "OP_CALL", args);
            break;
        }
        case OP_RETURN:   printf("OP_RETURN\n"); break;
        default:          printf("Unknown opcode %d\n", instruction); break;
    }
}
#endif

static InterpretResult run(VM* vm) {
    CallFrame* frame = &vm->frames[vm->frameCount - 1];

#define READ_BYTE() (*frame->ip++)
#define READ_SHORT() (frame->ip += 2, (uint16_t)((frame->ip[-2] << 8) | frame->ip[-1]))
#define READ_CONSTANT() (frame->function->chunk.values[READ_BYTE()])

#define BINARY_OP(op_func) \
    do { \
        Value b = pop(vm); \
        Value a = pop(vm); \
        push(vm, op_func(a, b)); \
    } while (false)

    for (;;) {
#if DEBUG_TRACE_EXECUTION
        printf("          ");
        for (Value* slot = vm->stack; slot < vm->stackTop; slot++) {
            printf("[ ");
            print_value(*slot);
            printf(" ]");
        }
        printf("\n");
        disassemble_instruction(&frame->function->chunk,
            (int)(frame->ip - frame->function->chunk.code));
#endif

        uint8_t instruction = READ_BYTE();
        switch (instruction) {
            case OP_CONSTANT: {
                Value constant = READ_CONSTANT();
                push(vm, constant);
                break;
            }
            case OP_NIL:      push(vm, nil_val()); break;
            case OP_TRUE:     push(vm, bool_val(true)); break;
            case OP_FALSE:    push(vm, bool_val(false)); break;
            case OP_POP:      pop(vm); break;

            case OP_GET_LOCAL: {
                uint8_t slot = READ_BYTE();
                push(vm, frame->slots[slot]);
                break;
            }
            case OP_SET_LOCAL: {
                uint8_t slot = READ_BYTE();
                frame->slots[slot] = peek(vm, 0);
                break;
            }
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
                vm->globals[index] = pop(vm);
                break;
            }

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

            case OP_ADD:      BINARY_OP(add_values); break;
            case OP_SUB:      BINARY_OP(sub_values); break;
            case OP_MUL:      BINARY_OP(mul_values); break;
            case OP_DIV:      BINARY_OP(div_values); break;
            case OP_MOD:      BINARY_OP(mod_values); break;
            case OP_BIT_AND:  BINARY_OP(bitwise_and_values); break;
            case OP_BIT_OR:   BINARY_OP(bitwise_or_values); break;
            case OP_BIT_XOR:  BINARY_OP(bitwise_xor_values); break;
            case OP_SHL:      BINARY_OP(bitwise_shl_values); break;
            case OP_SHR:      BINARY_OP(bitwise_shr_values); break;
            case OP_EQUAL:    BINARY_OP(eq_values); break;
            case OP_LESS:     BINARY_OP(lt_values); break;
            case OP_GREATER:  BINARY_OP(gt_values); break;

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

            case OP_JUMP: {
                uint16_t offset = READ_SHORT();
                frame->ip += offset;
                break;
            }
            case OP_JUMP_IF_FALSE: {
                uint16_t offset = READ_SHORT();
                if (!is_truthy(peek(vm, 0))) {
                    frame->ip += offset;
                }
                break;
            }
            case OP_LOOP: {
                uint16_t offset = READ_SHORT();
                frame->ip -= offset;
                break;
            }

            case OP_CALL: {
                uint8_t argCount = READ_BYTE();
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

                CallFrame* newFrame = &vm->frames[vm->frameCount++];
                newFrame->function = function;
                newFrame->ip = function->chunk.code;
                // slots points to the function on the stack; locals start after that
                newFrame->slots = vm->stackTop - argCount - 1;

                frame = &vm->frames[vm->frameCount - 1];
                break;
            }

            case OP_RETURN: {
                Value result = pop(vm);
                vm->frameCount--;

                if (vm->frameCount == 0) {
                    pop(vm); // Pop main function
                    return INTERPRET_OK;
                }

                vm->stackTop = frame->slots;
                push(vm, result);

                frame = &vm->frames[vm->frameCount - 1];
                break;
            }
        }
    }

#undef READ_BYTE
#undef READ_SHORT
#undef READ_CONSTANT
#undef BINARY_OP
}

InterpretResult interpret(VM* vm, ObjFunction* function) {
    // Put main function at frame slot 0
    push(vm, (Value){VAL_FUNC, {.obj = function}});
    CallFrame* frame = &vm->frames[vm->frameCount++];
    frame->function = function;
    frame->ip = function->chunk.code;
    frame->slots = vm->stack;

    return run(vm);
}
