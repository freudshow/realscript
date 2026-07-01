// ============================================================================
// value.c -- Value Type System Implementation
//
// Implements the tagged-union Value type and all operators that the VM
// delegates to function calls (via the BINARY_OP macro in vm.c).
//
// Type-promotion rule: if both operands are VAL_INT, the result stays VAL_INT
// (except for division which truncates like C integer division).  If either
// operand is VAL_DOUBLE (or mixed), the result promotes to VAL_DOUBLE.
// Bitwise operators always coerce to int64 via as_int().
// ============================================================================

#include "value.h"
#include <stdio.h>

// ============================================================================
// Value constructors
// ============================================================================

Value int_val(int64_t val) {
    Value v;
    v.type = VAL_INT;
    v.as.integer = val;
    return v;
}

Value double_val(double val) {
    Value v;
    v.type = VAL_DOUBLE;
    v.as.real = val;
    return v;
}

Value bool_val(bool val) {
    Value v;
    v.type = VAL_BOOL;
    v.as.boolean = val;
    return v;
}

Value nil_val(void) {
    Value v;
    v.type = VAL_NIL;
    v.as.integer = 0;  // Zero-initialize the union for safety
    return v;
}

// ============================================================================
// print_value -- output a Value's human-readable form to stdout
// ============================================================================
void print_value(Value val) {
    switch (val.type) {
        case VAL_INT:
            printf("%ld", val.as.integer);
            break;
        case VAL_DOUBLE:
            printf("%g", val.as.real);
            break;
        case VAL_BOOL:
            printf(val.as.boolean ? "true" : "false");
            break;
        case VAL_NIL:
            printf("nil");
            break;
        case VAL_FUNC:
            printf("<fn>");   // Functions are opaque; just print a placeholder
            break;
    }
}

// ============================================================================
// is_truthy -- used by ! (logical not) and if/while condition checks
//
// Truthiness rules:
//   VAL_INT    → integer != 0
//   VAL_DOUBLE → double != 0.0
//   VAL_BOOL   → the boolean value itself
//   VAL_NIL    → always false
// ============================================================================
bool is_truthy(Value val) {
    switch (val.type) {
        case VAL_INT:
            return val.as.integer != 0;
        case VAL_DOUBLE:
            return val.as.real != 0.0;
        case VAL_BOOL:
            return val.as.boolean;
        case VAL_NIL:
            return false;
        default:
            return false;
    }
}

// ============================================================================
// Type-coercion helpers
// ============================================================================

// as_int: convert any Value to int64
int64_t as_int(Value val) {
    switch (val.type) {
        case VAL_INT:    return val.as.integer;
        case VAL_DOUBLE: return (int64_t)val.as.real;          // Truncates toward zero
        case VAL_BOOL:   return val.as.boolean ? 1 : 0;
        case VAL_NIL:
        default:         return 0;
    }
}

// as_double: convert any Value to double
double as_double(Value val) {
    switch (val.type) {
        case VAL_INT:    return (double)val.as.integer;
        case VAL_DOUBLE: return val.as.real;
        case VAL_BOOL:   return val.as.boolean ? 1.0 : 0.0;
        case VAL_NIL:
        default:         return 0.0;
    }
}

// ============================================================================
// Arithmetic operators
// ============================================================================

Value add_values(Value a, Value b) {
    // Integer-only path preserves integer result
    if (a.type == VAL_INT && b.type == VAL_INT) {
        return int_val(a.as.integer + b.as.integer);
    }
    // Mixed or double path → promote to double
    return double_val(as_double(a) + as_double(b));
}

Value sub_values(Value a, Value b) {
    if (a.type == VAL_INT && b.type == VAL_INT) {
        return int_val(a.as.integer - b.as.integer);
    }
    return double_val(as_double(a) - as_double(b));
}

