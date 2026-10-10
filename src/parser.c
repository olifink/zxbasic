#define _POSIX_C_SOURCE 200809L

#include "parser.h"
#include "expr.h"
#include "console.h"
#include "sysvars.h"
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <math.h>
#include <ctype.h>
#include <errno.h>

static void print_chars(Runtime *rt, const char *s, size_t len) {
    if (!rt || !rt->out || !s || len == 0) return;
    int64_t row = 0, col = 0, scr_rows = 24, scr_cols = 32;
    sysvar_get_int(&rt->sysvars, "S_POSN_ROW", &row);
    sysvar_get_int(&rt->sysvars, "S_POSN_COL", &col);
    sysvar_get_int(&rt->sysvars, "SCR_ROWS", &scr_rows);
    sysvar_get_int(&rt->sysvars, "SCR_COLS", &scr_cols);

    for (size_t i = 0; i < len; i++) {
        char c = s[i];
        fputc(c, rt->out);
        if (c == '\n') {
            rt->print_col = 0;
            col = 0;
            row++;
            if (row >= scr_rows) row = scr_rows - 1;
        } else {
            rt->print_col++;
            col++;
            if (col >= scr_cols) {
                col = 0;
                rt->print_col = 0;
                row++;
                if (row >= scr_rows) row = scr_rows - 1;
            }
        }
    }
    sysvar_set_int(&rt->sysvars, "S_POSN_ROW", row);
    sysvar_set_int(&rt->sysvars, "S_POSN_COL", col);
}

static void print_newline(Runtime *rt) {
    if (!rt || !rt->out) return;
    fputc('\n', rt->out);
    rt->print_col = 0;
    int64_t row = 0, scr_rows = 24;
    sysvar_get_int(&rt->sysvars, "S_POSN_ROW", &row);
    sysvar_get_int(&rt->sysvars, "SCR_ROWS", &scr_rows);
    row++;
    if (row >= scr_rows) row = scr_rows - 1;
    sysvar_set_int(&rt->sysvars, "S_POSN_ROW", row);
    sysvar_set_int(&rt->sysvars, "S_POSN_COL", 0);
}

static void print_tab(Runtime *rt) {
    if (!rt || !rt->out) return;
    int next_tab = ((rt->print_col / 16) + 1) * 16;
    while (rt->print_col < next_tab) {
        print_chars(rt, " ", 1);
    }
}

