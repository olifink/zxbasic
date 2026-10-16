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

void program_delete_range(Program *p, uint16_t from, uint16_t to) {
    if (!p || p->count == 0 || from > to) return;

    size_t write_idx = 0;
    for (size_t read_idx = 0; read_idx < p->count; ++read_idx) {
        uint16_t line_no = p->lines[read_idx].line_no;
        if (line_no >= from && line_no <= to) {
            free(p->lines[read_idx].source);
        } else {
            if (write_idx != read_idx) {
                p->lines[write_idx] = p->lines[read_idx];
            }
            write_idx++;
        }
    }
    p->count = write_idx;
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

void program_list_range(const Program *p, uint16_t from, uint16_t to, FILE *out) {
    if (!p || !out || p->count == 0 || from > to) return;

    ssize_t start_idx = program_find_ge_index(p, from);
    if (start_idx < 0) {
        return;
    }

    for (size_t i = (size_t)start_idx; i < p->count; i++) {
        if (p->lines[i].line_no > to) break;
        fprintf(out, "%u %s\n", (unsigned int)p->lines[i].line_no, p->lines[i].source);
    }
}

void program_list(const Program *p, uint16_t start_line, FILE *out) {
    uint16_t from = (start_line == 0) ? MIN_LINE_NO : start_line;
    program_list_range(p, from, MAX_LINE_NO, out);
}

#include "lexer.h"
#include <math.h>
#include <ctype.h>

void program_llist(const Program *p, FILE *out) {
    if (!p || !out || p->count == 0) return;

    // 1. Scan program to identify GOTO/GOSUB/RUN/RESTORE/THEN jump target lines
    bool is_target[10000];
    memset(is_target, 0, sizeof(is_target));

    for (size_t i = 0; i < p->count; i++) {
        if (!p->lines[i].source) continue;
        Lexer l;
        lexer_init(&l, p->lines[i].source);
        while (l.current.type != TOKEN_EOF) {
            if (l.current.type == TOKEN_GOTO || l.current.type == TOKEN_GOSUB ||
                l.current.type == TOKEN_RUN || l.current.type == TOKEN_RESTORE) {
                lexer_next(&l);
                if (l.current.type == TOKEN_NUMBER) {
                    long tgt = (long)l.current.num_val;
                    if (tgt >= 1 && tgt <= 9999) is_target[tgt] = true;
                }
                continue;
            }
            if (l.current.type == TOKEN_THEN) {
                lexer_next(&l);
                if (l.current.type == TOKEN_NUMBER) {
                    long tgt = (long)l.current.num_val;
                    if (tgt >= 1 && tgt <= 9999) is_target[tgt] = true;
                }
                continue;
            }
            lexer_next(&l);
        }
        token_free(&l.current);
    }

    // 2. Print each line with loop indentation and syntax coloring
    int indent_level = 0;

    for (size_t i = 0; i < p->count; i++) {
        uint16_t line_no = p->lines[i].line_no;
        const char *src = p->lines[i].source;
        if (!src) continue;

        // Pre-scan line for NEXT to dedent BEFORE printing this line
        int line_for_count = 0;
        int line_next_count = 0;
        bool starts_with_next = false;
        {
            Lexer l;
            lexer_init(&l, src);
            if (l.current.type == TOKEN_NEXT) {
                starts_with_next = true;
            }
            while (l.current.type != TOKEN_EOF) {
                if (l.current.type == TOKEN_FOR) line_for_count++;
                else if (l.current.type == TOKEN_NEXT) line_next_count++;
                lexer_next(&l);
            }
            token_free(&l.current);
        }

        if (starts_with_next && indent_level > 0) {
            indent_level--;
            line_next_count--;
        }

        // Line number formatting: right-aligned 4 digits
        if (line_no < 10000 && is_target[line_no]) {
            fprintf(out, "\033[1;35m▶%4u \033[0m", (unsigned int)line_no);
        } else {
            fprintf(out, " \033[36m%4u \033[0m", (unsigned int)line_no);
        }

        // Indentation
        for (int s = 0; s < indent_level * 2; s++) {
            fputc(' ', out);
        }

        // Syntax highlighting
        Lexer l;
        lexer_init(&l, src);
        size_t last_cursor = 0;
        bool after_jump = false;

        while (l.current.type != TOKEN_EOF) {
            size_t tok_start = (l.current.col > 0) ? l.current.col - 1 : last_cursor;
            if (tok_start > last_cursor && tok_start <= strlen(src)) {
                for (size_t c = last_cursor; c < tok_start; c++) {
                    fputc(src[c], out);
                }
            }

            switch (l.current.type) {
                case TOKEN_STRING:
                    after_jump = false;
                    fprintf(out, "\033[32m\"%s\"\033[0m", l.current.str_val ? l.current.str_val : "");
                    break;

                case TOKEN_NUMBER: {
                    size_t tok_len = l.cursor > tok_start ? l.cursor - tok_start : 0;
                    if (after_jump) {
                        fprintf(out, "\033[1;35m▶%.*s\033[0m", (int)tok_len, &src[tok_start]);
                    } else {
                        fprintf(out, "\033[35m%.*s\033[0m", (int)tok_len, &src[tok_start]);
                    }
                    after_jump = false;
                    break;
                }

                case TOKEN_REM:
                    after_jump = false;
                    fprintf(out, "\033[90m%s\033[0m", &src[tok_start]);
                    break;

                case TOKEN_IDENT:
                    after_jump = false;
                    fprintf(out, "%s", l.current.str_val ? l.current.str_val : "");
                    break;

                default:
                    if (l.current.type >= TOKEN_LET && l.current.type <= TOKEN_INKEY_STR) {
                        fprintf(out, "\033[1;33m%s\033[0m", token_type_name(l.current.type));
                        if (l.current.type == TOKEN_GOTO || l.current.type == TOKEN_GOSUB || l.current.type == TOKEN_THEN) {
                            after_jump = true;
                        } else {
                            after_jump = false;
                        }
                    } else {
                        after_jump = false;
                        size_t tok_len = l.cursor > tok_start ? l.cursor - tok_start : 1;
                        fprintf(out, "%.*s", (int)tok_len, &src[tok_start]);
                    }
                    break;
            }

            if (l.current.type == TOKEN_REM) {
                last_cursor = strlen(src);
                break;
            }

            last_cursor = l.cursor;
            lexer_next(&l);
        }

        if (last_cursor < strlen(src)) {
            fputs(&src[last_cursor], out);
        }

        token_free(&l.current);
        fprintf(out, "\033[0m\n");

        indent_level += line_for_count;
        indent_level -= line_next_count;
        if (indent_level < 0) indent_level = 0;
    }
}

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
