#define _POSIX_C_SOURCE 200809L

#include "runtime.h"
#include "parser.h"
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <ctype.h>
#include <math.h>

#include "normalize.h"

volatile sig_atomic_t g_interrupted = 0;

static void sigint_handler(int sig) {
    (void)sig;
    g_interrupted = 1;
}

void setup_signal_handlers(void) {
    struct sigaction sa;
    memset(&sa, 0, sizeof(sa));
    sa.sa_handler = sigint_handler;
    sigemptyset(&sa.sa_mask);
    sa.sa_flags = 0;
    sigaction(SIGINT, &sa, NULL);
}

void runtime_init(Runtime *rt) {
    memset(rt, 0, sizeof(*rt));
    program_init(&rt->program);
    symtab_init(&rt->symtab);
    sysvar_table_init(&rt->sysvars);
    rt->symtab.sysvars = &rt->sysvars;
    console_init(rt);
    rt->out = stdout;
    rt->in = stdin;
    rt->print_col = 0;
    rt->last_error.code = ERR_OK;
    rt->last_error.custom_msg = NULL;
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
    rt->symtab.sysvars = &rt->sysvars;
    rt->is_running = false;
    rt->stop_requested = false;
    rt->jump_requested = false;
    rt->call_sp = 0;
    rt->for_sp = 0;
    rt->data_line_idx = 0;
    rt->data_offset = 0;
    rt->data_initialized = false;
    rt->print_col = 0;
    sysvar_set_int(&rt->sysvars, "ATTR_T_INK", ATTR_INACTIVE);
    sysvar_set_int(&rt->sysvars, "ATTR_T_PAPER", ATTR_INACTIVE);
    sysvar_set_int(&rt->sysvars, "ATTR_T_BRIGHT", ATTR_INACTIVE);
    sysvar_set_int(&rt->sysvars, "ATTR_T_INVERSE", ATTR_INACTIVE);
    sysvar_set_int(&rt->sysvars, "S_POSN_ROW", 0);
    sysvar_set_int(&rt->sysvars, "S_POSN_COL", 0);
    console_reset_term_attrs(rt);
    rt->last_error.code = ERR_OK;
    rt->last_error.custom_msg = NULL;
    rt->last_error.line_no = -1;
    rt->last_error.stmt_index = 0;
    rt->auto_mode = false;
    free(rt->edit_prefill_buffer);
    rt->edit_prefill_buffer = NULL;

    // Reset debug and continuation state
    rt->can_continue = false;
    rt->cont_line_idx = 0;
    rt->cont_stmt_idx = 1;
    rt->temp_stop_line = 0;
    rt->breakpoint_count = 0;
}

void runtime_free(Runtime *rt) {
    if (!rt) return;
    program_free(&rt->program);
    symtab_free(&rt->symtab);
    sysvar_table_free(&rt->sysvars);
    free(rt->edit_prefill_buffer);
    memset(rt, 0, sizeof(*rt));
}