static bool parse_let(Lexer *l, Runtime *rt) {
    lexer_next(l); // consume LET
    if (l->current.type != TOKEN_IDENT) {
        rt->last_error.code = ERR_NONSENSE;
        return false;
    }

    char *var_name = strdup(l->current.str_val);
    lexer_next(l);

    // Check if target is array element or slice
    if (l->current.type == TOKEN_LPAREN) {
        // Check if declared as array in symtab
        bool is_num_array = false;
        for (size_t i = 0; i < rt->symtab.num_arrays_count; i++) {
            if (strcmp(rt->symtab.num_arrays[i].name, var_name) == 0) {
                is_num_array = true;
                break;
            }
        }
        bool is_str_array = false;
        for (size_t i = 0; i < rt->symtab.str_arrays_count; i++) {
            if (strcmp(rt->symtab.str_arrays[i].name, var_name) == 0) {
                is_str_array = true;
                break;
            }
        }

        if (is_num_array) {
            lexer_next(l); // consume '('
            size_t indices[MAX_ARRAY_DIMS];
            size_t nidx = 0;
            while (true) {
                Value idx_val = expr_eval(l, &rt->symtab, &rt->last_error);
                if (rt->last_error.code != ERR_OK) { free(var_name); return false; }
                if (!value_is_num(&idx_val)) { rt->last_error.code = ERR_NONSENSE; free(var_name); return false; }
                if (nidx >= MAX_ARRAY_DIMS) { rt->last_error.code = ERR_SUBSCRIPT_RANGE; free(var_name); return false; }
                indices[nidx++] = (size_t)floor(idx_val.as.num);

                if (l->current.type == TOKEN_COMMA) {
                    lexer_next(l);
                } else if (l->current.type == TOKEN_RPAREN) {
                    lexer_next(l);
                    break;
                } else {
                    rt->last_error.code = ERR_NONSENSE;
                    free(var_name);
                    return false;
                }
            }

            if (l->current.type != TOKEN_EQUAL) {
                rt->last_error.code = ERR_NONSENSE;
                free(var_name);
                return false;
            }
            lexer_next(l); // consume '='

            Value val = expr_eval(l, &rt->symtab, &rt->last_error);
            if (rt->last_error.code != ERR_OK) { free(var_name); return false; }
            if (!value_is_num(&val)) { rt->last_error.code = ERR_NONSENSE; free(var_name); return false; }

            bool ok = symtab_set_num_array(&rt->symtab, var_name, nidx, indices, val.as.num, &rt->last_error);
            free(var_name);
            return ok;
        }

        if (is_str_array) {
            lexer_next(l); // consume '('
            size_t indices[MAX_ARRAY_DIMS];
            size_t nidx = 0;
            while (true) {
                Value idx_val = expr_eval(l, &rt->symtab, &rt->last_error);
                if (rt->last_error.code != ERR_OK) { free(var_name); return false; }
                if (!value_is_num(&idx_val)) { rt->last_error.code = ERR_NONSENSE; free(var_name); return false; }
                if (nidx >= MAX_ARRAY_DIMS) { rt->last_error.code = ERR_SUBSCRIPT_RANGE; free(var_name); return false; }
                indices[nidx++] = (size_t)floor(idx_val.as.num);

                if (l->current.type == TOKEN_COMMA) {
                    lexer_next(l);
                } else if (l->current.type == TOKEN_RPAREN) {
                    lexer_next(l);
                    break;
                } else {
                    rt->last_error.code = ERR_NONSENSE;
                    free(var_name);
                    return false;
                }
            }

            if (l->current.type != TOKEN_EQUAL) {
                rt->last_error.code = ERR_NONSENSE;
                free(var_name);
                return false;
            }
            lexer_next(l); // consume '='

            Value val = expr_eval(l, &rt->symtab, &rt->last_error);
            if (rt->last_error.code != ERR_OK) { free(var_name); return false; }
            if (!value_is_str(&val)) { rt->last_error.code = ERR_NONSENSE; value_free(&val); free(var_name); return false; }

            bool ok = symtab_set_str_array(&rt->symtab, var_name, nidx, indices, &val, &rt->last_error);
            value_free(&val);
            free(var_name);
            return ok;
        }

        // Slicing assignment on string variable: LET A$(s TO e) = expr or LET A$(i) = expr
        size_t nlen = strlen(var_name);
        if (nlen > 0 && var_name[nlen - 1] == '$') {
            lexer_next(l); // consume '('
            int64_t start = 1;
            int64_t end = 0;
            bool has_start = false;
            bool has_end = false;

            if (l->current.type == TOKEN_TO) {
                lexer_next(l);
                if (l->current.type != TOKEN_RPAREN) {
                    Value ev = expr_eval(l, &rt->symtab, &rt->last_error);
                    if (rt->last_error.code != ERR_OK) { free(var_name); return false; }
                    if (!value_is_num(&ev)) { rt->last_error.code = ERR_NONSENSE; free(var_name); return false; }
                    end = (int64_t)floor(ev.as.num);
                    has_end = true;
                }
            } else {
                Value sv = expr_eval(l, &rt->symtab, &rt->last_error);
                if (rt->last_error.code != ERR_OK) { free(var_name); return false; }
                if (!value_is_num(&sv)) { rt->last_error.code = ERR_NONSENSE; free(var_name); return false; }
                start = (int64_t)floor(sv.as.num);
                has_start = true;

                if (l->current.type == TOKEN_TO) {
                    lexer_next(l);
                    if (l->current.type != TOKEN_RPAREN) {
                        Value ev = expr_eval(l, &rt->symtab, &rt->last_error);
                        if (rt->last_error.code != ERR_OK) { free(var_name); return false; }
                        if (!value_is_num(&ev)) { rt->last_error.code = ERR_NONSENSE; free(var_name); return false; }
                        end = (int64_t)floor(ev.as.num);
                        has_end = true;
                    }
                } else {
                    end = start;
                    has_end = true;
                }
            }

            if (l->current.type != TOKEN_RPAREN) {
                rt->last_error.code = ERR_NONSENSE;
                free(var_name);
                return false;
            }
            lexer_next(l); // consume ')'

            if (l->current.type != TOKEN_EQUAL) {
                rt->last_error.code = ERR_NONSENSE;
                free(var_name);
                return false;
            }
            lexer_next(l); // consume '='

            Value rhs = expr_eval(l, &rt->symtab, &rt->last_error);
            if (rt->last_error.code != ERR_OK) { free(var_name); return false; }
            if (!value_is_str(&rhs)) { rt->last_error.code = ERR_NONSENSE; value_free(&rhs); free(var_name); return false; }

            const char *curr_str = NULL;
            size_t curr_len = 0;
            if (!symtab_get_str(&rt->symtab, var_name, &curr_str, &curr_len)) {
                rt->last_error.code = ERR_VAR_NOT_FOUND;
                value_free(&rhs);
                free(var_name);
                return false;
            }

            if (!has_start) start = 1;
            if (!has_end) end = (int64_t)curr_len;

            if (start < 1 || start > (int64_t)curr_len || end < 1 || end > (int64_t)curr_len || start > end) {
                rt->last_error.code = ERR_SUBSCRIPT_RANGE;
                value_free(&rhs);
                free(var_name);
                return false;
            }

            // Replace characters in curr_str from start to end with rhs characters
            char *buf = malloc(curr_len + 1);
            memcpy(buf, curr_str, curr_len);
            buf[curr_len] = '\0';

            size_t slice_len = (size_t)(end - start + 1);
            size_t copy_len = (rhs.as.str.len < slice_len) ? rhs.as.str.len : slice_len;
            memcpy(buf + (start - 1), rhs.as.str.chars, copy_len);
            // Pad remainder of slice with spaces if rhs is shorter
            if (copy_len < slice_len) {
                memset(buf + (start - 1) + copy_len, ' ', slice_len - copy_len);
            }

            symtab_set_str(&rt->symtab, var_name, buf, curr_len);
            free(buf);
            value_free(&rhs);
            free(var_name);
            return true;
        }

        rt->last_error.code = ERR_NONSENSE;
        free(var_name);
        return false;
    }

    if (l->current.type != TOKEN_EQUAL) {
        rt->last_error.code = ERR_NONSENSE;
        free(var_name);
        return false;
    }
    lexer_next(l); // consume '='

    Value val = expr_eval(l, &rt->symtab, &rt->last_error);
    if (rt->last_error.code != ERR_OK) {
        free(var_name);
        return false;
    }

    const SysVar *sv = sysvar_lookup_const(&rt->sysvars, var_name);
    if (sv && (sv->flags & SVAR_FLAG_READ_ONLY)) {
        rt->last_error.code = ERR_NONSENSE;
        value_free(&val);
        free(var_name);
        return false;
    }

    size_t nlen = strlen(var_name);
    if (nlen > 0 && var_name[nlen - 1] == '$') {
        if (!value_is_str(&val)) {
            rt->last_error.code = ERR_NONSENSE;
            value_free(&val);
            free(var_name);
            return false;
        }
        symtab_set_str(&rt->symtab, var_name, val.as.str.chars, val.as.str.len);
        if (sv) {
            sysvar_set_str(&rt->sysvars, var_name, val.as.str.chars);
        }
    } else {
        if (!value_is_num(&val)) {
            rt->last_error.code = ERR_NONSENSE;
            value_free(&val);
            free(var_name);
            return false;
        }
        symtab_set_num(&rt->symtab, var_name, val.as.num);
        if (sv) {
            sysvar_set_int(&rt->sysvars, var_name, (int64_t)floor(val.as.num));
            console_apply_delta(rt);
        }
    }

    value_free(&val);
    free(var_name);
    return true;
}

