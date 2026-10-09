#ifndef ZXBASIC_PROGRAM_H
#define ZXBASIC_PROGRAM_H

#include <stddef.h>
#include <stdint.h>
#include <stdbool.h>
#include <stdio.h>
#include <sys/types.h>

#define MIN_LINE_NO 1
#define MAX_LINE_NO 9999

typedef struct {
    uint16_t line_no;
    char *source; // Raw text stripped of the leading line number
} ProgramLine;

typedef struct {
    ProgramLine *lines;
    size_t count;
    size_t capacity;
} Program;

void program_init(Program *p);
void program_free(Program *p);
void program_clear(Program *p);

// Inserts or replaces a line. Returns true on success, false if line_no out of range or OOM.
bool program_insert_or_replace(Program *p, uint16_t line_no, const char *source);

// Deletes line matching line_no. Returns true if line was found and deleted, false otherwise.
bool program_delete(Program *p, uint16_t line_no);

// Finds exact index of line_no. Returns -1 if not found.
ssize_t program_find_index(const Program *p, uint16_t line_no);

// Finds index of first line where line_no >= target_line. Returns -1 if none found.
ssize_t program_find_ge_index(const Program *p, uint16_t target_line);

// Returns pointer to ProgramLine at index, or NULL if out of bounds.
const ProgramLine *program_get_line(const Program *p, size_t index);

#include "error.h"

// Lists program lines to stream, starting from start_line (or 1 if 0).
void program_list(const Program *p, uint16_t start_line, FILE *out);

// Renumbers stored lines starting at start_line with step increment, patching branch targets.
// If any line would exceed 9999, aborts without modifying store and sets err->code = ERR_INTEGER_RANGE.
bool program_renumber(Program *p, uint16_t start_line, uint16_t step, BasicError *err, FILE *warn_out);

#endif // ZXBASIC_PROGRAM_H