void runtime_reset_for_run(Runtime *rt, uint16_t start_line) {
    symtab_clear(&rt->symtab);
    rt->symtab.sysvars = &rt->sysvars;
    rt->is_running = true;
    rt->stop_requested = false;
    rt->jump_requested = false;
    rt->call_sp = 0;
    rt->for_sp = 0;
    sysvar_set_int(&rt->sysvars, "ATTR_T_INK", ATTR_INACTIVE);
    sysvar_set_int(&rt->sysvars, "ATTR_T_PAPER", ATTR_INACTIVE);
    sysvar_set_int(&rt->sysvars, "ATTR_T_BRIGHT", ATTR_INACTIVE);
    sysvar_set_int(&rt->sysvars, "ATTR_T_INVERSE", ATTR_INACTIVE);
    sysvar_set_int(&rt->sysvars, "S_POSN_ROW", 0);
    sysvar_set_int(&rt->sysvars, "S_POSN_COL", 0);

    // Reset continuation and temporary stop line (breakpoints persist across RUNs)
    rt->can_continue = false;
    rt->cont_line_idx = 0;
    rt->cont_stmt_idx = 1;
    rt->temp_stop_line = 0;

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
    rt->last_error.custom_msg = NULL;
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

        if (g_interrupted) {
            g_interrupted = 0;
            rt->stop_requested = true;
            rt->last_error.code = ERR_STOP;
            rt->last_error.custom_msg = "BREAK into program";
            break;
        }

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

bool runtime_add_breakpoint(Runtime *rt, uint16_t line_no) {
    if (!rt) return false;
    for (size_t i = 0; i < rt->breakpoint_count; i++) {
        if (rt->breakpoints[i] == line_no) return true;
    }
    if (rt->breakpoint_count < MAX_BREAKPOINTS) {
        rt->breakpoints[rt->breakpoint_count++] = line_no;
        return true;
    }
    return false;
}

bool runtime_has_breakpoint(const Runtime *rt, uint16_t line_no) {
    if (!rt) return false;
    for (size_t i = 0; i < rt->breakpoint_count; i++) {
        if (rt->breakpoints[i] == line_no) return true;
    }
    return false;
}

void runtime_clear_breakpoints(Runtime *rt) {
    if (!rt) return;
    rt->breakpoint_count = 0;
    rt->temp_stop_line = 0;
}

void runtime_vars(Runtime *rt) {
    if (!rt || !rt->out) return;
    SymTab *st = &rt->symtab;
    size_t total = st->num_vars_count + st->str_vars_count + st->num_arrays_count + st->str_arrays_count;
    if (total == 0) {
        fprintf(rt->out, "(no variables defined)\n");
        fflush(rt->out);
        return;
    }

    fprintf(rt->out, "%-10s %-10s %-19s %s\n", "Variable", "Type", "Dimensions / Size", "Value Preview");
    fprintf(rt->out, "-----------------------------------------------------------------\n");

    // 1. Numeric scalar variables
    for (size_t i = 0; i < st->num_vars_count; i++) {
        NumVar *v = &st->num_vars[i];
        char val_buf[64];
        if (v->val == floor(v->val) && v->val >= -2147483648.0 && v->val <= 2147483647.0) {
            snprintf(val_buf, sizeof(val_buf), "%ld", (long)v->val);
        } else {
            snprintf(val_buf, sizeof(val_buf), "%.6g", v->val);
        }
        fprintf(rt->out, "%-10s %-10s %-19s %s\n", v->name, "NUMBER", "scalar", val_buf);
    }

    // 2. String scalar variables
    for (size_t i = 0; i < st->str_vars_count; i++) {
        StrVar *v = &st->str_vars[i];
        char dim_buf[32];
        snprintf(dim_buf, sizeof(dim_buf), "%zu byte%s", v->len, (v->len == 1) ? "" : "s");

        char prev_buf[64];
        if (v->len > 27) {
            snprintf(prev_buf, sizeof(prev_buf), "\"%.27s...\"", v->val ? v->val : "");
        } else {
            snprintf(prev_buf, sizeof(prev_buf), "\"%s\"", v->val ? v->val : "");
        }
        fprintf(rt->out, "%-10s %-10s %-19s %s\n", v->name, "STRING", dim_buf, prev_buf);
    }

    // 3. Numeric arrays
    for (size_t i = 0; i < st->num_arrays_count; i++) {
        NumArray *a = &st->num_arrays[i];
        char dim_buf[64];
        int offset = snprintf(dim_buf, sizeof(dim_buf), "(");
        for (size_t d = 0; d < a->ndims; d++) {
            offset += snprintf(dim_buf + offset, sizeof(dim_buf) - offset, "%zu%s",
                               a->dims[d], (d + 1 < a->ndims) ? ", " : ")");
        }

        char prev_buf[64];
        if (a->total_elements == 0 || !a->data) {
            snprintf(prev_buf, sizeof(prev_buf), "[]");
        } else if (a->ndims == 1) {
            if (a->total_elements <= 3) {
                int poff = snprintf(prev_buf, sizeof(prev_buf), "[");
                for (size_t k = 0; k < a->total_elements; k++) {
                    poff += snprintf(prev_buf + poff, sizeof(prev_buf) - poff, "%.4g%s",
                                     a->data[k], (k + 1 < a->total_elements) ? ", " : "]");
                }
            } else {
                snprintf(prev_buf, sizeof(prev_buf), "[%.4g, %.4g...]", a->data[0], a->data[1]);
            }
        } else {
            snprintf(prev_buf, sizeof(prev_buf), "[[%.4g, ...], ...]", a->data[0]);
        }
        fprintf(rt->out, "%-10s %-10s %-19s %s\n", a->name, "ARRAY(N)", dim_buf, prev_buf);
    }

    // 4. String arrays
    for (size_t i = 0; i < st->str_arrays_count; i++) {
        StrArray *a = &st->str_arrays[i];
        char dim_buf[64];
        int offset = snprintf(dim_buf, sizeof(dim_buf), "(");
        for (size_t d = 0; d < a->ndims; d++) {
            offset += snprintf(dim_buf + offset, sizeof(dim_buf) - offset, "%zu%s",
                               a->dims[d], (d + 1 < a->ndims) ? ", " : ")");
        }

        char prev_buf[64];
        if (a->total_elements == 0 || !a->data) {
            snprintf(prev_buf, sizeof(prev_buf), "[]");
        } else {
            size_t str_len = a->dims[a->ndims - 1];
            if (str_len > 24) str_len = 24;
            snprintf(prev_buf, sizeof(prev_buf), "[\"%.*s%s\", ...]",
                     (int)str_len, a->data, (a->dims[a->ndims - 1] > 24) ? "..." : "");
        }
        fprintf(rt->out, "%-10s %-10s %-19s %s\n", a->name, "ARRAY(S)", dim_buf, prev_buf);
    }

    fflush(rt->out);
}

static void runtime_run_loop(Runtime *rt, bool first_line_skip_bp) {
    bool skip_bp_check = first_line_skip_bp;

    while (rt->is_running && rt->cur_line_idx < rt->program.count) {
        const ProgramLine *line = &rt->program.lines[rt->cur_line_idx];
        rt->last_error.line_no = (int32_t)line->line_no;
        rt->last_error.stmt_index = rt->cur_stmt_idx;

        if (!skip_bp_check && (runtime_has_breakpoint(rt, line->line_no) ||
            (rt->temp_stop_line != 0 && line->line_no == rt->temp_stop_line))) {
            rt->stop_requested = true;
            rt->last_error.code = ERR_STOP;
            rt->last_error.custom_msg = "Breakpoint reached";
            rt->last_error.line_no = (int32_t)line->line_no;
            rt->last_error.stmt_index = 1;
            rt->can_continue = true;
            rt->cont_line_idx = rt->cur_line_idx;
            rt->cont_stmt_idx = 1;
            rt->temp_stop_line = 0;
            char err_buf[128];
            error_format(&rt->last_error, err_buf, sizeof(err_buf));
            fprintf(rt->out, "\n%s\n", err_buf);
            rt->is_running = false;
            return;
        }
        skip_bp_check = false;

        if (g_interrupted) {
            g_interrupted = 0;
            rt->stop_requested = true;
            rt->last_error.code = ERR_STOP;
            rt->last_error.custom_msg = "BREAK into program";
            rt->can_continue = true;
            rt->cont_line_idx = rt->cur_line_idx;
            rt->cont_stmt_idx = rt->cur_stmt_idx;
            char err_buf[128];
            error_format(&rt->last_error, err_buf, sizeof(err_buf));
            fprintf(rt->out, "\n%s\n", err_buf);
            rt->is_running = false;
            return;
        }

        const char *src = line->source + rt->cur_offset;
        rt->cur_offset = 0;

        Lexer l;
        lexer_init(&l, src);
        bool skip_line = false;
        bool jumped = false;

        // Fast-forward to rt->cur_stmt_idx if > 1
        if (rt->cur_stmt_idx > 1) {
            int target_stmt = rt->cur_stmt_idx;
            int cur_s = 1;
            while (cur_s < target_stmt && l.current.type != TOKEN_EOF) {
                if (l.current.type == TOKEN_COLON) {
                    cur_s++;
                    lexer_next(&l);
                } else {
                    lexer_next(&l);
                }
            }
        }

        while (l.current.type != TOKEN_EOF && !skip_line && rt->is_running) {
            rt->last_error.line_no = (int32_t)line->line_no;
            rt->last_error.stmt_index = rt->cur_stmt_idx;
            rt->jump_requested = false;

            if (g_interrupted) {
                g_interrupted = 0;
                rt->stop_requested = true;
                rt->last_error.code = ERR_STOP;
                rt->last_error.custom_msg = "BREAK into program";
                rt->can_continue = true;
                rt->cont_line_idx = rt->cur_line_idx;
                rt->cont_stmt_idx = rt->cur_stmt_idx;
                char err_buf[128];
                error_format(&rt->last_error, err_buf, sizeof(err_buf));
                fprintf(rt->out, "\n%s\n", err_buf);
                rt->is_running = false;
                token_free(&l.current);
                return;
            }

            size_t next_offset = l.cursor;
            if (!parser_execute_statement(&l, rt, next_offset, &skip_line)) {
                if (rt->last_error.code != ERR_OK && !rt->stop_requested) {
                    rt->can_continue = false;
                    char err_buf[128];
                    error_format(&rt->last_error, err_buf, sizeof(err_buf));
                    fprintf(rt->out, "\n%s\n", err_buf);
                    rt->is_running = false;
                    token_free(&l.current);
                    return;
                }
                if (rt->stop_requested) {
                    if (l.current.type == TOKEN_COLON) {
                        rt->can_continue = true;
                        rt->cont_line_idx = rt->cur_line_idx;
                        rt->cont_stmt_idx = rt->cur_stmt_idx + 1;
                    } else {
                        if (rt->cur_line_idx + 1 < rt->program.count) {
                            rt->can_continue = true;
                            rt->cont_line_idx = rt->cur_line_idx + 1;
                            rt->cont_stmt_idx = 1;
                        } else {
                            rt->can_continue = false;
                        }
                    }
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
    rt->can_continue = false;
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

    runtime_run_loop(rt, false);
}

bool runtime_continue(Runtime *rt, uint16_t temp_stop_line) {
    if (!rt) return false;

    if (rt->is_running) {
        if (temp_stop_line > 0) {
            if (program_find_index(&rt->program, temp_stop_line) < 0) {
                rt->last_error.code = ERR_INTEGER_RANGE;
                return false;
            }
            rt->temp_stop_line = temp_stop_line;
        }
        return true;
    }

    if (!rt->can_continue) {
        rt->last_error.code = ERR_NONSENSE;
        rt->last_error.custom_msg = NULL;
        return false;
    }

    if (temp_stop_line > 0) {
        if (program_find_index(&rt->program, temp_stop_line) < 0) {
            rt->last_error.code = ERR_INTEGER_RANGE;
            return false;
        }
        rt->temp_stop_line = temp_stop_line;
    }

    if (rt->cont_line_idx >= rt->program.count) {
        rt->can_continue = false;
        return true;
    }

    rt->cur_line_idx = rt->cont_line_idx;
    rt->cur_stmt_idx = rt->cont_stmt_idx;
    rt->cur_offset = 0;
    rt->is_running = true;
    rt->stop_requested = false;
    rt->jump_requested = false;
    rt->last_error.code = ERR_OK;
    rt->last_error.custom_msg = NULL;
    rt->can_continue = false;

    runtime_run_loop(rt, (rt->cur_stmt_idx == 1));
    return (rt->last_error.code == ERR_OK || rt->last_error.code == ERR_STOP);
}

bool runtime_process_input(Runtime *rt, const char *raw_line, bool interactive) {
    if (!rt || !raw_line) return false;

    // Skip leading whitespace
    const char *p = raw_line;
    while (*p && isspace((unsigned char)*p)) p++;
    if (*p == '\0') return true;

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
            return false;
        }

        const char *body = endptr;
        while (*body && isspace((unsigned char)*body)) body++;

        if (*body == '\0') {
            // Delete line
            program_delete(&rt->program, (uint16_t)line_no);
            rt->last_error.code = ERR_OK;
            return true;
        } else {
            // Pre-storage filter passes:
            // 1. Keyword Auto-Capitalization
            char *normalized = normalize_keywords(body);
            if (!normalized) return false;

            size_t clen = strlen(normalized);
            while (clen > 0 && (normalized[clen - 1] == '\r' || normalized[clen - 1] == '\n')) {
                normalized[--clen] = '\0';
            }

            // 2. Syntax Verification
            size_t err_col = 0;
            if (!syntax_validate_line(normalized, &err_col)) {
                // Reject line, do not touch existing program store
                rt->last_error.code = ERR_NONSENSE;
                rt->last_error.line_no = (int32_t)line_no;
                rt->last_error.stmt_index = (int32_t)err_col;
                char err_buf[128];
                snprintf(err_buf, sizeof(err_buf), "C Nonsense in BASIC, %u:%u", (unsigned int)line_no, (unsigned int)err_col);
                fprintf(rt->out, "%s\n", err_buf);
                free(normalized);
                return false;
            }

            // Ingest validated line into program store
            program_insert_or_replace(&rt->program, (uint16_t)line_no, normalized);
            free(normalized);
            rt->last_error.code = ERR_OK;
            return true;
        }
    } else {
        // Immediate mode execution
        rt->last_error.code = ERR_OK;
        rt->last_error.custom_msg = NULL;
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
        return (rt->last_error.code == ERR_OK);
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