static bool parse_print(Lexer *l, Runtime *rt) {
    lexer_next(l); // consume PRINT

    bool ended_with_separator = false;
    bool modified_temp_attrs = false;

    while (l->current.type != TOKEN_EOF && l->current.type != TOKEN_COLON) {
        if (l->current.type == TOKEN_SEMICOLON) {
            ended_with_separator = true;
            lexer_next(l);
            continue;
        }
        if (l->current.type == TOKEN_COMMA) {
            ended_with_separator = true;
            print_tab(rt);
            lexer_next(l);
            continue;
        }
        if (l->current.type == TOKEN_APOSTROPHE) {
            ended_with_separator = false;
            print_newline(rt);
            lexer_next(l);
            continue;
        }
        if (l->current.type == TOKEN_AT) {
            ended_with_separator = false;
            lexer_next(l); // consume AT
            Value row_v = expr_eval(l, &rt->symtab, &rt->last_error);
            if (rt->last_error.code != ERR_OK) goto cleanup;
            if (!value_is_num(&row_v)) { rt->last_error.code = ERR_NONSENSE; goto cleanup; }
            if (l->current.type != TOKEN_COMMA) { rt->last_error.code = ERR_NONSENSE; goto cleanup; }
            lexer_next(l); // consume COMMA
            Value col_v = expr_eval(l, &rt->symtab, &rt->last_error);
            if (rt->last_error.code != ERR_OK) goto cleanup;
            if (!value_is_num(&col_v)) { rt->last_error.code = ERR_NONSENSE; goto cleanup; }

            if (!console_cursor_at(rt, (int64_t)floor(row_v.as.num), (int64_t)floor(col_v.as.num), &rt->last_error)) {
                goto cleanup;
            }
            continue;
        }
        if (l->current.type == TOKEN_INK) {
            ended_with_separator = false;
            lexer_next(l); // consume INK
            Value cv = expr_eval(l, &rt->symtab, &rt->last_error);
            if (rt->last_error.code != ERR_OK) goto cleanup;
            if (!value_is_num(&cv)) { rt->last_error.code = ERR_NONSENSE; goto cleanup; }
            if (!console_set_temporary_ink(rt, (int64_t)floor(cv.as.num), &rt->last_error)) {
                goto cleanup;
            }
            modified_temp_attrs = true;
            continue;
        }
        if (l->current.type == TOKEN_PAPER) {
            ended_with_separator = false;
            lexer_next(l); // consume PAPER
            Value cv = expr_eval(l, &rt->symtab, &rt->last_error);
            if (rt->last_error.code != ERR_OK) goto cleanup;
            if (!value_is_num(&cv)) { rt->last_error.code = ERR_NONSENSE; goto cleanup; }
            if (!console_set_temporary_paper(rt, (int64_t)floor(cv.as.num), &rt->last_error)) {
                goto cleanup;
            }
            modified_temp_attrs = true;
            continue;
        }
        if (l->current.type == TOKEN_BRIGHT) {
            ended_with_separator = false;
            lexer_next(l); // consume BRIGHT
            Value fv = expr_eval(l, &rt->symtab, &rt->last_error);
            if (rt->last_error.code != ERR_OK) goto cleanup;
            if (!value_is_num(&fv)) { rt->last_error.code = ERR_NONSENSE; goto cleanup; }
            if (!console_set_temporary_bright(rt, (int64_t)floor(fv.as.num), &rt->last_error)) {
                goto cleanup;
            }
            modified_temp_attrs = true;
            continue;
        }
        if (l->current.type == TOKEN_INVERSE) {
            ended_with_separator = false;
            lexer_next(l); // consume INVERSE
            Value fv = expr_eval(l, &rt->symtab, &rt->last_error);
            if (rt->last_error.code != ERR_OK) goto cleanup;
            if (!value_is_num(&fv)) { rt->last_error.code = ERR_NONSENSE; goto cleanup; }
            if (!console_set_temporary_inverse(rt, (int64_t)floor(fv.as.num), &rt->last_error)) {
                goto cleanup;
            }
            modified_temp_attrs = true;
            continue;
        }

        Value v = expr_eval(l, &rt->symtab, &rt->last_error);
        if (rt->last_error.code != ERR_OK) {
            goto cleanup;
        }

        char *s = value_to_str(&v);
        print_chars(rt, s, strlen(s));
        free(s);
        value_free(&v);

        ended_with_separator = false;

        // Check if next is separator or expression
        if (l->current.type == TOKEN_SEMICOLON) {
            ended_with_separator = true;
            lexer_next(l);
        } else if (l->current.type == TOKEN_COMMA) {
            ended_with_separator = true;
            print_tab(rt);
            lexer_next(l);
        } else if (l->current.type == TOKEN_APOSTROPHE) {
            ended_with_separator = false;
            print_newline(rt);
            lexer_next(l);
        }
    }

    if (!ended_with_separator) {
        print_newline(rt);
    }
    fflush(rt->out);

    if (modified_temp_attrs) {
        console_reset_temporary_attrs(rt);
    }
    return true;

cleanup:
    if (modified_temp_attrs) {
        console_reset_temporary_attrs(rt);
    }
    return false;
}

