// ============================================================================
// value.h -- Value Type System Header
//
// RealScript uses a tagged-union Value type that can hold one of five kinds:
//   VAL_INT, VAL_DOUBLE, VAL_BOOL, VAL_NIL, VAL_FUNC
//
// This header declares:
//   - Constructor functions  (int_val, double_val, bool_val, nil_val)
//   - Arithmetic / bitwise / comparison / logical operators on Values
//   - Conversion helpers    (as_int, as_double)
//   - Runtime helpers        (print_value, is_truthy)
//
// Type promotion: when an operation mixes VAL_INT and VAL_DOUBLE, the int is
// promoted to double and the result is VAL_DOUBLE.  Pure int-in int ops stay int.
// ============================================================================

#ifndef VALUE_H
#define VALUE_H

#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>

// ---------------------------------------------------------------------------
// ValueType enum -- every Value carries one of these
// ---------------------------------------------------------------------------
typedef enum {
    VAL_INT,     // 64-bit signed integer
    VAL_DOUBLE,  // 64-bit IEEE 754 double
    VAL_BOOL,    // boolean (true/false)
    VAL_NIL,     // null / undefined sentinel
    VAL_FUNC,
    VAL_NATIVE,
    VAL_STRING,
    VAL_BYTES,
    VAL_ARRAY,
    VAL_RESULT,
} ValueType;

// ---------------------------------------------------------------------------
// Value -- the universal runtime type
//
// Uses a tagged union to save memory: the union is large enough to hold the
// largest member (void* on 64-bit = 8 bytes, matching int64_t and double).
// ---------------------------------------------------------------------------
typedef struct Value Value;
typedef Value (*NativeFn)(void *context, int argc, const Value *argv);
typedef struct { size_t length; uint8_t *data; } ObjBytes;
typedef struct { size_t count; Value *items; } ObjArray;
typedef struct { bool ok; int code; char *message; } ObjResult;

struct Value {
    ValueType type;
    union {
        int64_t integer;   // Used by VAL_INT
        double real;       // Used by VAL_DOUBLE
        bool boolean;      // Used by VAL_BOOL
        void* obj;
        NativeFn native;
    } as;
};

// ---------------------------------------------------------------------------
// Constructors -- create a Value from a native C value
// ---------------------------------------------------------------------------
Value int_val(int64_t val);
Value double_val(double val);
Value bool_val(bool val);
Value nil_val(void);
Value native_val(NativeFn function);
Value string_val(const char *text, size_t length);
Value bytes_val(const uint8_t *data, size_t length);
Value array_val(size_t count, const Value *items);
Value array_literal_val(size_t count, const Value *items);
Value result_val(bool ok, int code, const char *message);
Value free_value(Value value);

// ---------------------------------------------------------------------------
// Runtime helpers
// ---------------------------------------------------------------------------
void print_value(Value val);      // Print a Value's representation to stdout
bool is_truthy(Value val);        // Truthiness: 0/nil/false → false; else true

// ---------------------------------------------------------------------------
// Type-coercion helpers  -- convert any Value to int or double
// ---------------------------------------------------------------------------
int64_t as_int(Value val);
int64_t as_int(Value val);
double as_double(Value val);
bool value_get_field(Value object, const char* name, Value* result);

// ---------------------------------------------------------------------------
// Arithmetic operators
// ---------------------------------------------------------------------------
Value add_values(Value a, Value b);
Value sub_values(Value a, Value b);
Value mul_values(Value a, Value b);
Value div_values(Value a, Value b);
Value mod_values(Value a, Value b);

// ---------------------------------------------------------------------------
// Bitwise operators  (all force both operands to int64 via as_int)
// ---------------------------------------------------------------------------
Value bitwise_and_values(Value a, Value b);
Value bitwise_or_values(Value a, Value b);
Value bitwise_xor_values(Value a, Value b);
Value bitwise_not_value(Value a);
Value bitwise_shl_values(Value a, Value b);
Value bitwise_shr_values(Value a, Value b);

// ---------------------------------------------------------------------------
// Logical operator  (short-circuit && / || are handled by VM jumps, not here)
// ---------------------------------------------------------------------------
Value logical_not_value(Value a);

// ---------------------------------------------------------------------------
// Comparison operators   (all return VAL_BOOL)
// ---------------------------------------------------------------------------
Value eq_values(Value a, Value b);
Value neq_values(Value a, Value b);
Value lt_values(Value a, Value b);
Value lte_values(Value a, Value b);
Value gt_values(Value a, Value b);
Value gte_values(Value a, Value b);

#endif // VALUE_H
