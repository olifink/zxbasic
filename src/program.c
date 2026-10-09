#define _POSIX_C_SOURCE 200809L

#include "program.h"
#include <stdlib.h>
#include <string.h>

static size_t binary_search_insert_pos(const Program *p, uint16_t line_no, bool *found) {
    size_t low = 0;
    size_t high = p->count;

    while (low < high) {
        size_t mid = low + (high - low) / 2;
        if (p->lines[mid].line_no == line_no) {
            *found = true;
            return mid;
        } else if (p->lines[mid].line_no < line_no) {
            low = mid + 1;
        } else {
            high = mid;
        }
    }

    *found = false;
    return low;
}

void program_init(Program *p) {
    p->lines = NULL;
    p->count = 0;
    p->capacity = 0;
}

void program_free(Program *p) {
    if (!p) return;
    for (size_t i = 0; i < p->count; i++) {
        free(p->lines[i].source);
    }
    free(p->lines);
    p->lines = NULL;
    p->count = 0;
    p->capacity = 0;
}

void program_clear(Program *p) {
    if (!p) return;
    for (size_t i = 0; i < p->count; i++) {
        free(p->lines[i].source);
        p->lines[i].source = NULL;
    }
    p->count = 0;
}

bool program_insert_or_replace(Program *p, uint16_t line_no, const char *source) {
    if (!p || line_no < MIN_LINE_NO || line_no > MAX_LINE_NO) {
        return false;
    }

    bool found = false;
    size_t idx = binary_search_insert_pos(p, line_no, &found);

    char *source_copy = strdup(source ? source : "");
    if (!source_copy) {
        return false;
    }

    if (found) {
        free(p->lines[idx].source);
        p->lines[idx].source = source_copy;
        return true;
    }

    if (p->count >= p->capacity) {
        size_t new_cap = (p->capacity == 0) ? 16 : p->capacity * 2;
        ProgramLine *new_lines = realloc(p->lines, new_cap * sizeof(ProgramLine));
        if (!new_lines) {
            free(source_copy);
            return false;
        }
        p->lines = new_lines;
        p->capacity = new_cap;
    }

    if (idx < p->count) {
        memmove(&p->lines[idx + 1], &p->lines[idx], (p->count - idx) * sizeof(ProgramLine));
    }

    p->lines[idx].line_no = line_no;
    p->lines[idx].source = source_copy;
    p->count++;

    return true;
}

bool program_delete(Program *p, uint16_t line_no) {
    if (!p || p->count == 0) return false;

    bool found = false;
    size_t idx = binary_search_insert_pos(p, line_no, &found);
    if (!found) {
        return false;
    }

    free(p->lines[idx].source);
    if (idx + 1 < p->count) {
        memmove(&p->lines[idx], &p->lines[idx + 1], (p->count - idx - 1) * sizeof(ProgramLine));
    }
    p->count--;
    return true;
}

ssize_t program_find_index(const Program *p, uint16_t line_no) {
    if (!p || p->count == 0) return -1;
    bool found = false;
    size_t idx = binary_search_insert_pos(p, line_no, &found);
    return found ? (ssize_t)idx : -1;
}

ssize_t program_find_ge_index(const Program *p, uint16_t target_line) {
    if (!p || p->count == 0) return -1;
    bool found = false;
    size_t idx = binary_search_insert_pos(p, target_line, &found);
    if (idx < p->count) {
        return (ssize_t)idx;
    }
    return -1;
}

const ProgramLine *program_get_line(const Program *p, size_t index) {
    if (!p || index >= p->count) {
        return NULL;
    }
    return &p->lines[index];
}

void program_list(const Program *p, uint16_t start_line, FILE *out) {
    if (!p || !out || p->count == 0) return;

    ssize_t start_idx = 0;
    if (start_line > 0) {
        start_idx = program_find_ge_index(p, start_line);
        if (start_idx < 0) {
            return;
        }
    }

    for (size_t i = (size_t)start_idx; i < p->count; i++) {
        fprintf(out, "%u %s\n", (unsigned int)p->lines[i].line_no, p->lines[i].source);
    }
}

#include "lexer.h"
#include <math.h>
#include <ctype.h>

typedef struct {
    uint16_t old_line;
    uint16_t new_line;
} LineMap;