static bool parse_input(Lexer *l, Runtime *rt) {
    lexer_next(l); // consume INPUT

    bool had_prompt = false;
    bool modified_temp_attrs = false;

    while (l->current.type != TOKEN_EOF && l->current.type != TOKEN_COLON) {
        if (l->current.type == TOKEN_SEMICOLON || l->current.type == TOKEN_COMMA) {
            lexer_next(l);
            continue;
        }
        if (l->current.type == TOKEN_AT) {
            lexer_next(l); // consume AT
            Value row_v = expr_eval(l, &rt->symtab, &rt->last_error);
            if (rt->last_error.code != ERR_OK) goto cleanup;
            if (!value_is_num(&row_v)) { rt->last_error.code = ERR_NONSENSE; goto cleanup; }
            if (l->current.type != TOKEN_COMMA) { rt->last_error.code = ERR_NONSENSE; goto cleanup; }
            lexer_next(l); // consume COMMA
            Value col_v = expr_eval(l, &rt->symtab, &rt->last_error);
            if (rt->last_error.code != ERR_OK) goto cleanup;
            if (!value_is_num(&col_v)) { rt->last_error.code = ERR_NONSENSE; goto cleanup; }

            if (!console_cursor_at(rt, (int64_t)floor(row_v.as.num), (int64_t)floor(col_v.as.num), &rt->last_error)) {
                goto cleanup;
            }
            continue;
        }
        if (l->current.type == TOKEN_INK) {
            lexer_next(l); // consume INK
            Value cv = expr_eval(l, &rt->symtab, &rt->last_error);
            if (rt->last_error.code != ERR_OK) goto cleanup;
            if (!value_is_num(&cv)) { rt->last_error.code = ERR_NONSENSE; goto cleanup; }
            if (!console_set_temporary_ink(rt, (int64_t)floor(cv.as.num), &rt->last_error)) goto cleanup;
            modified_temp_attrs = true;
            continue;
        }
        if (l->current.type == TOKEN_PAPER) {
            lexer_next(l); // consume PAPER
            Value cv = expr_eval(l, &rt->symtab, &rt->last_error);
            if (rt->last_error.code != ERR_OK) goto cleanup;
            if (!value_is_num(&cv)) { rt->last_error.code = ERR_NONSENSE; goto cleanup; }
            if (!console_set_temporary_paper(rt, (int64_t)floor(cv.as.num), &rt->last_error)) goto cleanup;
            modified_temp_attrs = true;
            continue;
        }
        if (l->current.type == TOKEN_BRIGHT) {
            lexer_next(l); // consume BRIGHT
            Value fv = expr_eval(l, &rt->symtab, &rt->last_error);
            if (rt->last_error.code != ERR_OK) goto cleanup;
            if (!value_is_num(&fv)) { rt->last_error.code = ERR_NONSENSE; goto cleanup; }
            if (!console_set_temporary_bright(rt, (int64_t)floor(fv.as.num), &rt->last_error)) goto cleanup;
            modified_temp_attrs = true;
            continue;
        }
        if (l->current.type == TOKEN_INVERSE) {
            lexer_next(l); // consume INVERSE
            Value fv = expr_eval(l, &rt->symtab, &rt->last_error);
            if (rt->last_error.code != ERR_OK) goto cleanup;
            if (!value_is_num(&fv)) { rt->last_error.code = ERR_NONSENSE; goto cleanup; }
            if (!console_set_temporary_inverse(rt, (int64_t)floor(fv.as.num), &rt->last_error)) goto cleanup;
            modified_temp_attrs = true;
            continue;
        }
        if (l->current.type == TOKEN_STRING) {
            had_prompt = true;
            print_chars(rt, l->current.str_val, l->current.str_len);
            lexer_next(l);
            continue;
        }
        break; // Identifier
    }

    if (!had_prompt) {
        print_chars(rt, "? ", 2);
    }
    fflush(rt->out);

    if (l->current.type != TOKEN_IDENT) {
        rt->last_error.code = ERR_NONSENSE;
        goto cleanup;
    }

    char *var_name = strdup(l->current.str_val);
    lexer_next(l);

    char line_buf[1024];
    if (!fgets(line_buf, sizeof(line_buf), rt->in ? rt->in : stdin)) {
        if (g_interrupted || errno == EINTR) {
            g_interrupted = 0;
            rt->stop_requested = true;
            rt->last_error.code = ERR_STOP;
            rt->last_error.custom_msg = "BREAK into program";
            free(var_name);
            goto cleanup;
        }
        line_buf[0] = '\0';
    }

    // Strip trailing \r, \n
    size_t in_len = strlen(line_buf);
    while (in_len > 0 && (line_buf[in_len - 1] == '\r' || line_buf[in_len - 1] == '\n')) {
        line_buf[--in_len] = '\0';
    }

    size_t nlen = strlen(var_name);
    if (nlen > 0 && var_name[nlen - 1] == '$') {
        symtab_set_str(&rt->symtab, var_name, line_buf, in_len);
    } else {
        double val = strtod(line_buf, NULL);
        symtab_set_num(&rt->symtab, var_name, val);
    }

    free(var_name);
    if (modified_temp_attrs) {
        console_reset_temporary_attrs(rt);
    }
    return true;

cleanup:
    if (modified_temp_attrs) {
        console_reset_temporary_attrs(rt);
    }
    return false;
}

static bool parse_dim(Lexer *l, Runtime *rt) {
    lexer_next(l); // consume DIM

    if (l->current.type != TOKEN_IDENT) {
        rt->last_error.code = ERR_NONSENSE;
        return false;
    }

    char *name = strdup(l->current.str_val);
    lexer_next(l);

    if (l->current.type != TOKEN_LPAREN) {
        rt->last_error.code = ERR_NONSENSE;
        free(name);
        return false;
    }
    lexer_next(l); // consume '('

    size_t dims[MAX_ARRAY_DIMS];
    size_t ndims = 0;

    while (true) {
        Value v = expr_eval(l, &rt->symtab, &rt->last_error);
        if (rt->last_error.code != ERR_OK) { free(name); return false; }
        if (!value_is_num(&v)) { rt->last_error.code = ERR_NONSENSE; free(name); return false; }
        if (ndims >= MAX_ARRAY_DIMS) { rt->last_error.code = ERR_SUBSCRIPT_RANGE; free(name); return false; }
        dims[ndims++] = (size_t)floor(v.as.num);

        if (l->current.type == TOKEN_COMMA) {
            lexer_next(l);
        } else if (l->current.type == TOKEN_RPAREN) {
            lexer_next(l);
            break;
        } else {
            rt->last_error.code = ERR_NONSENSE;
            free(name);
            return false;
        }
    }

    size_t nlen = strlen(name);
    bool ok = false;
    if (nlen > 0 && name[nlen - 1] == '$') {
        ok = symtab_dim_str_array(&rt->symtab, name, ndims, dims, &rt->last_error);
    } else {
        ok = symtab_dim_num_array(&rt->symtab, name, ndims, dims, &rt->last_error);
    }
    free(name);
    return ok;
}

