#define _POSIX_C_SOURCE 200809L

#include "runtime.h"
#include "parser.h"
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <strings.h>
#include <ctype.h>
#include <math.h>
#include <dirent.h>
#include <sys/stat.h>
#include <unistd.h>

#include <errno.h>

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
    rt->symtab.program = &rt->program;
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
    runtime_channels_init(rt);
}

void runtime_clear(Runtime *rt) {
    if (!rt) return;
    runtime_channels_close_all(rt);
    symtab_clear(&rt->symtab);
    rt->symtab.sysvars = &rt->sysvars;
    rt->symtab.program = &rt->program;
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
    rt->auto_ran = false;
}

void runtime_free(Runtime *rt) {
    if (!rt) return;
    runtime_channels_close_all(rt);
    program_free(&rt->program);
    symtab_free(&rt->symtab);
    sysvar_table_free(&rt->sysvars);
    free(rt->edit_prefill_buffer);
    memset(rt, 0, sizeof(*rt));
}

void runtime_reset_for_run(Runtime *rt, uint16_t start_line) {
    symtab_clear(&rt->symtab);
    rt->symtab.sysvars = &rt->sysvars;
    rt->symtab.program = &rt->program;
    rt->is_running = true;
    rt->stop_requested = false;
    rt->jump_requested = false;
    rt->call_sp = 0;
    rt->for_sp = 0;
    rt->auto_ran = true;
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

void runtime_verify(Runtime *rt) {
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

bool runtime_goto(Runtime *rt, uint16_t target_line) {
    if (!rt || rt->program.count == 0) return false;

    ssize_t idx = program_find_index(&rt->program, target_line);
    if (idx < 0) {
        rt->last_error.code = ERR_INTEGER_RANGE;
        rt->last_error.line_no = target_line;
        rt->last_error.stmt_index = 1;
        return false;
    }

    rt->is_running = true;
    rt->stop_requested = false;
    rt->jump_requested = false;
    rt->call_sp = 0;
    rt->for_sp = 0;
    rt->can_continue = false;
    rt->temp_stop_line = 0;

    sysvar_set_int(&rt->sysvars, "ATTR_T_INK", ATTR_INACTIVE);
    sysvar_set_int(&rt->sysvars, "ATTR_T_PAPER", ATTR_INACTIVE);
    sysvar_set_int(&rt->sysvars, "ATTR_T_BRIGHT", ATTR_INACTIVE);
    sysvar_set_int(&rt->sysvars, "ATTR_T_INVERSE", ATTR_INACTIVE);
    sysvar_set_int(&rt->sysvars, "S_POSN_ROW", 0);
    sysvar_set_int(&rt->sysvars, "S_POSN_COL", 0);

    if (!rt->data_initialized) {
        rt->data_line_idx = (size_t)idx;
        rt->data_offset = 0;
        rt->data_initialized = true;
    }

    rt->cur_line_idx = (size_t)idx;
    rt->cur_stmt_idx = 1;
    rt->cur_offset = 0;
    rt->print_col = 0;
    rt->last_error.code = ERR_OK;
    rt->last_error.custom_msg = NULL;

    runtime_run_loop(rt, false);
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

bool runtime_save_ext(Runtime *rt, const char *path, bool has_line, uint16_t line_no) {
    if (!rt || !path) return false;
    FILE *f = fopen(path, "w");
    if (!f) {
        rt->last_error.code = ERR_NONSENSE;
        return false;
    }

    program_list(&rt->program, 0, f);
    if (has_line) {
        if (line_no == 0) {
            fprintf(f, "RUN\n");
        } else {
            fprintf(f, "RUN %u\n", (unsigned int)line_no);
        }
    }
    fclose(f);
    return true;
}

bool runtime_save(Runtime *rt, const char *path) {
    return runtime_save_ext(rt, path, false, 0);
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
    rt->auto_ran = false;

    char line_buf[2048];
    while (fgets(line_buf, sizeof(line_buf), f)) {
        runtime_process_input(rt, line_buf, false);
    }

    fclose(f);
    return true;
}

bool runtime_merge(Runtime *rt, const char *path) {
    if (!rt || !path) return false;
    FILE *f = fopen(path, "r");
    if (!f) {
        rt->last_error.code = ERR_NONSENSE;
        return false;
    }

    char line_buf[2048];
    while (fgets(line_buf, sizeof(line_buf), f)) {
        const char *p = line_buf;
        while (*p && isspace((unsigned char)*p)) p++;
        // Ingest only numbered lines; ignore unnumbered direct commands
        if (*p != '\0' && isdigit((unsigned char)*p)) {
            runtime_process_input(rt, line_buf, false);
        }
    }

    fclose(f);
    return true;
}

typedef struct {
    char name[256];
    bool is_dir;
    long long size;
} CatEntry;

static int compare_cat_entries(const void *a, const void *b) {
    const CatEntry *ea = (const CatEntry *)a;
    const CatEntry *eb = (const CatEntry *)b;
    return strcasecmp(ea->name, eb->name);
}

bool runtime_cat(Runtime *rt, const char *subpath) {
    if (!rt) return false;
    const char *dir_path = (subpath && subpath[0] != '\0') ? subpath : ".";

    DIR *d = opendir(dir_path);
    if (!d) {
        rt->last_error.code = ERR_NONSENSE;
        return false;
    }

    CatEntry *entries = NULL;
    size_t count = 0;
    size_t cap = 0;

    struct dirent *de;
    while ((de = readdir(d)) != NULL) {
        if (de->d_name[0] == '.') continue;

        if (count >= cap) {
            cap = (cap == 0) ? 16 : cap * 2;
            CatEntry *new_entries = realloc(entries, cap * sizeof(CatEntry));
            if (!new_entries) {
                free(entries);
                closedir(d);
                rt->last_error.code = ERR_OUT_OF_MEMORY;
                return false;
            }
            entries = new_entries;
        }

        strncpy(entries[count].name, de->d_name, sizeof(entries[count].name) - 1);
        entries[count].name[sizeof(entries[count].name) - 1] = '\0';

        char full_path[1024];
        if (strcmp(dir_path, ".") == 0) {
            snprintf(full_path, sizeof(full_path), "%s", de->d_name);
        } else {
            snprintf(full_path, sizeof(full_path), "%s/%s", dir_path, de->d_name);
        }

        struct stat st;
        if (stat(full_path, &st) == 0) {
            entries[count].is_dir = S_ISDIR(st.st_mode);
            entries[count].size = (long long)st.st_size;
        } else {
            entries[count].is_dir = false;
            entries[count].size = 0;
        }
        count++;
    }
    closedir(d);

    if (count > 1) {
        qsort(entries, count, sizeof(CatEntry), compare_cat_entries);
    }

    fprintf(rt->out, "Directory of %s:\n", dir_path);
    for (size_t i = 0; i < count; i++) {
        if (entries[i].is_dir) {
            fprintf(rt->out, "  [DIR]  %s/\n", entries[i].name);
        } else {
            fprintf(rt->out, "  [FILE] %-20s %lld bytes\n", entries[i].name, entries[i].size);
        }
    }
    free(entries);
    return true;
}

static bool remove_recursive_dir(const char *path) {
    DIR *d = opendir(path);
    if (!d) return false;
    struct dirent *entry;
    bool ok = true;
    while ((entry = readdir(d)) != NULL) {
        if (strcmp(entry->d_name, ".") == 0 || strcmp(entry->d_name, "..") == 0) {
            continue;
        }
        char subpath[1024];
        snprintf(subpath, sizeof(subpath), "%s/%s", path, entry->d_name);
        struct stat st;
        if (lstat(subpath, &st) != 0) {
            ok = false;
            break;
        }
        if (S_ISDIR(st.st_mode)) {
            if (!remove_recursive_dir(subpath)) {
                ok = false;
                break;
            }
        } else {
            if (unlink(subpath) != 0) {
                ok = false;
                break;
            }
        }
    }
    closedir(d);
    if (!ok) return false;
    return rmdir(path) == 0;
}

bool runtime_erase_recursive(Runtime *rt, const char *path) {
    if (!rt || !path || path[0] == '\0') {
        if (rt) rt->last_error.code = ERR_NONSENSE;
        return false;
    }
    struct stat st;
    if (lstat(path, &st) != 0) {
        rt->last_error.code = ERR_NONSENSE;
        return false;
    }
    if (S_ISDIR(st.st_mode)) {
        if (!remove_recursive_dir(path)) {
            rt->last_error.code = ERR_NONSENSE;
            return false;
        }
        return true;
    } else {
        if (unlink(path) != 0) {
            rt->last_error.code = ERR_NONSENSE;
            return false;
        }
        return true;
    }
}

bool runtime_erase_file(Runtime *rt, const char *path) {
    return runtime_erase_recursive(rt, path);
}

bool runtime_delete_file(Runtime *rt, const char *path) {
    return runtime_erase_file(rt, path);
}

void runtime_channels_init(Runtime *rt) {
    if (!rt) return;
    for (int i = 0; i < MAX_CHANNELS; i++) {
        rt->channels[i].fp = NULL;
        rt->channels[i].path = NULL;
        rt->channels[i].mode = CHANNEL_MODE_NONE;
        rt->channels[i].is_open = false;
    }
    // Stream 0: stdin / input
    rt->channels[0].fp = rt->in ? rt->in : stdin;
    rt->channels[0].mode = CHANNEL_MODE_SYSTEM;
    rt->channels[0].is_open = true;

    // Stream 1: stdin / input
    rt->channels[1].fp = rt->in ? rt->in : stdin;
    rt->channels[1].mode = CHANNEL_MODE_SYSTEM;
    rt->channels[1].is_open = true;

    // Stream 2: stdout / output
    rt->channels[2].fp = rt->out ? rt->out : stdout;
    rt->channels[2].mode = CHANNEL_MODE_SYSTEM;
    rt->channels[2].is_open = true;

    // Stream 3: stderr / printer
    rt->channels[3].fp = stderr;
    rt->channels[3].mode = CHANNEL_MODE_SYSTEM;
    rt->channels[3].is_open = true;
}

void runtime_channels_close_all(Runtime *rt) {
    if (!rt) return;
    for (int i = MIN_USER_CHANNEL; i <= MAX_USER_CHANNEL; i++) {
        if (rt->channels[i].is_open) {
            runtime_channel_close(rt, i);
        }
    }
}

bool runtime_channel_open(Runtime *rt, int channel_no, const char *path) {
    if (!rt || !path || path[0] == '\0') {
        if (rt) rt->last_error.code = ERR_NONSENSE;
        return false;
    }
    if (channel_no < MIN_USER_CHANNEL || channel_no > MAX_USER_CHANNEL) {
        rt->last_error.code = ERR_INTEGER_RANGE;
        return false;
    }
    if (rt->channels[channel_no].is_open) {
        runtime_channel_close(rt, channel_no);
    }
    if (access(path, F_OK) == 0) {
        // Exists: open for reading
        struct stat st;
        if (stat(path, &st) == 0 && S_ISDIR(st.st_mode)) {
            rt->last_error.code = ERR_NONSENSE;
            return false;
        }
        FILE *fp = fopen(path, "r");
        if (!fp) {
            rt->last_error.code = ERR_NONSENSE;
            return false;
        }
        rt->channels[channel_no].fp = fp;
        rt->channels[channel_no].mode = CHANNEL_MODE_READ;
        rt->channels[channel_no].path = strdup(path);
        rt->channels[channel_no].is_open = true;
        return true;
    } else {
        // Does not exist: open for writing
        FILE *fp = fopen(path, "w");
        if (!fp) {
            rt->last_error.code = ERR_NONSENSE;
            return false;
        }
        rt->channels[channel_no].fp = fp;
        rt->channels[channel_no].mode = CHANNEL_MODE_WRITE;
        rt->channels[channel_no].path = strdup(path);
        rt->channels[channel_no].is_open = true;
        return true;
    }
}

bool runtime_channel_close(Runtime *rt, int channel_no) {
    if (!rt) return false;
    if (channel_no < MIN_USER_CHANNEL || channel_no > MAX_USER_CHANNEL) {
        if (channel_no >= 0 && channel_no < MIN_USER_CHANNEL) {
            rt->last_error.code = ERR_NONSENSE;
        } else {
            rt->last_error.code = ERR_INTEGER_RANGE;
        }
        return false;
    }
    if (rt->channels[channel_no].is_open) {
        if (rt->channels[channel_no].fp) {
            fflush(rt->channels[channel_no].fp);
            fclose(rt->channels[channel_no].fp);
            rt->channels[channel_no].fp = NULL;
        }
        if (rt->channels[channel_no].path) {
            free(rt->channels[channel_no].path);
            rt->channels[channel_no].path = NULL;
        }
        rt->channels[channel_no].is_open = false;
        rt->channels[channel_no].mode = CHANNEL_MODE_NONE;
    }
    return true;
}

bool runtime_channel_move(Runtime *rt, int src_ch, int dest_ch) {
    if (!rt) return false;
    if (src_ch < 0 || src_ch >= MAX_CHANNELS || dest_ch < 0 || dest_ch >= MAX_CHANNELS) {
        rt->last_error.code = ERR_INTEGER_RANGE;
        return false;
    }
    Channel *s = &rt->channels[src_ch];
    Channel *d = &rt->channels[dest_ch];
    if (!s->is_open || !d->is_open) {
        rt->last_error.code = ERR_NONSENSE;
        return false;
    }
    if (s->mode == CHANNEL_MODE_WRITE || d->mode == CHANNEL_MODE_READ) {
        rt->last_error.code = ERR_NONSENSE;
        return false;
    }
    FILE *src_fp = s->fp ? s->fp : (src_ch == 0 || src_ch == 1 ? (rt->in ? rt->in : stdin) : NULL);
    FILE *dest_fp = d->fp ? d->fp : (dest_ch == 2 ? rt->out : (dest_ch == 3 ? stderr : NULL));
    if (!src_fp || !dest_fp) {
        rt->last_error.code = ERR_NONSENSE;
        return false;
    }
    if (src_ch == dest_ch) {
        return true;
    }
    char buf[4096];
    size_t n;
    while ((n = fread(buf, 1, sizeof(buf), src_fp)) > 0) {
        if (fwrite(buf, 1, n, dest_fp) != n) {
            rt->last_error.code = ERR_NONSENSE;
            return false;
        }
    }
    fflush(dest_fp);
    // Channels remain open!
    return true;
}

static bool make_dirs_p(const char *path) {
    char tmp[1024];
    size_t len = strlen(path);
    if (len >= sizeof(tmp)) return false;
    strcpy(tmp, path);
    for (char *p = tmp + 1; *p; p++) {
        if (*p == '/') {
            *p = '\0';
            if (access(tmp, F_OK) != 0) {
                if (mkdir(tmp, 0755) != 0 && errno != EEXIST) {
                    return false;
                }
            }
            *p = '/';
        }
    }
    if (access(tmp, F_OK) != 0) {
        if (mkdir(tmp, 0755) != 0 && errno != EEXIST) {
            return false;
        }
    }
    return true;
}

bool runtime_format_dir(Runtime *rt, const char *path) {
    if (!rt || !path || path[0] == '\0') {
        if (rt) rt->last_error.code = ERR_NONSENSE;
        return false;
    }
    if (access(path, F_OK) != 0) {
        if (!make_dirs_p(path)) {
            rt->last_error.code = ERR_NONSENSE;
            return false;
        }
    }
    if (chdir(path) != 0) {
        rt->last_error.code = ERR_NONSENSE;
        return false;
    }
    return true;
}

static bool is_channel_file_open(const Runtime *rt, const char *path) {
    if (!rt || !path) return false;
    for (int i = MIN_USER_CHANNEL; i <= MAX_USER_CHANNEL; i++) {
        if (rt->channels[i].is_open && rt->channels[i].path) {
            if (strcmp(rt->channels[i].path, path) == 0) {
                return true;
            }
        }
    }
    return false;
}

static const char *resolve_dest_path(const char *src_path, const char *dest_path, char *buf, size_t buf_size) {
    struct stat st_dest;
    if (stat(dest_path, &st_dest) == 0 && S_ISDIR(st_dest.st_mode)) {
        struct stat st_src;
        if (stat(src_path, &st_src) == 0 && !S_ISDIR(st_src.st_mode)) {
            const char *base = strrchr(src_path, '/');
            base = base ? base + 1 : src_path;
            if (snprintf(buf, buf_size, "%s/%s", dest_path, base) < (int)buf_size) {
                return buf;
            }
        }
    }
    return dest_path;
}

bool runtime_move_file(Runtime *rt, const char *src_path, const char *dest_path) {
    if (!rt || !src_path || !dest_path || src_path[0] == '\0' || dest_path[0] == '\0') {
        if (rt) rt->last_error.code = ERR_NONSENSE;
        return false;
    }
    char buf[1024];
    const char *final_dest = resolve_dest_path(src_path, dest_path, buf, sizeof(buf));
    if (is_channel_file_open(rt, src_path) || is_channel_file_open(rt, final_dest)) {
        rt->last_error.code = ERR_NONSENSE;
        return false;
    }
    if (rename(src_path, final_dest) != 0) {
        rt->last_error.code = ERR_NONSENSE;
        return false;
    }
    return true;
}

bool runtime_copy_file(Runtime *rt, const char *src_path, const char *dest_path) {
    if (!rt || !src_path || !dest_path || src_path[0] == '\0' || dest_path[0] == '\0') {
        if (rt) rt->last_error.code = ERR_NONSENSE;
        return false;
    }
    struct stat st_src;
    if (stat(src_path, &st_src) != 0 || S_ISDIR(st_src.st_mode)) {
        rt->last_error.code = ERR_NONSENSE;
        return false;
    }
    char buf[1024];
    const char *final_dest = resolve_dest_path(src_path, dest_path, buf, sizeof(buf));

    if (is_channel_file_open(rt, src_path) || is_channel_file_open(rt, final_dest)) {
        rt->last_error.code = ERR_NONSENSE;
        return false;
    }

    struct stat st_dest;
    if (stat(final_dest, &st_dest) == 0) {
        if (st_src.st_dev == st_dest.st_dev && st_src.st_ino == st_dest.st_ino) {
            // Same file, no-op
            return true;
        }
    }

    FILE *src_fp = fopen(src_path, "rb");
    if (!src_fp) {
        rt->last_error.code = ERR_NONSENSE;
        return false;
    }
    FILE *dest_fp = fopen(final_dest, "wb");
    if (!dest_fp) {
        fclose(src_fp);
        rt->last_error.code = ERR_NONSENSE;
        return false;
    }

    char chunk[4096];
    size_t n;
    bool write_ok = true;
    while ((n = fread(chunk, 1, sizeof(chunk), src_fp)) > 0) {
        if (fwrite(chunk, 1, n, dest_fp) != n) {
            write_ok = false;
            break;
        }
    }
    if (ferror(src_fp)) {
        write_ok = false;
    }
    if (fflush(dest_fp) != 0) {
        write_ok = false;
    }
    fclose(src_fp);
    fclose(dest_fp);

    if (!write_ok) {
        unlink(final_dest);
        rt->last_error.code = ERR_NONSENSE;
        return false;
    }

    chmod(final_dest, st_src.st_mode & 0777);
    return true;
}