static char *patch_line_branch_targets(const char *source, uint16_t old_line_no, size_t count, const LineMap *map, FILE *warn_out) {
    Lexer l;
    lexer_init(&l, source);

    size_t cap = strlen(source) + 128;
    char *out = malloc(cap);
    if (!out) {
        token_free(&l.current);
        return strdup(source);
    }
    size_t out_len = 0;
    size_t src_pos = 0;

    TokenType prev_type = TOKEN_EOF;

    while (l.current.type != TOKEN_EOF) {
        if (l.current.type == TOKEN_NUMBER) {
            if (prev_type == TOKEN_GOTO || prev_type == TOKEN_GOSUB ||
                prev_type == TOKEN_RESTORE || prev_type == TOKEN_THEN) {
                
                double num_val = l.current.num_val;
                if (floor(num_val) == num_val && num_val >= MIN_LINE_NO && num_val <= MAX_LINE_NO) {
                    uint16_t target = (uint16_t)num_val;
                    ssize_t mapped_idx = -1;
                    for (size_t k = 0; k < count; k++) {
                        if (map[k].old_line == target) {
                            mapped_idx = (ssize_t)k;
                            break;
                        }
                    }

                    size_t start_col = (l.current.col > 0) ? l.current.col - 1 : 0;
                    if (start_col > src_pos) {
                        size_t chunk = start_col - src_pos;
                        while (out_len + chunk >= cap) { cap *= 2; out = realloc(out, cap); }
                        memcpy(out + out_len, source + src_pos, chunk);
                        out_len += chunk;
                    }

                    size_t end_col = start_col;
                    while (source[end_col] != '\0' && (isdigit((unsigned char)source[end_col]) || source[end_col] == '.')) {
                        end_col++;
                    }

                    if (mapped_idx >= 0) {
                        char num_buf[32];
                        snprintf(num_buf, sizeof(num_buf), "%u", (unsigned int)map[mapped_idx].new_line);
                        size_t nlen = strlen(num_buf);
                        while (out_len + nlen >= cap) { cap *= 2; out = realloc(out, cap); }
                        memcpy(out + out_len, num_buf, nlen);
                        out_len += nlen;
                    } else {
                        if (warn_out) {
                            fprintf(warn_out, "Warning: Line reference %u not found at line %u\n", (unsigned int)target, (unsigned int)old_line_no);
                        }
                        size_t nlen = end_col - start_col;
                        while (out_len + nlen >= cap) { cap *= 2; out = realloc(out, cap); }
                        memcpy(out + out_len, source + start_col, nlen);
                        out_len += nlen;
                    }

                    src_pos = end_col;
                }
            }
        }

        prev_type = l.current.type;
        lexer_next(&l);
    }

    token_free(&l.current);

    size_t rem = strlen(source + src_pos);
    while (out_len + rem >= cap) { cap = out_len + rem + 1; out = realloc(out, cap); }
    memcpy(out + out_len, source + src_pos, rem);
    out_len += rem;
    out[out_len] = '\0';

    return out;
}

bool program_renumber(Program *p, uint16_t start_line, uint16_t step, BasicError *err, FILE *warn_out) {
    if (!p || p->count == 0) return true;

    if (start_line == 0) start_line = 10;
    if (step == 0) step = 10;

    if (start_line < MIN_LINE_NO || start_line > MAX_LINE_NO || step < 1) {
        if (err) err->code = ERR_INTEGER_RANGE;
        return false;
    }

    if (p->count > 1) {
        uint64_t max_new = (uint64_t)start_line + (uint64_t)(p->count - 1) * (uint64_t)step;
        if (max_new > MAX_LINE_NO) {
            if (err) err->code = ERR_INTEGER_RANGE;
            return false;
        }
    }

    LineMap *map = malloc(p->count * sizeof(LineMap));
    if (!map) {
        if (err) err->code = ERR_OUT_OF_MEMORY;
        return false;
    }

    for (size_t i = 0; i < p->count; i++) {
        map[i].old_line = p->lines[i].line_no;
        map[i].new_line = (uint16_t)(start_line + i * step);
    }

    char **new_sources = malloc(p->count * sizeof(char *));
    if (!new_sources) {
        free(map);
        if (err) err->code = ERR_OUT_OF_MEMORY;
        return false;
    }

    for (size_t i = 0; i < p->count; i++) {
        new_sources[i] = patch_line_branch_targets(p->lines[i].source, p->lines[i].line_no, p->count, map, warn_out);
    }

    for (size_t i = 0; i < p->count; i++) {
        p->lines[i].line_no = map[i].new_line;
        free(p->lines[i].source);
        p->lines[i].source = new_sources[i];
    }

    free(new_sources);
    free(map);
    return true;
}