static bool parse_goto(Lexer *l, Runtime *rt) {
    lexer_next(l); // consume GOTO
    Value target_val = expr_eval(l, &rt->symtab, &rt->last_error);
    if (rt->last_error.code != ERR_OK) return false;
    if (!value_is_num(&target_val)) {
        rt->last_error.code = ERR_NONSENSE;
        return false;
    }

    uint16_t target_line = (uint16_t)floor(target_val.as.num);
    ssize_t idx = program_find_index(&rt->program, target_line);
    if (idx < 0) {
        rt->last_error.code = ERR_INTEGER_RANGE;
        return false;
    }

    rt->jump_requested = true;
    rt->jump_line_idx = (size_t)idx;
    rt->jump_stmt_idx = 1;
    rt->jump_offset = 0;
    return true;
}

static bool parse_gosub(Lexer *l, Runtime *rt, size_t next_stmt_offset) {
    (void)next_stmt_offset;
    lexer_next(l); // consume GOSUB
    Value target_val = expr_eval(l, &rt->symtab, &rt->last_error);
    if (rt->last_error.code != ERR_OK) return false;
    if (!value_is_num(&target_val)) {
        rt->last_error.code = ERR_NONSENSE;
        return false;
    }

    uint16_t target_line = (uint16_t)floor(target_val.as.num);
    ssize_t idx = program_find_index(&rt->program, target_line);
    if (idx < 0) {
        rt->last_error.code = ERR_INTEGER_RANGE;
        return false;
    }

    if (rt->call_sp >= MAX_CALL_STACK) {
        rt->last_error.code = ERR_OUT_OF_MEMORY;
        return false;
    }

    if (l->current.type == TOKEN_COLON) {
        rt->call_stack[rt->call_sp].line_idx = rt->cur_line_idx;
        rt->call_stack[rt->call_sp].stmt_idx = rt->cur_stmt_idx + 1;
        rt->call_stack[rt->call_sp].char_offset = rt->cur_offset + l->cursor;
    } else {
        rt->call_stack[rt->call_sp].line_idx = rt->cur_line_idx + 1;
        rt->call_stack[rt->call_sp].stmt_idx = 1;
        rt->call_stack[rt->call_sp].char_offset = 0;
    }
    rt->call_sp++;

    rt->jump_requested = true;
    rt->jump_line_idx = (size_t)idx;
    rt->jump_stmt_idx = 1;
    rt->jump_offset = 0;
    return true;
}

static bool parse_return(Lexer *l, Runtime *rt) {
    lexer_next(l); // consume RETURN
    if (rt->call_sp == 0) {
        rt->last_error.code = ERR_RETURN_NO_GOSUB;
        return false;
    }

    rt->call_sp--;
    CallFrame frame = rt->call_stack[rt->call_sp];

    rt->jump_requested = true;
    rt->jump_line_idx = frame.line_idx;
    rt->jump_stmt_idx = frame.stmt_idx;
    rt->jump_offset = frame.char_offset;
    return true;
}

static bool parse_for(Lexer *l, Runtime *rt, size_t next_stmt_offset) {
    (void)next_stmt_offset;
    lexer_next(l); // consume FOR
    if (l->current.type != TOKEN_IDENT) {
        rt->last_error.code = ERR_NONSENSE;
        return false;
    }

    char *var_name = strdup(l->current.str_val);
    lexer_next(l);

    if (l->current.type != TOKEN_EQUAL) {
        rt->last_error.code = ERR_NONSENSE;
        free(var_name);
        return false;
    }
    lexer_next(l); // consume '='

    Value start_val = expr_eval(l, &rt->symtab, &rt->last_error);
    if (rt->last_error.code != ERR_OK) { free(var_name); return false; }
    if (!value_is_num(&start_val)) { rt->last_error.code = ERR_NONSENSE; free(var_name); return false; }

    if (l->current.type != TOKEN_TO) {
        rt->last_error.code = ERR_NONSENSE;
        free(var_name);
        return false;
    }
    lexer_next(l); // consume TO

    Value limit_val = expr_eval(l, &rt->symtab, &rt->last_error);
    if (rt->last_error.code != ERR_OK) { free(var_name); return false; }
    if (!value_is_num(&limit_val)) { rt->last_error.code = ERR_NONSENSE; free(var_name); return false; }

    double step = 1.0;
    if (l->current.type == TOKEN_STEP) {
        lexer_next(l); // consume STEP
        Value step_val = expr_eval(l, &rt->symtab, &rt->last_error);
        if (rt->last_error.code != ERR_OK) { free(var_name); return false; }
        if (!value_is_num(&step_val)) { rt->last_error.code = ERR_NONSENSE; free(var_name); return false; }
        step = step_val.as.num;
    }

    symtab_set_num(&rt->symtab, var_name, start_val.as.num);

    // Reuse existing loop frame for same variable if already present on top
    ssize_t frame_idx = -1;
    for (ssize_t i = (ssize_t)rt->for_sp - 1; i >= 0; i--) {
        if (strcmp(rt->for_stack[i].var_name, var_name) == 0) {
            frame_idx = i;
            break;
        }
    }

    if (frame_idx < 0) {
        if (rt->for_sp >= MAX_FOR_STACK) {
            rt->last_error.code = ERR_OUT_OF_MEMORY;
            free(var_name);
            return false;
        }
        frame_idx = (ssize_t)rt->for_sp++;
    }

    ForFrame *ff = &rt->for_stack[frame_idx];
    strncpy(ff->var_name, var_name, sizeof(ff->var_name) - 1);
    ff->var_name[sizeof(ff->var_name) - 1] = '\0';
    ff->limit = limit_val.as.num;
    ff->step = step;
    if (l->current.type == TOKEN_COLON) {
        ff->line_idx = rt->cur_line_idx;
        ff->stmt_idx = rt->cur_stmt_idx + 1;
        ff->char_offset = rt->cur_offset + l->cursor;
    } else {
        ff->line_idx = rt->cur_line_idx + 1;
        ff->stmt_idx = 1;
        ff->char_offset = 0;
    }

    free(var_name);
    return true;
}

