#ifndef ZXBASIC_VALUE_H
#define ZXBASIC_VALUE_H

#include <stddef.h>
#include <stdint.h>
#include <stdbool.h>
#include "error.h"

typedef enum {
    VAL_NIL,
    VAL_NUM,
    VAL_STR
} ValueType;

typedef struct {
    ValueType type;
    union {
        double num;
        struct {
            char *chars;
            size_t len;
        } str;
    } as;
} Value;

Value value_nil(void);
Value value_number(double n);
Value value_string(const char *s);
Value value_string_len(const char *s, size_t len);
Value value_copy(const Value *v);
void value_free(Value *v);

bool value_is_num(const Value *v);
bool value_is_str(const Value *v);
bool value_is_truthy(const Value *v);

// Convert value to newly allocated formatted string (caller must free)
char *value_to_str(const Value *v);

// Binary operations
Value value_add(const Value *a, const Value *b, BasicError *err);
Value value_sub(const Value *a, const Value *b, BasicError *err);
Value value_mul(const Value *a, const Value *b, BasicError *err);
Value value_div(const Value *a, const Value *b, BasicError *err);
Value value_pow(const Value *a, const Value *b, BasicError *err);

// Unary operations
Value value_neg(const Value *a, BasicError *err);
Value value_pos(const Value *a, BasicError *err);
Value value_not(const Value *a, BasicError *err);

// Relational operations (return 1.0 or 0.0)
Value value_eq(const Value *a, const Value *b, BasicError *err);
Value value_ne(const Value *a, const Value *b, BasicError *err);
Value value_lt(const Value *a, const Value *b, BasicError *err);
Value value_gt(const Value *a, const Value *b, BasicError *err);
Value value_le(const Value *a, const Value *b, BasicError *err);
Value value_ge(const Value *a, const Value *b, BasicError *err);

// Sinclair logical shortcuts
Value value_and(const Value *a, const Value *b, BasicError *err);
Value value_or(const Value *a, const Value *b, BasicError *err);

// String slicing (1-based indices)
Value value_slice(const Value *s, int64_t start, int64_t end, bool has_start, bool has_end, BasicError *err);

#endif // ZXBASIC_VALUE_H
