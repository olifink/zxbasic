#define _POSIX_C_SOURCE 200809L

#include "symtab.h"
#include <stdlib.h>
#include <string.h>

void symtab_init(SymTab *st) {
    memset(st, 0, sizeof(*st));
}

void symtab_clear(SymTab *st) {
    if (!st) return;

    for (size_t i = 0; i < st->num_vars_count; i++) {
        free(st->num_vars[i].name);
    }
    st->num_vars_count = 0;

    for (size_t i = 0; i < st->str_vars_count; i++) {
        free(st->str_vars[i].name);
        free(st->str_vars[i].val);
    }
    st->str_vars_count = 0;

    for (size_t i = 0; i < st->num_arrays_count; i++) {
        free(st->num_arrays[i].name);
        free(st->num_arrays[i].data);
    }
    st->num_arrays_count = 0;

    for (size_t i = 0; i < st->str_arrays_count; i++) {
        free(st->str_arrays[i].name);
        free(st->str_arrays[i].data);
    }
    st->str_arrays_count = 0;
}

void symtab_free(SymTab *st) {
    if (!st) return;
    symtab_clear(st);

    free(st->num_vars);
    free(st->str_vars);
    free(st->num_arrays);
    free(st->str_arrays);
    memset(st, 0, sizeof(*st));
}

bool symtab_get_num(const SymTab *st, const char *name, double *out) {
    if (!st || !name) return false;
    for (size_t i = 0; i < st->num_vars_count; i++) {
        if (strcmp(st->num_vars[i].name, name) == 0) {
            if (out) *out = st->num_vars[i].val;
            return true;
        }
    }
    return false;
}

bool symtab_set_num(SymTab *st, const char *name, double val) {
    if (!st || !name) return false;
    for (size_t i = 0; i < st->num_vars_count; i++) {
        if (strcmp(st->num_vars[i].name, name) == 0) {
            st->num_vars[i].val = val;
            return true;
        }
    }

    if (st->num_vars_count >= st->num_vars_cap) {
        size_t new_cap = (st->num_vars_cap == 0) ? 16 : st->num_vars_cap * 2;
        NumVar *new_arr = realloc(st->num_vars, new_cap * sizeof(NumVar));
        if (!new_arr) return false;
        st->num_vars = new_arr;
        st->num_vars_cap = new_cap;
    }

    char *copy_name = strdup(name);
    if (!copy_name) return false;

    st->num_vars[st->num_vars_count].name = copy_name;
    st->num_vars[st->num_vars_count].val = val;
    st->num_vars_count++;
    return true;
}

bool symtab_get_str(const SymTab *st, const char *name, const char **out_str, size_t *out_len) {
    if (!st || !name) return false;
    for (size_t i = 0; i < st->str_vars_count; i++) {
        if (strcmp(st->str_vars[i].name, name) == 0) {
            if (out_str) *out_str = st->str_vars[i].val;
            if (out_len) *out_len = st->str_vars[i].len;
            return true;
        }
    }
    return false;
}

bool symtab_set_str(SymTab *st, const char *name, const char *str, size_t len) {
    if (!st || !name) return false;

    for (size_t i = 0; i < st->str_vars_count; i++) {
        if (strcmp(st->str_vars[i].name, name) == 0) {
            char *new_val = malloc(len + 1);
            if (!new_val) return false;
            if (str && len > 0) memcpy(new_val, str, len);
            new_val[len] = '\0';

            free(st->str_vars[i].val);
            st->str_vars[i].val = new_val;
            st->str_vars[i].len = len;
            return true;
        }
    }

    if (st->str_vars_count >= st->str_vars_cap) {
        size_t new_cap = (st->str_vars_cap == 0) ? 16 : st->str_vars_cap * 2;
        StrVar *new_arr = realloc(st->str_vars, new_cap * sizeof(StrVar));
        if (!new_arr) return false;
        st->str_vars = new_arr;
        st->str_vars_cap = new_cap;
    }

    char *copy_name = strdup(name);
    char *copy_val = malloc(len + 1);
    if (!copy_name || !copy_val) {
        free(copy_name);
        free(copy_val);
        return false;
    }
    if (str && len > 0) memcpy(copy_val, str, len);
    copy_val[len] = '\0';

    st->str_vars[st->str_vars_count].name = copy_name;
    st->str_vars[st->str_vars_count].val = copy_val;
    st->str_vars[st->str_vars_count].len = len;
    st->str_vars_count++;
    return true;
}