static bool parse_next(Lexer *l, Runtime *rt) {
    lexer_next(l); // consume NEXT
    if (l->current.type != TOKEN_IDENT) {
        rt->last_error.code = ERR_NONSENSE;
        return false;
    }

    char *var_name = strdup(l->current.str_val);
    lexer_next(l);

    ssize_t frame_idx = -1;
    for (ssize_t i = (ssize_t)rt->for_sp - 1; i >= 0; i--) {
        if (strcmp(rt->for_stack[i].var_name, var_name) == 0) {
            frame_idx = i;
            break;
        }
    }

    if (frame_idx < 0) {
        rt->last_error.code = ERR_NONSENSE;
        free(var_name);
        return false;
    }

    ForFrame *ff = &rt->for_stack[frame_idx];
    double val = 0.0;
    symtab_get_num(&rt->symtab, var_name, &val);
    val += ff->step;
    symtab_set_num(&rt->symtab, var_name, val);

    bool loop_again = (ff->step >= 0.0) ? (val <= ff->limit) : (val >= ff->limit);
    if (loop_again) {
        rt->jump_requested = true;
        rt->jump_line_idx = ff->line_idx;
        rt->jump_stmt_idx = ff->stmt_idx;
        rt->jump_offset = ff->char_offset;
    } else {
        // Pop loop and any nested loops above it
        rt->for_sp = (size_t)frame_idx;
    }

    free(var_name);
    return true;
}

static bool parse_restore(Lexer *l, Runtime *rt) {
    lexer_next(l); // consume RESTORE
    if (l->current.type != TOKEN_EOF && l->current.type != TOKEN_COLON) {
        Value v = expr_eval(l, &rt->symtab, &rt->last_error);
        if (rt->last_error.code != ERR_OK) return false;
        if (!value_is_num(&v)) { rt->last_error.code = ERR_NONSENSE; return false; }
        uint16_t line_no = (uint16_t)floor(v.as.num);
        ssize_t idx = program_find_ge_index(&rt->program, line_no);
        rt->data_line_idx = (idx >= 0) ? (size_t)idx : rt->program.count;
    } else {
        rt->data_line_idx = 0;
    }
    rt->data_offset = 0;
    rt->data_initialized = true;
    return true;
}

bool runtime_read_next_data(Runtime *rt, Value *out_val) {
    if (!rt) return false;
    if (!rt->data_initialized) {
        rt->data_line_idx = 0;
        rt->data_offset = 0;
        rt->data_initialized = true;
    }

    while (rt->data_line_idx < rt->program.count) {
        const char *src = rt->program.lines[rt->data_line_idx].source;
        size_t len = strlen(src);

        // Find next DATA item
        size_t pos = rt->data_offset;

        // Skip leading whitespace / colons
        while (pos < len && (src[pos] == ' ' || src[pos] == '\t' || src[pos] == ':')) {
            pos++;
        }

        if (pos >= len) {
            // Next line
            rt->data_line_idx++;
            rt->data_offset = 0;
            continue;
        }

        // Check if cursor is at a DATA statement or continuing one
        if (rt->data_offset == 0) {
            // Must find "DATA" keyword at statement start
            const char *data_pos = strstr(src, "DATA");
            const char *data_pos_lower = strstr(src, "data");
            const char *found = data_pos ? data_pos : data_pos_lower;
            if (!found) {
                rt->data_line_idx++;
                rt->data_offset = 0;
                continue;
            }
            pos = (found - src) + 4;
        }

        // Skip spaces and commas
        while (pos < len && (src[pos] == ' ' || src[pos] == '\t' || src[pos] == ',')) {
            pos++;
        }

        if (pos >= len || src[pos] == ':') {
            // End of this DATA statement
            rt->data_line_idx++;
            rt->data_offset = 0;
            continue;
        }

        // Read item: either string in quotes or literal
        if (src[pos] == '"') {
            pos++; // skip initial "
            size_t start = pos;
            while (pos < len && src[pos] != '"') {
                pos++;
            }
            size_t str_len = pos - start;
            if (pos < len && src[pos] == '"') pos++; // skip closing "
            rt->data_offset = pos;
            if (out_val) *out_val = value_string_len(src + start, str_len);
            return true;
        } else {
            size_t start = pos;
            while (pos < len && src[pos] != ',' && src[pos] != ':' && src[pos] != '\r' && src[pos] != '\n') {
                pos++;
            }
            size_t item_len = pos - start;
            // Trim trailing spaces
            while (item_len > 0 && isspace((unsigned char)src[start + item_len - 1])) {
                item_len--;
            }
            char temp[128];
            if (item_len < sizeof(temp)) {
                memcpy(temp, src + start, item_len);
                temp[item_len] = '\0';
                char *endp = NULL;
                double num = strtod(temp, &endp);
                while (*endp && isspace((unsigned char)*endp)) endp++;
                if (*endp == '\0') {
                    if (out_val) *out_val = value_number(num);
                } else {
                    if (out_val) *out_val = value_string_len(src + start, item_len);
                }
            } else {
                if (out_val) *out_val = value_string_len(src + start, item_len);
            }
            rt->data_offset = pos;
            return true;
        }
    }

    rt->last_error.code = ERR_END_OF_DATA;
    return false;
}

