#ifndef ZXBASIC_NORMALIZE_H
#define ZXBASIC_NORMALIZE_H

#include <stddef.h>
#include <stdbool.h>
#include <stdint.h>

// Returns a newly allocated string with keywords and built-in functions
// converted to canonical uppercase. Preserves exact casing inside string literals,
// REM comments, and variable identifiers.
char *normalize_keywords(const char *source);

// Validates the syntax of a program line.
// Returns true if syntactically valid, or false if syntax error.
// On error, *err_col is set to the 1-based character position of the syntax error.
bool syntax_validate_line(const char *source, size_t *err_col);

#endif // ZXBASIC_NORMALIZE_H