static size_t calc_offset(size_t ndims, const size_t *dims, size_t nidx, const size_t *indices, BasicError *err) {
    if (nidx != ndims) {
        if (err) err->code = ERR_SUBSCRIPT_RANGE;
        return SIZE_MAX;
    }
    size_t offset = 0;
    for (size_t d = 0; d < ndims; d++) {
        size_t idx = indices[d];
        if (idx < 1 || idx > dims[d]) {
            if (err) err->code = ERR_SUBSCRIPT_RANGE;
            return SIZE_MAX;
        }
        offset = offset * dims[d] + (idx - 1);
    }
    return offset;
}

bool symtab_dim_num_array(SymTab *st, const char *name, size_t ndims, const size_t *dims, BasicError *err) {
    if (!st || !name || ndims == 0 || ndims > MAX_ARRAY_DIMS) {
        if (err) err->code = ERR_NONSENSE;
        return false;
    }

    // Check if array already exists
    for (size_t i = 0; i < st->num_arrays_count; i++) {
        if (strcmp(st->num_arrays[i].name, name) == 0) {
            if (err) err->code = ERR_NONSENSE; // Redimensioning error
            return false;
        }
    }

    size_t total = 1;
    for (size_t d = 0; d < ndims; d++) {
        if (dims[d] == 0) {
            if (err) err->code = ERR_SUBSCRIPT_RANGE;
            return false;
        }
        total *= dims[d];
    }

    double *data = calloc(total, sizeof(double));
    if (!data) {
        if (err) err->code = ERR_OUT_OF_MEMORY;
        return false;
    }

    if (st->num_arrays_count >= st->num_arrays_cap) {
        size_t new_cap = (st->num_arrays_cap == 0) ? 8 : st->num_arrays_cap * 2;
        NumArray *new_arr = realloc(st->num_arrays, new_cap * sizeof(NumArray));
        if (!new_arr) {
            free(data);
            if (err) err->code = ERR_OUT_OF_MEMORY;
            return false;
        }
        st->num_arrays = new_arr;
        st->num_arrays_cap = new_cap;
    }

    char *copy_name = strdup(name);
    if (!copy_name) {
        free(data);
        if (err) err->code = ERR_OUT_OF_MEMORY;
        return false;
    }

    NumArray *na = &st->num_arrays[st->num_arrays_count++];
    na->name = copy_name;
    na->ndims = ndims;
    memcpy(na->dims, dims, ndims * sizeof(size_t));
    na->total_elements = total;
    na->data = data;
    return true;
}

bool symtab_get_num_array(const SymTab *st, const char *name, size_t nidx, const size_t *indices, double *out, BasicError *err) {
    if (!st || !name) return false;
    for (size_t i = 0; i < st->num_arrays_count; i++) {
        if (strcmp(st->num_arrays[i].name, name) == 0) {
            const NumArray *na = &st->num_arrays[i];
            size_t off = calc_offset(na->ndims, na->dims, nidx, indices, err);
            if (off == SIZE_MAX) return false;
            if (out) *out = na->data[off];
            return true;
        }
    }
    if (err) err->code = ERR_VAR_NOT_FOUND;
    return false;
}

bool symtab_set_num_array(SymTab *st, const char *name, size_t nidx, const size_t *indices, double val, BasicError *err) {
    if (!st || !name) return false;
    for (size_t i = 0; i < st->num_arrays_count; i++) {
        if (strcmp(st->num_arrays[i].name, name) == 0) {
            NumArray *na = &st->num_arrays[i];
            size_t off = calc_offset(na->ndims, na->dims, nidx, indices, err);
            if (off == SIZE_MAX) return false;
            na->data[off] = val;
            return true;
        }
    }
    if (err) err->code = ERR_VAR_NOT_FOUND;
    return false;
}