static bool parse_read(Lexer *l, Runtime *rt) {
    lexer_next(l); // consume READ

    while (true) {
        if (l->current.type != TOKEN_IDENT) {
            rt->last_error.code = ERR_NONSENSE;
            return false;
        }

        char *var_name = strdup(l->current.str_val);
        lexer_next(l);

        Value item = value_nil();
        if (!runtime_read_next_data(rt, &item)) {
            free(var_name);
            return false;
        }

        size_t nlen = strlen(var_name);
        if (nlen > 0 && var_name[nlen - 1] == '$') {
            if (!value_is_str(&item)) {
                // Number converted to string or error
                char *s = value_to_str(&item);
                symtab_set_str(&rt->symtab, var_name, s, strlen(s));
                free(s);
            } else {
                symtab_set_str(&rt->symtab, var_name, item.as.str.chars, item.as.str.len);
            }
        } else {
            if (!value_is_num(&item)) {
                rt->last_error.code = ERR_NONSENSE;
                value_free(&item);
                free(var_name);
                return false;
            }
            symtab_set_num(&rt->symtab, var_name, item.as.num);
        }

        value_free(&item);
        free(var_name);

        if (l->current.type == TOKEN_COMMA) {
            lexer_next(l);
        } else {
            break;
        }
    }

    return true;
}

