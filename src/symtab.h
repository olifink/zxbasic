#ifndef ZXBASIC_SYMTAB_H
#define ZXBASIC_SYMTAB_H

#include <stddef.h>
#include <stdint.h>
#include <stdbool.h>
#include "value.h"
#include "error.h"

#define MAX_ARRAY_DIMS 8

typedef struct {
    char *name;
    double val;
} NumVar;

typedef struct {
    char *name;
    char *val;
    size_t len;
} StrVar;

typedef struct {
    char *name;
    size_t ndims;
    size_t dims[MAX_ARRAY_DIMS];
    size_t total_elements;
    double *data;
} NumArray;

typedef struct {
    char *name;
    size_t ndims;
    size_t dims[MAX_ARRAY_DIMS];
    size_t total_elements;
    char *data; // In Sinclair BASIC, string arrays are character matrices initialized to spaces
} StrArray;

typedef struct {
    NumVar *num_vars;
    size_t num_vars_count;
    size_t num_vars_cap;

    StrVar *str_vars;
    size_t str_vars_count;
    size_t str_vars_cap;

    NumArray *num_arrays;
    size_t num_arrays_count;
    size_t num_arrays_cap;

    StrArray *str_arrays;
    size_t str_arrays_count;
    size_t str_arrays_cap;
} SymTab;

void symtab_init(SymTab *st);
void symtab_clear(SymTab *st);
void symtab_free(SymTab *st);

// Scalar numbers
bool symtab_get_num(const SymTab *st, const char *name, double *out);
bool symtab_set_num(SymTab *st, const char *name, double val);

// Scalar strings
bool symtab_get_str(const SymTab *st, const char *name, const char **out_str, size_t *out_len);
bool symtab_set_str(SymTab *st, const char *name, const char *str, size_t len);

// Numeric Arrays (1-based indices)
bool symtab_dim_num_array(SymTab *st, const char *name, size_t ndims, const size_t *dims, BasicError *err);
bool symtab_get_num_array(const SymTab *st, const char *name, size_t nidx, const size_t *indices, double *out, BasicError *err);
bool symtab_set_num_array(SymTab *st, const char *name, size_t nidx, const size_t *indices, double val, BasicError *err);

// String Arrays / Character matrices (1-based indices)
bool symtab_dim_str_array(SymTab *st, const char *name, size_t ndims, const size_t *dims, BasicError *err);
bool symtab_get_str_array(const SymTab *st, const char *name, size_t nidx, const size_t *indices, Value *out, BasicError *err);
bool symtab_set_str_array(SymTab *st, const char *name, size_t nidx, const size_t *indices, const Value *val, BasicError *err);

#endif // ZXBASIC_SYMTAB_H
