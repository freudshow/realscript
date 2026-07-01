// ============================================================================
// main.c -- Entry Point
//
// Three modes:
//   1. No arguments — run the built-in verification test suite
//   2. One argument  — run a .real source file
//   3. Wrong args   — print usage
//
// The test suite exercises all major language features: arithmetic, bitwise,
// logical operators with short-circuiting, control flow (if/while/for),
// functions (including recursion), and database read/write integration.
//
// After each successful run, the final values of all global variables are
// printed to verify correctness.
// ============================================================================

#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include "parser.h"
#include "compiler.h"
#include "vm.h"
#include "db.h"

// ---------------------------------------------------------------------------
// run_source — full pipeline: parse → compile → execute → print globals
// ---------------------------------------------------------------------------
static void run_source(const char* source) {
    // Step 1: Parse source into AST
    AstNodeList* ast = parse(source);
    if (ast == NULL) {
        printf("Parsing failed!\n");
        return;
    }

    // Step 2: Compile AST into bytecode (ObjFunction)
    ObjFunction* function = compile(ast);
    if (function == NULL) {
        printf("Compilation failed!\n");
        free_ast_list(ast);
        return;
    }

    // Step 3: Create VM and execute the compiled function
    VM vm;
    init_vm(&vm);

    InterpretResult result = interpret(&vm, function);

    // Step 4: Report results
    if (result == INTERPRET_OK) {
        printf("\n--- Global Variables State ---\n");
        int count = get_global_count();
        if (count == 0) {
            printf("(None defined)\n");
        } else {
            for (int i = 0; i < count; i++) {
                const char* name = get_global_name(i);
                Value val = vm.globals[i];
                printf("  %s = ", name);
                print_value(val);
                printf("\n");
            }
        }
    } else {
        printf("Execution encountered a runtime error!\n");
    }

    // Step 5: Cleanup
    free_vm(&vm);
    free_function(function);
    free_ast_list(ast);
}

// ---------------------------------------------------------------------------
// readFile — read an entire file into a heap-allocated string
// ---------------------------------------------------------------------------
static char* readFile(const char* path) {
    FILE* file = fopen(path, "rb");
    if (file == NULL) {
        fprintf(stderr, "Could not open file \"%s\".\n", path);
        exit(74);
    }

    fseek(file, 0L, SEEK_END);
    size_t fileSize = ftell(file);
    rewind(file);

    char* buffer = (char*)malloc(fileSize + 1);
    if (buffer == NULL) {
        fprintf(stderr, "Not enough memory to read \"%s\".\n", path);
        exit(74);
    }
    size_t bytesRead = fread(buffer, sizeof(char), fileSize, file);
    buffer[bytesRead] = '\0';

    fclose(file);
    return buffer;
}

// ---------------------------------------------------------------------------
// run_test_case — wrapper that prints header/source before running
// ---------------------------------------------------------------------------
static void run_test_case(const char* testName, const char* source) {
    printf("========================================\n");
    printf("TEST CASE: %s\n", testName);
    printf("========================================\n");
    printf("Source Code:\n%s\n", source);
    printf("----------------------------------------\n");
    printf("Execution logs:\n");
    run_source(source);
    printf("========================================\n\n");
}

// ============================================================================
// main — program entry point
// ============================================================================
int main(int argc, char* argv[]) {
    if (argc == 2) {
        // Run a .real source file
        char* source = readFile(argv[1]);
        run_source(source);
        free(source);

    } else if (argc == 1) {
        // Run the built-in verification test suite
        printf("RealScript Engine - Verification Test Suite\n");
        printf("========================================\n\n");

        // Test 1: arithmetic with parentheses, modulo, unary minus
        run_test_case("1. Arithmetic and Modulo Calculations",
            "var a = (10 + 5) * 3 - (12 / 4);\n"
            "var b = 25 % 7;\n"
            "var c = -10 + 15;\n"
        );

        // Test 2: C-like bitwise operations
        run_test_case("2. Bit-wise Operations (C-like)",
            "var andVal = 12 & 25;\n"
            "var orVal = 12 | 25;\n"
            "var xorVal = 12 ^ 25;\n"
            "var notVal = ~12;\n"
            "var shlVal = 3 << 2;\n"
            "var shrVal = 16 >> 2;\n"
        );

        // Test 3: logical &&, ||, ! with short-circuit guarantees
        run_test_case("3. Logical Operations with Short-Circuiting",
            "var flag1 = true && false;\n"
            "var flag2 = false || true;\n"
            "var flag3 = !true;\n"
            "// Verifying short circuit: if the LHS of && is false, RHS is not evaluated\n"
            "var x = 0;\n"
            "var testShortCircuit = false && (x = 999);\n"
        );

        // Test 4: if-else-if chains with block scopes
        run_test_case("4. Control Flow (If-Else & Block Scopes)",
            "var num = 45;\n"
            "var result = 0;\n"
            "if (num < 20) {\n"
            "    result = 1;\n"
            "} else if (num < 50) {\n"
            "    result = 2;\n"
            "} else {\n"
            "    result = 3;\n"
            "}\n"
        );

        // Test 5: while and for loops
        run_test_case("5. Loops (While & For Loops)",
            "// Calculating 1 + 2 + 3 + 4 + 5 using while loop\n"
            "var sum = 0;\n"
            "var i = 1;\n"
            "while (i <= 5) {\n"
            "    sum = sum + i;\n"
            "    i = i + 1;\n"
            "}\n"
            "\n"
            "// Calculating 5! (factorial) using for loop\n"
            "var factorial = 1;\n"
            "for (var j = 1; j <= 5; j = j + 1) {\n"
            "    factorial = factorial * j;\n"
            "}\n"
        );

        // Test 6: function calls (including recursive factorial)
        run_test_case("6. Function Calls and Recursion",
            "fn doubleNum(val) {\n"
            "    return val * 2;\n"
            "}\n"
            "\n"
            "fn recursiveFactorial(n) {\n"
            "    if (n <= 1) {\n"
            "        return 1;\n"
            "    }\n"
            "    return n * recursiveFactorial(n - 1);\n"
            "}\n"
            "\n"
            "var res1 = doubleNum(21);\n"
            "var res2 = recursiveFactorial(5);\n"
        );

        // Test 7: database read/write via both addressing schemes
        run_test_case("7. RealDataBase Get/Set Integration",
            "// 1. Direct index DB reference (#integer)\n"
            "#32 = 45.67;\n"
            "var getByReal = #32;\n"
            "var calcByReal = #32 * 2;\n"
            "\n"
            "// 2. Link, Device, Register DB reference (#(l, d, r))\n"
            "#(3, 45, 12) = 88.99;\n"
            "var getByLink = #(3, 45, 12);\n"
            "var calcByLink = #(3, 45, 12) + 11.01;\n"
        );

    } else {
        printf("Usage: ./realscript [filename]\n");
        return 1;
    }

    return 0;
}
