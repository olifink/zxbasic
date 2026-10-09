#ifndef ZXBASIC_PARSER_H
#define ZXBASIC_PARSER_H

#include <stdbool.h>
#include "lexer.h"
#include "runtime.h"

// Parses and executes a single statement from the lexer.
// Sets skip_rest_of_line to true if IF condition is false.
// Returns true to continue execution, false if an error occurred or execution halted.
bool parser_execute_statement(Lexer *l, Runtime *rt, size_t next_stmt_offset, bool *skip_rest_of_line);

// Helper for READ: fetches the next value from the program's DATA statements
bool runtime_read_next_data(Runtime *rt, Value *out_val);

#endif // ZXBASIC_PARSER_H
