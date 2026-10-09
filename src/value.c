#define _POSIX_C_SOURCE 200809L

#include "value.h"
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <math.h>

Value value_nil(void) {
    Value v;
    v.type = VAL_NIL;
    v.as.num = 0.0;
    return v;
}

Value value_number(double n) {
    Value v;
    v.type = VAL_NUM;
    v.as.num = n;
    return v;
}

Value value_string_len(const char *s, size_t len) {
    Value v;
    v.type = VAL_STR;
    v.as.str.len = len;
    v.as.str.chars = malloc(len + 1);
    if (v.as.str.chars) {
        if (s && len > 0) {
            memcpy(v.as.str.chars, s, len);
        }
        v.as.str.chars[len] = '\0';
    } else {
        v.as.str.len = 0;
    }
    return v;
}

Value value_string(const char *s) {
    return value_string_len(s, s ? strlen(s) : 0);
}

Value value_copy(const Value *v) {
    if (!v) return value_nil();
    if (v->type == VAL_STR) {
        return value_string_len(v->as.str.chars, v->as.str.len);
    }
    return *v;
}

void value_free(Value *v) {
    if (!v) return;
    if (v->type == VAL_STR && v->as.str.chars) {
        free(v->as.str.chars);
        v->as.str.chars = NULL;
        v->as.str.len = 0;
    }
    v->type = VAL_NIL;
}

bool value_is_num(const Value *v) {
    return v && v->type == VAL_NUM;
}

bool value_is_str(const Value *v) {
    return v && v->type == VAL_STR;
}

bool value_is_truthy(const Value *v) {
    if (!v) return false;
    if (v->type == VAL_NUM) {
        return v->as.num != 0.0;
    }
    if (v->type == VAL_STR) {
        return v->as.str.len > 0;
    }
    return false;
}

char *value_to_str(const Value *v) {
    if (!v) return strdup("");
    if (v->type == VAL_STR) {
        return strdup(v->as.str.chars ? v->as.str.chars : "");
    }
    if (v->type == VAL_NUM) {
        char buf[64];
        if (isnan(v->as.num)) {
            snprintf(buf, sizeof(buf), "NaN");
        } else if (isinf(v->as.num)) {
            snprintf(buf, sizeof(buf), "Inf");
        } else if (floor(v->as.num) == v->as.num && fabs(v->as.num) < 1e14) {
            snprintf(buf, sizeof(buf), "%.0f", v->as.num);
        } else {
            snprintf(buf, sizeof(buf), "%.8g", v->as.num);
        }
        return strdup(buf);
    }
    return strdup("");
}

Value value_add(const Value *a, const Value *b, BasicError *err) {
    if (value_is_num(a) && value_is_num(b)) {
        return value_number(a->as.num + b->as.num);
    }
    if (value_is_str(a) && value_is_str(b)) {
        size_t total_len = a->as.str.len + b->as.str.len;
        char *joined = malloc(total_len + 1);
        if (!joined) {
            if (err) err->code = ERR_OUT_OF_MEMORY;
            return value_nil();
        }
        memcpy(joined, a->as.str.chars, a->as.str.len);
        memcpy(joined + a->as.str.len, b->as.str.chars, b->as.str.len);
        joined[total_len] = '\0';
        Value res;
        res.type = VAL_STR;
        res.as.str.chars = joined;
        res.as.str.len = total_len;
        return res;
    }
    if (err) err->code = ERR_NONSENSE;
    return value_nil();
}

Value value_sub(const Value *a, const Value *b, BasicError *err) {
    if (value_is_num(a) && value_is_num(b)) {
        return value_number(a->as.num - b->as.num);
    }
    if (err) err->code = ERR_NONSENSE;
    return value_nil();
}

Value value_mul(const Value *a, const Value *b, BasicError *err) {
    if (value_is_num(a) && value_is_num(b)) {
        return value_number(a->as.num * b->as.num);
    }
    if (err) err->code = ERR_NONSENSE;
    return value_nil();
}

Value value_div(const Value *a, const Value *b, BasicError *err) {
    if (value_is_num(a) && value_is_num(b)) {
        if (b->as.num == 0.0) {
            if (err) err->code = ERR_INTEGER_RANGE;
            return value_nil();
        }
        return value_number(a->as.num / b->as.num);
    }
    if (err) err->code = ERR_NONSENSE;
    return value_nil();
}

Value value_pow(const Value *a, const Value *b, BasicError *err) {
    if (value_is_num(a) && value_is_num(b)) {
        return value_number(pow(a->as.num, b->as.num));
    }
    if (err) err->code = ERR_NONSENSE;
    return value_nil();
}

