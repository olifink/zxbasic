#ifndef ZXBASIC_RUNTIME_H
#define ZXBASIC_RUNTIME_H

#include <stddef.h>
#include <stdint.h>
#include <stdbool.h>
#include <stdio.h>
#include <signal.h>
#include "program.h"
#include "symtab.h"
#include "error.h"

extern volatile sig_atomic_t g_interrupted;
void setup_signal_handlers(void);

#define MAX_CALL_STACK 256
#define MAX_FOR_STACK 64

typedef struct {
    size_t line_idx;
    int stmt_idx;
    size_t char_offset;
} CallFrame;

typedef struct {
    char var_name[64];
    double limit;
    double step;
    size_t line_idx;
    int stmt_idx;
    size_t char_offset;
} ForFrame;

typedef struct {
    Program program;
    SymTab symtab;

    // Execution state
    bool is_running;
    bool stop_requested;
    size_t cur_line_idx;
    int cur_stmt_idx;
    size_t cur_offset;

    // Jumps
    bool jump_requested;
    size_t jump_line_idx;
    int jump_stmt_idx;
    size_t jump_offset;

    // Call stack (GOSUB / RETURN)
    CallFrame call_stack[MAX_CALL_STACK];
    size_t call_sp;

    // Loop stack (FOR / NEXT)
    ForFrame for_stack[MAX_FOR_STACK];
    size_t for_sp;

    // DATA reading state
    size_t data_line_idx;
    size_t data_offset;
    bool data_initialized;

    // Diagnostic error
    BasicError last_error;

    // Interactive features (AUTO and EDIT)
    bool auto_mode;
    uint16_t auto_current_line;
    uint16_t auto_step;
    char *edit_prefill_buffer;

    // Console output tracking
    int print_col;
    FILE *out;
    FILE *in;
} Runtime;

void runtime_init(Runtime *rt);
void runtime_clear(Runtime *rt);
void runtime_free(Runtime *rt);

// Resets runtime state for RUN
void runtime_reset_for_run(Runtime *rt, uint16_t start_line);

// Executes a single line containing one or more ':' separated statements
// If line_no == -1, executed in direct mode.
void runtime_execute_line(Runtime *rt, const char *line_text, int32_t line_no);

// Runs the stored program starting at start_line (or 0 for beginning)
void runtime_run(Runtime *rt, uint16_t start_line);

// Process a single REPL or script input line (storing numbered lines, executing direct lines)
void runtime_process_input(Runtime *rt, const char *raw_line, bool interactive);

// Persistence
bool runtime_save(Runtime *rt, const char *path);
bool runtime_load(Runtime *rt, const char *path);

#endif // ZXBASIC_RUNTIME_H