Value mul_values(Value a, Value b) {
    if (a.type == VAL_INT && b.type == VAL_INT) {
        return int_val(a.as.integer * b.as.integer);
    }
    return double_val(as_double(a) * as_double(b));
}

Value div_values(Value a, Value b) {
    // Integer ÷ integer → C-style integer division (truncates)
    if (a.type == VAL_INT && b.type == VAL_INT) {
        if (b.as.integer == 0) {
            printf("[Runtime Warning] Division by zero!\n");
            return nil_val();           // Error sentinel instead of crash
        }
        return int_val(a.as.integer / b.as.integer);
    }
    // At least one operand is double → floating-point division
    double denom = as_double(b);
    if (denom == 0.0) {
        printf("[Runtime Warning] Division by zero!\n");
        return nil_val();
    }
    return double_val(as_double(a) / denom);
}

// mod_values: always coerces to int; returns int result
Value mod_values(Value a, Value b) {
    int64_t denom = as_int(b);
    if (denom == 0) {
        printf("[Runtime Warning] Modulo by zero!\n");
        return nil_val();
    }
    return int_val(as_int(a) % denom);
}

// ============================================================================
// Bitwise operators  (all coerce to int64 via as_int, return int)
// ============================================================================

Value bitwise_and_values(Value a, Value b) { return int_val(as_int(a) & as_int(b)); }
Value bitwise_or_values(Value a, Value b)  { return int_val(as_int(a) | as_int(b)); }
Value bitwise_xor_values(Value a, Value b) { return int_val(as_int(a) ^ as_int(b)); }
Value bitwise_not_value(Value a)            { return int_val(~as_int(a)); }
Value bitwise_shl_values(Value a, Value b) { return int_val(as_int(a) << as_int(b)); }
Value bitwise_shr_values(Value a, Value b) { return int_val(as_int(a) >> as_int(b)); }

// ============================================================================
// Logical operator
// ============================================================================

Value logical_not_value(Value a) {
    return bool_val(!is_truthy(a));
}

// ============================================================================
// Comparison operators  (all return VAL_BOOL)
// ============================================================================

// Equality: handles nil/nil → true; nil/anything → false;
// pure bool vs bool; pure int vs int; everything else promotes to double.
Value eq_values(Value a, Value b) {
    if (a.type == VAL_NIL && b.type == VAL_NIL) return bool_val(true);
    if (a.type == VAL_NIL || b.type == VAL_NIL) return bool_val(false);
    if (a.type == VAL_BOOL && b.type == VAL_BOOL) {
        return bool_val(a.as.boolean == b.as.boolean);
    }
    if (a.type == VAL_INT && b.type == VAL_INT) {
        return bool_val(a.as.integer == b.as.integer);
    }
    // Mixed types: promote both to double and compare
    return bool_val(as_double(a) == as_double(b));
}

Value neq_values(Value a, Value b) {
    Value eq = eq_values(a, b);
    return bool_val(!eq.as.boolean);
}

// Less-than: pure int path avoids floating-point rounding
Value lt_values(Value a, Value b) {
    if (a.type == VAL_INT && b.type == VAL_INT) {
        return bool_val(a.as.integer < b.as.integer);
    }
    return bool_val(as_double(a) < as_double(b));
}

Value lte_values(Value a, Value b) {
    if (a.type == VAL_INT && b.type == VAL_INT) {
        return bool_val(a.as.integer <= b.as.integer);
    }
    return bool_val(as_double(a) <= as_double(b));
}

Value gt_values(Value a, Value b) {
    if (a.type == VAL_INT && b.type == VAL_INT) {
        return bool_val(a.as.integer > b.as.integer);
    }
    return bool_val(as_double(a) > as_double(b));
}

Value gte_values(Value a, Value b) {
    if (a.type == VAL_INT && b.type == VAL_INT) {
        return bool_val(a.as.integer >= b.as.integer);
    }
    return bool_val(as_double(a) >= as_double(b));
}