bool parser_execute_statement(Lexer *l, Runtime *rt, size_t next_stmt_offset, bool *skip_rest_of_line) {
    if (!l || !rt) return false;

    TokenType t = l->current.type;

    switch (t) {
        case TOKEN_REM:
            // Handled in lexer
            return true;

        case TOKEN_LET:
            return parse_let(l, rt);

        case TOKEN_PRINT:
            return parse_print(l, rt);

        case TOKEN_INPUT:
            return parse_input(l, rt);

        case TOKEN_DIM:
            return parse_dim(l, rt);

        case TOKEN_GOTO:
            return parse_goto(l, rt);

        case TOKEN_GOSUB:
            return parse_gosub(l, rt, next_stmt_offset);

        case TOKEN_RETURN:
            return parse_return(l, rt);

        case TOKEN_FOR:
            return parse_for(l, rt, next_stmt_offset);

        case TOKEN_NEXT:
            return parse_next(l, rt);

        case TOKEN_RESTORE:
            return parse_restore(l, rt);

        case TOKEN_READ:
            return parse_read(l, rt);

        case TOKEN_DATA:
            // Skip data items during normal execution
            while (l->current.type != TOKEN_EOF && l->current.type != TOKEN_COLON) {
                lexer_next(l);
            }
            return true;

        case TOKEN_STOP:
            rt->stop_requested = true;
            rt->last_error.code = ERR_STOP;
            rt->last_error.custom_msg = NULL;
            return false;

        case TOKEN_IF: {
            lexer_next(l); // consume IF
            Value cond = expr_eval(l, &rt->symtab, &rt->last_error);
            if (rt->last_error.code != ERR_OK) return false;

            if (l->current.type != TOKEN_THEN) {
                rt->last_error.code = ERR_NONSENSE;
                value_free(&cond);
                return false;
            }
            lexer_next(l); // consume THEN

            if (value_is_truthy(&cond)) {
                value_free(&cond);
                if (l->current.type == TOKEN_NUMBER) {
                    // IF cond THEN <line_no> -> GOTO <line_no>
                    uint16_t target_line = (uint16_t)floor(l->current.num_val);
                    ssize_t idx = program_find_index(&rt->program, target_line);
                    if (idx < 0) {
                        rt->last_error.code = ERR_INTEGER_RANGE;
                        return false;
                    }
                    rt->jump_requested = true;
                    rt->jump_line_idx = (size_t)idx;
                    rt->jump_stmt_idx = 1;
                    rt->jump_offset = 0;
                    return true;
                }
                return parser_execute_statement(l, rt, next_stmt_offset, skip_rest_of_line);
            } else {
                value_free(&cond);
                if (skip_rest_of_line) *skip_rest_of_line = true;
                return true;
            }
        }

        case TOKEN_RUN: {
            lexer_next(l);
            uint16_t start_line = 0;
            if (l->current.type != TOKEN_EOF && l->current.type != TOKEN_COLON) {
                Value v = expr_eval(l, &rt->symtab, &rt->last_error);
                if (rt->last_error.code != ERR_OK) return false;
                if (!value_is_num(&v)) { rt->last_error.code = ERR_NONSENSE; return false; }
                start_line = (uint16_t)floor(v.as.num);
            }
            runtime_run(rt, start_line);
            return true;
        }

        case TOKEN_LIST: {
            lexer_next(l);
            uint16_t start_line = 0;
            if (l->current.type != TOKEN_EOF && l->current.type != TOKEN_COLON) {
                Value v = expr_eval(l, &rt->symtab, &rt->last_error);
                if (rt->last_error.code != ERR_OK) return false;
                if (!value_is_num(&v)) { rt->last_error.code = ERR_NONSENSE; return false; }
                start_line = (uint16_t)floor(v.as.num);
            }
            program_list(&rt->program, start_line, rt->out);
            return true;
        }

        case TOKEN_NEW:
            lexer_next(l);
            program_clear(&rt->program);
            runtime_clear(rt);
            return true;

        case TOKEN_CLEAR:
            lexer_next(l);
            runtime_clear(rt);
            return true;

        case TOKEN_CLS:
            lexer_next(l);
            console_cls(rt);
            return true;

        case TOKEN_INK: {
            lexer_next(l);
            Value v = expr_eval(l, &rt->symtab, &rt->last_error);
            if (rt->last_error.code != ERR_OK) return false;
            if (!value_is_num(&v)) { rt->last_error.code = ERR_NONSENSE; return false; }
            return console_set_permanent_ink(rt, (int64_t)floor(v.as.num), &rt->last_error);
        }

        case TOKEN_PAPER: {
            lexer_next(l);
            Value v = expr_eval(l, &rt->symtab, &rt->last_error);
            if (rt->last_error.code != ERR_OK) return false;
            if (!value_is_num(&v)) { rt->last_error.code = ERR_NONSENSE; return false; }
            return console_set_permanent_paper(rt, (int64_t)floor(v.as.num), &rt->last_error);
        }

        case TOKEN_BRIGHT: {
            lexer_next(l);
            Value v = expr_eval(l, &rt->symtab, &rt->last_error);
            if (rt->last_error.code != ERR_OK) return false;
            if (!value_is_num(&v)) { rt->last_error.code = ERR_NONSENSE; return false; }
            return console_set_permanent_bright(rt, (int64_t)floor(v.as.num), &rt->last_error);
        }

        case TOKEN_INVERSE: {
            lexer_next(l);
            Value v = expr_eval(l, &rt->symtab, &rt->last_error);
            if (rt->last_error.code != ERR_OK) return false;
            if (!value_is_num(&v)) { rt->last_error.code = ERR_NONSENSE; return false; }
            return console_set_permanent_inverse(rt, (int64_t)floor(v.as.num), &rt->last_error);
        }

        case TOKEN_AT:
            rt->last_error.code = ERR_NONSENSE;
            return false;

        case TOKEN_SAVE: {
            lexer_next(l);
            Value path_val = expr_eval(l, &rt->symtab, &rt->last_error);
            if (rt->last_error.code != ERR_OK) return false;
            if (!value_is_str(&path_val)) { rt->last_error.code = ERR_NONSENSE; return false; }
            bool ok = runtime_save(rt, path_val.as.str.chars);
            value_free(&path_val);
            return ok;
        }

        case TOKEN_LOAD: {
            lexer_next(l);
            Value path_val = expr_eval(l, &rt->symtab, &rt->last_error);
            if (rt->last_error.code != ERR_OK) return false;
            if (!value_is_str(&path_val)) { rt->last_error.code = ERR_NONSENSE; return false; }
            bool ok = runtime_load(rt, path_val.as.str.chars);
            value_free(&path_val);
            return ok;
        }

        case TOKEN_EDIT: {
            if (rt->is_running) {
                rt->last_error.code = ERR_NONSENSE;
                return false;
            }
            lexer_next(l);
            Value line_val = expr_eval(l, &rt->symtab, &rt->last_error);
            if (rt->last_error.code != ERR_OK) return false;
            if (!value_is_num(&line_val)) { rt->last_error.code = ERR_NONSENSE; return false; }
            uint16_t target_line = (uint16_t)floor(line_val.as.num);
            ssize_t idx = program_find_index(&rt->program, target_line);
            if (idx < 0) {
                rt->last_error.code = ERR_INTEGER_RANGE;
                return false;
            }
            const ProgramLine *pl = program_get_line(&rt->program, (size_t)idx);
            char buf[4096];
            snprintf(buf, sizeof(buf), "%u %s", (unsigned int)pl->line_no, pl->source);
            free(rt->edit_prefill_buffer);
            rt->edit_prefill_buffer = strdup(buf);
            return true;
        }

        case TOKEN_AUTO: {
            if (rt->is_running) {
                rt->last_error.code = ERR_NONSENSE;
                return false;
            }
            lexer_next(l);
            uint16_t start_line = 10;
            uint16_t step = 10;
            if (l->current.type != TOKEN_EOF && l->current.type != TOKEN_COLON) {
                Value sv = expr_eval(l, &rt->symtab, &rt->last_error);
                if (rt->last_error.code != ERR_OK) return false;
                if (!value_is_num(&sv)) { rt->last_error.code = ERR_NONSENSE; return false; }
                start_line = (uint16_t)floor(sv.as.num);

                if (l->current.type == TOKEN_COMMA) {
                    lexer_next(l);
                    Value step_v = expr_eval(l, &rt->symtab, &rt->last_error);
                    if (rt->last_error.code != ERR_OK) return false;
                    if (!value_is_num(&step_v)) { rt->last_error.code = ERR_NONSENSE; return false; }
                    step = (uint16_t)floor(step_v.as.num);
                }
            }
            if (start_line < MIN_LINE_NO || start_line > MAX_LINE_NO || step < 1) {
                rt->last_error.code = ERR_INTEGER_RANGE;
                return false;
            }
            rt->auto_mode = true;
            rt->auto_current_line = start_line;
            rt->auto_step = step;
            return true;
        }

        case TOKEN_RENUM: {
            if (rt->is_running) {
                rt->last_error.code = ERR_NONSENSE;
                return false;
            }
            lexer_next(l);
            uint16_t start_line = 10;
            uint16_t step = 10;
            if (l->current.type != TOKEN_EOF && l->current.type != TOKEN_COLON) {
                Value sv = expr_eval(l, &rt->symtab, &rt->last_error);
                if (rt->last_error.code != ERR_OK) return false;
                if (!value_is_num(&sv)) { rt->last_error.code = ERR_NONSENSE; return false; }
                start_line = (uint16_t)floor(sv.as.num);

                if (l->current.type == TOKEN_COMMA) {
                    lexer_next(l);
                    Value step_v = expr_eval(l, &rt->symtab, &rt->last_error);
                    if (rt->last_error.code != ERR_OK) return false;
                    if (!value_is_num(&step_v)) { rt->last_error.code = ERR_NONSENSE; return false; }
                    step = (uint16_t)floor(step_v.as.num);
                }
            }
            return program_renumber(&rt->program, start_line, step, &rt->last_error, rt->out);
        }

        case TOKEN_EXIT: {
            lexer_next(l);
            int code = 0;
            if (l->current.type != TOKEN_EOF && l->current.type != TOKEN_COLON) {
                Value cv = expr_eval(l, &rt->symtab, &rt->last_error);
                if (rt->last_error.code != ERR_OK) return false;
                if (!value_is_num(&cv)) { rt->last_error.code = ERR_NONSENSE; return false; }
                code = (int)floor(cv.as.num);
            }
            exit(code);
            return true;
        }

        default:
            rt->last_error.code = ERR_NONSENSE;
            return false;
    }
}