bool symtab_dim_str_array(SymTab *st, const char *name, size_t ndims, const size_t *dims, BasicError *err) {
    if (!st || !name || ndims == 0 || ndims > MAX_ARRAY_DIMS) {
        if (err) err->code = ERR_NONSENSE;
        return false;
    }

    for (size_t i = 0; i < st->str_arrays_count; i++) {
        if (strcmp(st->str_arrays[i].name, name) == 0) {
            if (err) err->code = ERR_NONSENSE;
            return false;
        }
    }

    size_t total = 1;
    for (size_t d = 0; d < ndims; d++) {
        if (dims[d] == 0) {
            if (err) err->code = ERR_SUBSCRIPT_RANGE;
            return false;
        }
        total *= dims[d];
    }

    char *data = malloc(total);
    if (!data) {
        if (err) err->code = ERR_OUT_OF_MEMORY;
        return false;
    }
    // Sinclair string arrays are blank-padded (all spaces)
    memset(data, ' ', total);

    if (st->str_arrays_count >= st->str_arrays_cap) {
        size_t new_cap = (st->str_arrays_cap == 0) ? 8 : st->str_arrays_cap * 2;
        StrArray *new_arr = realloc(st->str_arrays, new_cap * sizeof(StrArray));
        if (!new_arr) {
            free(data);
            if (err) err->code = ERR_OUT_OF_MEMORY;
            return false;
        }
        st->str_arrays = new_arr;
        st->str_arrays_cap = new_cap;
    }

    char *copy_name = strdup(name);
    if (!copy_name) {
        free(data);
        if (err) err->code = ERR_OUT_OF_MEMORY;
        return false;
    }

    StrArray *sa = &st->str_arrays[st->str_arrays_count++];
    sa->name = copy_name;
    sa->ndims = ndims;
    memcpy(sa->dims, dims, ndims * sizeof(size_t));
    sa->total_elements = total;
    sa->data = data;
    return true;
}

bool symtab_get_str_array(const SymTab *st, const char *name, size_t nidx, const size_t *indices, Value *out, BasicError *err) {
    if (!st || !name) return false;
    for (size_t i = 0; i < st->str_arrays_count; i++) {
        if (strcmp(st->str_arrays[i].name, name) == 0) {
            const StrArray *sa = &st->str_arrays[i];
            if (nidx == sa->ndims) {
                // Returns single character
                size_t off = calc_offset(sa->ndims, sa->dims, nidx, indices, err);
                if (off == SIZE_MAX) return false;
                char c = sa->data[off];
                if (out) *out = value_string_len(&c, 1);
                return true;
            } else if (nidx == sa->ndims - 1) {
                // Returns string of length sa->dims[ndims - 1]
                size_t row_len = sa->dims[sa->ndims - 1];
                size_t full_idx[MAX_ARRAY_DIMS];
                for (size_t d = 0; d < nidx; d++) full_idx[d] = indices[d];
                full_idx[nidx] = 1; // 1-based start of row
                size_t off = calc_offset(sa->ndims, sa->dims, sa->ndims, full_idx, err);
                if (off == SIZE_MAX) return false;
                if (out) *out = value_string_len(sa->data + off, row_len);
                return true;
            } else {
                if (err) err->code = ERR_SUBSCRIPT_RANGE;
                return false;
            }
        }
    }
    if (err) err->code = ERR_VAR_NOT_FOUND;
    return false;
}

bool symtab_set_str_array(SymTab *st, const char *name, size_t nidx, const size_t *indices, const Value *val, BasicError *err) {
    if (!st || !name || !value_is_str(val)) {
        if (err) err->code = ERR_NONSENSE;
        return false;
    }
    for (size_t i = 0; i < st->str_arrays_count; i++) {
        if (strcmp(st->str_arrays[i].name, name) == 0) {
            StrArray *sa = &st->str_arrays[i];
            if (nidx == sa->ndims) {
                // Setting single character
                size_t off = calc_offset(sa->ndims, sa->dims, nidx, indices, err);
                if (off == SIZE_MAX) return false;
                sa->data[off] = (val->as.str.len > 0) ? val->as.str.chars[0] : ' ';
                return true;
            } else if (nidx == sa->ndims - 1) {
                // Setting row string
                size_t row_len = sa->dims[sa->ndims - 1];
                size_t full_idx[MAX_ARRAY_DIMS];
                for (size_t d = 0; d < nidx; d++) full_idx[d] = indices[d];
                full_idx[nidx] = 1;
                size_t off = calc_offset(sa->ndims, sa->dims, sa->ndims, full_idx, err);
                if (off == SIZE_MAX) return false;
                size_t copy_len = (val->as.str.len < row_len) ? val->as.str.len : row_len;
                memcpy(sa->data + off, val->as.str.chars, copy_len);
                // Pad with spaces if shorter
                if (copy_len < row_len) {
                    memset(sa->data + off + copy_len, ' ', row_len - copy_len);
                }
                return true;
            } else {
                if (err) err->code = ERR_SUBSCRIPT_RANGE;
                return false;
            }
        }
    }
    if (err) err->code = ERR_VAR_NOT_FOUND;
    return false;
}