Value value_neg(const Value *a, BasicError *err) {
    if (value_is_num(a)) {
        return value_number(-a->as.num);
    }
    if (err) err->code = ERR_NONSENSE;
    return value_nil();
}

Value value_pos(const Value *a, BasicError *err) {
    if (value_is_num(a)) {
        return value_number(a->as.num);
    }
    if (err) err->code = ERR_NONSENSE;
    return value_nil();
}

Value value_not(const Value *a, BasicError *err) {
    if (value_is_num(a)) {
        return value_number(a->as.num == 0.0 ? 1.0 : 0.0);
    }
    if (err) err->code = ERR_NONSENSE;
    return value_nil();
}

static int compare_values(const Value *a, const Value *b, bool *type_err) {
    if (value_is_num(a) && value_is_num(b)) {
        if (a->as.num < b->as.num) return -1;
        if (a->as.num > b->as.num) return 1;
        return 0;
    }
    if (value_is_str(a) && value_is_str(b)) {
        return strcmp(a->as.str.chars, b->as.str.chars);
    }
    *type_err = true;
    return 0;
}

Value value_eq(const Value *a, const Value *b, BasicError *err) {
    bool type_err = false;
    int cmp = compare_values(a, b, &type_err);
    if (type_err) { if (err) err->code = ERR_NONSENSE; return value_nil(); }
    return value_number(cmp == 0 ? 1.0 : 0.0);
}

Value value_ne(const Value *a, const Value *b, BasicError *err) {
    bool type_err = false;
    int cmp = compare_values(a, b, &type_err);
    if (type_err) { if (err) err->code = ERR_NONSENSE; return value_nil(); }
    return value_number(cmp != 0 ? 1.0 : 0.0);
}

Value value_lt(const Value *a, const Value *b, BasicError *err) {
    bool type_err = false;
    int cmp = compare_values(a, b, &type_err);
    if (type_err) { if (err) err->code = ERR_NONSENSE; return value_nil(); }
    return value_number(cmp < 0 ? 1.0 : 0.0);
}

Value value_gt(const Value *a, const Value *b, BasicError *err) {
    bool type_err = false;
    int cmp = compare_values(a, b, &type_err);
    if (type_err) { if (err) err->code = ERR_NONSENSE; return value_nil(); }
    return value_number(cmp > 0 ? 1.0 : 0.0);
}

Value value_le(const Value *a, const Value *b, BasicError *err) {
    bool type_err = false;
    int cmp = compare_values(a, b, &type_err);
    if (type_err) { if (err) err->code = ERR_NONSENSE; return value_nil(); }
    return value_number(cmp <= 0 ? 1.0 : 0.0);
}

Value value_ge(const Value *a, const Value *b, BasicError *err) {
    bool type_err = false;
    int cmp = compare_values(a, b, &type_err);
    if (type_err) { if (err) err->code = ERR_NONSENSE; return value_nil(); }
    return value_number(cmp >= 0 ? 1.0 : 0.0);
}

// Sinclair shortcut: a AND b -> returns a if b is true (non-zero), else 0 (or "" for string)
Value value_and(const Value *a, const Value *b, BasicError *err) {
    if (!value_is_num(b)) {
        if (err) err->code = ERR_NONSENSE;
        return value_nil();
    }
    if (b->as.num != 0.0) {
        return value_copy(a);
    }
    if (value_is_str(a)) {
        return value_string("");
    }
    return value_number(0.0);
}

// Sinclair shortcut: a OR b -> returns a if a is true, else b
Value value_or(const Value *a, const Value *b, BasicError *err) {
    if (value_is_num(a)) {
        if (a->as.num != 0.0) {
            return value_copy(a);
        }
        if (value_is_num(b)) {
            return value_copy(b);
        }
        if (err) err->code = ERR_NONSENSE;
        return value_nil();
    }
    if (err) err->code = ERR_NONSENSE;
    return value_nil();
}

Value value_slice(const Value *s, int64_t start, int64_t end, bool has_start, bool has_end, BasicError *err) {
    if (!value_is_str(s)) {
        if (err) err->code = ERR_NONSENSE;
        return value_nil();
    }

    int64_t len = (int64_t)s->as.str.len;
    if (!has_start) start = 1;
    if (!has_end) end = len;

    // Sinclair rule: if start > end, result is empty string
    if (start > end) {
        if (start < 1 || end < 0 || start > len + 1 || end > len) {
            if (err) err->code = ERR_SUBSCRIPT_RANGE;
            return value_nil();
        }
        return value_string("");
    }

    if (start < 1 || start > len || end < 1 || end > len) {
        if (err) err->code = ERR_SUBSCRIPT_RANGE;
        return value_nil();
    }

    size_t slice_len = (size_t)(end - start + 1);
    return value_string_len(s->as.str.chars + (start - 1), slice_len);
}
