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
#include <stdlib.h>
#include <string.h>

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

Value native_val(NativeFn function) {
    Value value;
    value.type = VAL_NATIVE;
    value.as.native = function;
    return value;
}

Value string_val(const char *text, size_t length) {
    Value value;
    char *copy;
    if (text == NULL || length > 65536) return nil_val();
    copy = malloc(length + 1);
    if (copy == NULL) return nil_val();
    memcpy(copy, text, length);
    copy[length] = '\0';
    value.type = VAL_STRING;
    value.as.obj = copy;
    return value;
}

Value bytes_val(const uint8_t *data, size_t length) {
    Value value;
    ObjBytes *bytes;
    if ((data == NULL && length != 0) || length > 65536) return nil_val();
    bytes = malloc(sizeof(*bytes));
    if (bytes == NULL) return nil_val();
    bytes->data = malloc(length == 0 ? 1 : length);
    if (bytes->data == NULL) { free(bytes); return nil_val(); }
    if (length != 0) memcpy(bytes->data, data, length);
    bytes->length = length;
    value.type = VAL_BYTES;
    value.as.obj = bytes;
    return value;
}

Value array_val(size_t count, const Value *items) {
    Value value;
    ObjArray *array;
    if ((items == NULL && count != 0) || count > 4096) return nil_val();
    array = malloc(sizeof(*array));
    if (array == NULL) return nil_val();
    array->items = malloc((count == 0 ? 1 : count) * sizeof(Value));
    if (array->items == NULL) { free(array); return nil_val(); }
    if (count != 0) memcpy(array->items, items, count * sizeof(Value));
    array->count = count;
    value.type = VAL_ARRAY;
    value.as.obj = array;
    return value;
}

Value array_literal_val(size_t count, const Value *items) {
    Value value;
    ObjArray *array = malloc(sizeof(*array));
    if (array == NULL) return nil_val();
    array->items = malloc((count == 0 ? 1 : count) * sizeof(Value));
    if (array->items == NULL) { free(array); return nil_val(); }
    if (count != 0) memcpy(array->items, items, count * sizeof(Value));
    array->count = count;
    value.type = VAL_ARRAY;
    value.as.obj = array;
    return value;
}

Value result_val(bool ok, int code, const char *message) {
    Value value;
    ObjResult *result;
    size_t length = message == NULL ? 0 : strlen(message);
    result = malloc(sizeof(*result));
    if (result == NULL) return nil_val();
    result->message = malloc(length + 1);
    if (result->message == NULL) { free(result); return nil_val(); }
    if (length != 0) memcpy(result->message, message, length);
    result->message[length] = '\0';
    result->ok = ok;
    result->code = code;
    value.type = VAL_RESULT;
    value.as.obj = result;
    return value;
}

Value free_value(Value value) {
    Value nil = nil_val();
    if (value.as.obj == NULL) return nil;
    switch (value.type) {
        case VAL_STRING: free(value.as.obj); break;
        case VAL_BYTES: { ObjBytes *bytes = value.as.obj; free(bytes->data); free(bytes); break; }
        case VAL_ARRAY: { ObjArray *array = value.as.obj; size_t i; for (i = 0; i < array->count; ++i) free_value(array->items[i]); free(array->items); free(array); break; }
        case VAL_RESULT: { ObjResult *result = value.as.obj; free(result->message); free(result); break; }
        default: return value;
    }
    return nil;
}
bool value_get_field(Value object, const char *name, Value *result) {
    ObjResult *item;
    if (name == NULL || result == NULL || object.type != VAL_RESULT) return false;
    item = object.as.obj;
    if (strcmp(name, "ok") == 0) *result = bool_val(item->ok);
    else if (strcmp(name, "code") == 0) *result = int_val(item->code);
    else if (strcmp(name, "message") == 0) *result = string_val(item->message, strlen(item->message));
    else return false;
    return true;
}

Value nil_val(void) {
    Value value;
    value.type = VAL_NIL;
    value.as.integer = 0;
    return value;
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
            printf("<fn>");
            break;
        case VAL_NATIVE:
            printf("<native>");
            break;
        case VAL_STRING:
            printf("%s", (char*)val.as.obj);
            break;
        case VAL_BYTES:
            printf("<bytes>");
            break;
        case VAL_ARRAY:
            printf("<array>");
            break;
        case VAL_RESULT:
            printf("<result>");
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
        case VAL_FUNC:
        case VAL_NATIVE:
        case VAL_STRING:
        case VAL_BYTES:
        case VAL_ARRAY:
        case VAL_RESULT:
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
        case VAL_FUNC:
        case VAL_NATIVE:
        case VAL_STRING:
        case VAL_BYTES:
        case VAL_ARRAY:
        case VAL_RESULT:
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
        case VAL_FUNC:
        case VAL_NATIVE:
        case VAL_STRING:
        case VAL_BYTES:
        case VAL_ARRAY:
        case VAL_RESULT:
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
