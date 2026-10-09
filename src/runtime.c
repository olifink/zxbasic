#define _POSIX_C_SOURCE 200809L

#include "runtime.h"
#include "parser.h"
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <ctype.h>

#include "normalize.h"

void runtime_init(Runtime *rt) {
    memset(rt, 0, sizeof(*rt));
    program_init(&rt->program);
    symtab_init(&rt->symtab);
    rt->out = stdout;
    rt->in = stdin;
    rt->print_col = 0;
    rt->last_error.code = ERR_OK;
    rt->last_error.line_no = -1;
    rt->last_error.stmt_index = 0;
    rt->auto_mode = false;
    rt->auto_current_line = 10;
    rt->auto_step = 10;
    rt->edit_prefill_buffer = NULL;
}

void runtime_clear(Runtime *rt) {
    if (!rt) return;
    symtab_clear(&rt->symtab);
    rt->is_running = false;
    rt->stop_requested = false;
    rt->jump_requested = false;
    rt->call_sp = 0;
    rt->for_sp = 0;
    rt->data_line_idx = 0;
    rt->data_offset = 0;
    rt->data_initialized = false;
    rt->print_col = 0;
    rt->last_error.code = ERR_OK;
    rt->last_error.line_no = -1;
    rt->last_error.stmt_index = 0;
    rt->auto_mode = false;
    free(rt->edit_prefill_buffer);
    rt->edit_prefill_buffer = NULL;
}

void runtime_free(Runtime *rt) {
    if (!rt) return;
    program_free(&rt->program);
    symtab_free(&rt->symtab);
    free(rt->edit_prefill_buffer);
    memset(rt, 0, sizeof(*rt));
}

void runtime_reset_for_run(Runtime *rt, uint16_t start_line) {
    symtab_clear(&rt->symtab);
    rt->is_running = true;
    rt->stop_requested = false;
    rt->jump_requested = false;
    rt->call_sp = 0;
    rt->for_sp = 0;

    ssize_t idx = 0;
    if (start_line > 0) {
        idx = program_find_ge_index(&rt->program, start_line);
    }
    rt->data_line_idx = (idx >= 0) ? (size_t)idx : 0;
    rt->data_offset = 0;
    rt->data_initialized = true;

    rt->cur_line_idx = (idx >= 0) ? (size_t)idx : 0;
    rt->cur_stmt_idx = 1;
    rt->cur_offset = 0;
    rt->print_col = 0;
    rt->last_error.code = ERR_OK;
    rt->last_error.line_no = -1;
    rt->last_error.stmt_index = 0;
}

void runtime_execute_line(Runtime *rt, const char *line_text, int32_t line_no) {
    if (!rt || !line_text) return;

    Lexer l;
    lexer_init(&l, line_text);
    bool skip_line = false;
    rt->cur_stmt_idx = 1;

    while (l.current.type != TOKEN_EOF && !skip_line) {
        rt->last_error.line_no = line_no;
        rt->last_error.stmt_index = rt->cur_stmt_idx;
        rt->jump_requested = false;

        size_t next_offset = l.cursor;
        if (!parser_execute_statement(&l, rt, next_offset, &skip_line)) {
            break;
        }

        if (rt->jump_requested || rt->stop_requested) {
            break;
        }

        if (skip_line) {
            break;
        }

        if (l.current.type == TOKEN_COLON) {
            lexer_next(&l);
            rt->cur_stmt_idx++;
        } else if (l.current.type != TOKEN_EOF) {
            rt->last_error.code = ERR_NONSENSE;
            break;
        }
    }

    token_free(&l.current);
}

