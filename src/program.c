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
