#ifndef VALUE_H
#define VALUE_H

#include <stdbool.h>
#include <stdint.h>

typedef enum {
    VAL_INT,
    VAL_DOUBLE,
    VAL_BOOL,
    VAL_NIL,
    VAL_FUNC,
} ValueType;

typedef struct {
    ValueType type;
    union {
        int64_t integer;
        double real;
        bool boolean;
        void* obj;
    } as;
} Value;

Value int_val(int64_t val);
Value double_val(double val);
Value bool_val(bool val);
Value nil_val(void);

void print_value(Value val);
bool is_truthy(Value val);

// Type helper conversions
int64_t as_int(Value val);
double as_double(Value val);

// Arithmetic
Value add_values(Value a, Value b);
Value sub_values(Value a, Value b);
Value mul_values(Value a, Value b);
Value div_values(Value a, Value b);
Value mod_values(Value a, Value b);

// Bitwise
Value bitwise_and_values(Value a, Value b);
Value bitwise_or_values(Value a, Value b);
Value bitwise_xor_values(Value a, Value b);
Value bitwise_not_value(Value a);
Value bitwise_shl_values(Value a, Value b);
Value bitwise_shr_values(Value a, Value b);

// Logical (short-circuiting is typically handled by VM jumps, but we define operations too)
Value logical_not_value(Value a);

// Comparison
Value eq_values(Value a, Value b);
Value neq_values(Value a, Value b);
Value lt_values(Value a, Value b);
Value lte_values(Value a, Value b);
Value gt_values(Value a, Value b);
Value gte_values(Value a, Value b);

#endif // VALUE_H