void runtime_run(Runtime *rt, uint16_t start_line) {
    if (!rt || rt->program.count == 0) return;

    runtime_reset_for_run(rt, start_line);

    if (start_line > 0) {
        ssize_t idx = program_find_index(&rt->program, start_line);
        if (idx < 0) {
            rt->last_error.code = ERR_INTEGER_RANGE;
            rt->last_error.line_no = start_line;
            rt->last_error.stmt_index = 1;
            char err_buf[128];
            error_format(&rt->last_error, err_buf, sizeof(err_buf));
            fprintf(rt->out, "\n%s\n", err_buf);
            return;
        }
        rt->cur_line_idx = (size_t)idx;
    } else {
        rt->cur_line_idx = 0;
    }

    while (rt->is_running && rt->cur_line_idx < rt->program.count) {
        const ProgramLine *line = &rt->program.lines[rt->cur_line_idx];
        rt->last_error.line_no = (int32_t)line->line_no;
        rt->last_error.stmt_index = rt->cur_stmt_idx;

        const char *src = line->source + rt->cur_offset;
        rt->cur_offset = 0;

        Lexer l;
        lexer_init(&l, src);
        bool skip_line = false;
        bool jumped = false;

        while (l.current.type != TOKEN_EOF && !skip_line && rt->is_running) {
            rt->last_error.stmt_index = rt->cur_stmt_idx;
            rt->jump_requested = false;

            size_t next_offset = l.cursor;
            if (!parser_execute_statement(&l, rt, next_offset, &skip_line)) {
                if (rt->last_error.code != ERR_OK) {
                    char err_buf[128];
                    error_format(&rt->last_error, err_buf, sizeof(err_buf));
                    fprintf(rt->out, "\n%s\n", err_buf);
                    rt->is_running = false;
                    token_free(&l.current);
                    return;
                }
                if (rt->stop_requested) {
                    char err_buf[128];
                    error_format(&rt->last_error, err_buf, sizeof(err_buf));
                    fprintf(rt->out, "\n%s\n", err_buf);
                    rt->is_running = false;
                    token_free(&l.current);
                    return;
                }
            }

            if (rt->jump_requested) {
                rt->cur_line_idx = rt->jump_line_idx;
                rt->cur_stmt_idx = rt->jump_stmt_idx;
                rt->cur_offset = rt->jump_offset;
                rt->jump_requested = false;
                jumped = true;
                break;
            }

            if (skip_line) {
                break;
            }

            if (l.current.type == TOKEN_COLON) {
                lexer_next(&l);
                rt->cur_stmt_idx++;
            } else if (l.current.type != TOKEN_EOF) {
                rt->last_error.code = ERR_NONSENSE;
                char err_buf[128];
                error_format(&rt->last_error, err_buf, sizeof(err_buf));
                fprintf(rt->out, "\n%s\n", err_buf);
                rt->is_running = false;
                token_free(&l.current);
                return;
            }
        }
        token_free(&l.current);

        if (!jumped) {
            rt->cur_line_idx++;
            rt->cur_stmt_idx = 1;
            rt->cur_offset = 0;
        }
    }

    if (rt->is_running && !rt->stop_requested && rt->last_error.code == ERR_OK) {
        char err_buf[128];
        rt->last_error.code = ERR_OK;
        rt->last_error.line_no = (rt->program.count > 0) ? (int32_t)rt->program.lines[rt->program.count - 1].line_no : 0;
        rt->last_error.stmt_index = 1;
        error_format(&rt->last_error, err_buf, sizeof(err_buf));
        fprintf(rt->out, "\n%s\n", err_buf);
    }
    rt->is_running = false;
}

void runtime_process_input(Runtime *rt, const char *raw_line, bool interactive) {
    if (!rt || !raw_line) return;

    // Skip leading whitespace
    const char *p = raw_line;
    while (*p && isspace((unsigned char)*p)) p++;
    if (*p == '\0') return;

    // Check if line starts with line number
    if (isdigit((unsigned char)*p)) {
        char *endptr = NULL;
        long line_no = strtol(p, &endptr, 10);
        if (line_no < MIN_LINE_NO || line_no > MAX_LINE_NO) {
            rt->last_error.code = ERR_INTEGER_RANGE;
            rt->last_error.line_no = -1;
            char buf[128];
            error_format(&rt->last_error, buf, sizeof(buf));
            fprintf(rt->out, "%s\n", buf);
            return;
        }

        const char *body = endptr;
        while (*body && isspace((unsigned char)*body)) body++;

        if (*body == '\0') {
            // Delete line
            program_delete(&rt->program, (uint16_t)line_no);
        } else {
            // Pre-storage filter passes:
            // 1. Keyword Auto-Capitalization
            char *normalized = normalize_keywords(body);
            if (!normalized) return;

            size_t clen = strlen(normalized);
            while (clen > 0 && (normalized[clen - 1] == '\r' || normalized[clen - 1] == '\n')) {
                normalized[--clen] = '\0';
            }

            // 2. Syntax Verification
            size_t err_col = 0;
            if (!syntax_validate_line(normalized, &err_col)) {
                // Reject line, do not touch existing program store
                char err_buf[128];
                snprintf(err_buf, sizeof(err_buf), "C Nonsense in BASIC, %u:%u", (unsigned int)line_no, (unsigned int)err_col);
                fprintf(rt->out, "%s\n", err_buf);
                free(normalized);
                return;
            }

            // Ingest validated line into program store
            program_insert_or_replace(&rt->program, (uint16_t)line_no, normalized);
            free(normalized);
        }
    } else {
        // Immediate mode execution
        rt->last_error.code = ERR_OK;
        rt->last_error.line_no = -1;
        rt->last_error.stmt_index = 0;

        char *copy = strdup(p);
        size_t clen = strlen(copy);
        while (clen > 0 && (copy[clen - 1] == '\r' || copy[clen - 1] == '\n')) {
            copy[--clen] = '\0';
        }

        runtime_execute_line(rt, copy, -1);
        free(copy);

        if (rt->last_error.line_no < 0) {
            if (rt->last_error.code != ERR_OK || interactive) {
                char buf[128];
                error_format(&rt->last_error, buf, sizeof(buf));
                fprintf(rt->out, "%s\n", buf);
            }
        }
    }
}

bool runtime_save(Runtime *rt, const char *path) {
    if (!rt || !path) return false;
    FILE *f = fopen(path, "w");
    if (!f) {
        rt->last_error.code = ERR_NONSENSE;
        return false;
    }

    program_list(&rt->program, 0, f);
    fclose(f);
    return true;
}

bool runtime_load(Runtime *rt, const char *path) {
    if (!rt || !path) return false;
    FILE *f = fopen(path, "r");
    if (!f) {
        rt->last_error.code = ERR_NONSENSE;
        return false;
    }

    program_clear(&rt->program);
    runtime_clear(rt);

    char line_buf[2048];
    while (fgets(line_buf, sizeof(line_buf), f)) {
        runtime_process_input(rt, line_buf, false);
    }

    fclose(f);
    return true;
}
