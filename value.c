#include "value.h"
#include <stdio.h>

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
    v.as.integer = 0;
    return v;
}

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
    }
}

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

int64_t as_int(Value val) {
    switch (val.type) {
        case VAL_INT:
            return val.as.integer;
        case VAL_DOUBLE:
            return (int64_t)val.as.real;
        case VAL_BOOL:
            return val.as.boolean ? 1 : 0;
        case VAL_NIL:
        default:
            return 0;
    }
}

double as_double(Value val) {
    switch (val.type) {
        case VAL_INT:
            return (double)val.as.integer;
        case VAL_DOUBLE:
            return val.as.real;
        case VAL_BOOL:
            return val.as.boolean ? 1.0 : 0.0;
        case VAL_NIL:
        default:
            return 0.0;
    }
}

Value add_values(Value a, Value b) {
    if (a.type == VAL_INT && b.type == VAL_INT) {
        return int_val(a.as.integer + b.as.integer);
    }
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
    if (a.type == VAL_INT && b.type == VAL_INT) {
        if (b.as.integer == 0) {
            printf("[Runtime Warning] Division by zero!\n");
            return nil_val();
        }
        // If it's pure integer division, keep C rules or return double?
        // Let's perform double division or integer division. In C, it is integer division.
        // Let's do integer division for ints, double for doubles.
        return int_val(a.as.integer / b.as.integer);
    }
    double denom = as_double(b);
    if (denom == 0.0) {
        printf("[Runtime Warning] Division by zero!\n");
        return nil_val();
    }
    return double_val(as_double(a) / denom);
}

Value mod_values(Value a, Value b) {
    int64_t denom = as_int(b);
    if (denom == 0) {
        printf("[Runtime Warning] Modulo by zero!\n");
        return nil_val();
    }
    return int_val(as_int(a) % denom);
}

Value bitwise_and_values(Value a, Value b) {
    return int_val(as_int(a) & as_int(b));
}

Value bitwise_or_values(Value a, Value b) {
    return int_val(as_int(a) | as_int(b));
}

Value bitwise_xor_values(Value a, Value b) {
    return int_val(as_int(a) ^ as_int(b));
}

Value bitwise_not_value(Value a) {
    return int_val(~as_int(a));
}

Value bitwise_shl_values(Value a, Value b) {
    return int_val(as_int(a) << as_int(b));
}

Value bitwise_shr_values(Value a, Value b) {
    return int_val(as_int(a) >> as_int(b));
}

Value logical_not_value(Value a) {
    return bool_val(!is_truthy(a));
}

Value eq_values(Value a, Value b) {
    if (a.type == VAL_NIL && b.type == VAL_NIL) return bool_val(true);
    if (a.type == VAL_NIL || b.type == VAL_NIL) return bool_val(false);
    if (a.type == VAL_BOOL && b.type == VAL_BOOL) {
        return bool_val(a.as.boolean == b.as.boolean);
    }
    if (a.type == VAL_INT && b.type == VAL_INT) {
        return bool_val(a.as.integer == b.as.integer);
    }
    return bool_val(as_double(a) == as_double(b));
}

Value neq_values(Value a, Value b) {
    Value eq = eq_values(a, b);
    return bool_val(!eq.as.boolean);
}

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
