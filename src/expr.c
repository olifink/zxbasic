#define _POSIX_C_SOURCE 200809L

#include "expr.h"
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <math.h>
#include <poll.h>
#include <unistd.h>

typedef enum {
    PREC_NONE = 0,
    PREC_OR,         // OR
    PREC_AND,        // AND
    PREC_NOT,        // NOT
    PREC_RELATIONAL, // =, <>, <, >, <=, >=
    PREC_TERM,       // +, - (binary)
    PREC_FACTOR,     // *, /
    PREC_UNARY,      // +, - (unary)
    PREC_POWER,      // ^
    PREC_POSTFIX     // (, slicing
} Precedence;

static Value parse_precedence(Lexer *l, SymTab *st, Precedence prec, BasicError *err);

static Precedence get_infix_precedence(TokenType type) {
    switch (type) {
        case TOKEN_OR:
            return PREC_OR;
        case TOKEN_AND:
            return PREC_AND;
        case TOKEN_EQUAL:
        case TOKEN_NOT_EQUAL:
        case TOKEN_LESS:
        case TOKEN_GREATER:
        case TOKEN_LESS_EQUAL:
        case TOKEN_GREATER_EQUAL:
            return PREC_RELATIONAL;
        case TOKEN_PLUS:
        case TOKEN_MINUS:
            return PREC_TERM;
        case TOKEN_STAR:
        case TOKEN_SLASH:
            return PREC_FACTOR;
        case TOKEN_CARET:
            return PREC_POWER;
        case TOKEN_LPAREN:
            return PREC_POSTFIX;
        default:
            return PREC_NONE;
    }
}

static Value eval_val_function(const Value *arg, SymTab *st, BasicError *err) {
    if (!value_is_str(arg)) {
        if (err) err->code = ERR_NONSENSE;
        return value_nil();
    }
    Lexer inner_l;
    lexer_init(&inner_l, arg->as.str.chars);
    BasicError inner_err = { .code = ERR_OK, .custom_msg = NULL, .line_no = -1, .stmt_index = 0 };
    Value res = expr_eval(&inner_l, st, &inner_err);
    if (inner_err.code != ERR_OK || inner_l.current.type != TOKEN_EOF) {
        if (err) err->code = ERR_NONSENSE;
        value_free(&res);
        token_free(&inner_l.current);
        return value_nil();
    }
    token_free(&inner_l.current);
    return res;
}

static Value parse_slice_on_string(Value str_val, Lexer *l, SymTab *st, BasicError *err) {
    // We already saw '('
    lexer_next(l);

    bool has_start = false;
    bool has_end = false;
    int64_t start = 1;
    int64_t end = (int64_t)str_val.as.str.len;

    if (l->current.type == TOKEN_TO) {
        has_start = false;
        lexer_next(l);
        if (l->current.type != TOKEN_RPAREN) {
            Value ev = expr_eval(l, st, err);
            if (err && err->code != ERR_OK) { value_free(&str_val); return value_nil(); }
            if (!value_is_num(&ev)) { if (err) err->code = ERR_NONSENSE; value_free(&str_val); return value_nil(); }
            end = (int64_t)floor(ev.as.num);
            has_end = true;
        }
    } else {
        Value sv = expr_eval(l, st, err);
        if (err && err->code != ERR_OK) { value_free(&str_val); return value_nil(); }
        if (!value_is_num(&sv)) { if (err) err->code = ERR_NONSENSE; value_free(&str_val); return value_nil(); }
        start = (int64_t)floor(sv.as.num);
        has_start = true;

        if (l->current.type == TOKEN_TO) {
            lexer_next(l);
            if (l->current.type != TOKEN_RPAREN) {
                Value ev = expr_eval(l, st, err);
                if (err && err->code != ERR_OK) { value_free(&str_val); return value_nil(); }
                if (!value_is_num(&ev)) { if (err) err->code = ERR_NONSENSE; value_free(&str_val); return value_nil(); }
                end = (int64_t)floor(ev.as.num);
                has_end = true;
            }
        } else {
            // Single index A$(i) -> A$(i TO i)
            end = start;
            has_end = true;
        }
    }

    if (l->current.type != TOKEN_RPAREN) {
        if (err) err->code = ERR_NONSENSE;
        value_free(&str_val);
        return value_nil();
    }
    lexer_next(l);

    Value res = value_slice(&str_val, start, end, has_start, has_end, err);
    value_free(&str_val);
    return res;
}

static Value parse_unary_function(Lexer *l, SymTab *st, TokenType func_type, BasicError *err) {
    lexer_next(l); // consume function name
    if (l->current.type != TOKEN_LPAREN) {
        if (err) err->code = ERR_NONSENSE;
        return value_nil();
    }
    lexer_next(l); // consume '('

    Value arg = expr_eval(l, st, err);
    if (err && err->code != ERR_OK) return value_nil();

    if (l->current.type != TOKEN_RPAREN) {
        if (err) err->code = ERR_NONSENSE;
        value_free(&arg);
        return value_nil();
    }
    lexer_next(l); // consume ')'

    Value result = value_nil();
    switch (func_type) {
        case TOKEN_ABS:
            if (!value_is_num(&arg)) { if (err) err->code = ERR_NONSENSE; }
            else result = value_number(fabs(arg.as.num));
            break;
        case TOKEN_ACS:
            if (!value_is_num(&arg) || arg.as.num < -1.0 || arg.as.num > 1.0) { if (err) err->code = ERR_INTEGER_RANGE; }
            else result = value_number(acos(arg.as.num));
            break;
        case TOKEN_ASN:
            if (!value_is_num(&arg) || arg.as.num < -1.0 || arg.as.num > 1.0) { if (err) err->code = ERR_INTEGER_RANGE; }
            else result = value_number(asin(arg.as.num));
            break;
        case TOKEN_ATN:
            if (!value_is_num(&arg)) { if (err) err->code = ERR_NONSENSE; }
            else result = value_number(atan(arg.as.num));
            break;
        case TOKEN_COS:
            if (!value_is_num(&arg)) { if (err) err->code = ERR_NONSENSE; }
            else result = value_number(cos(arg.as.num));
            break;
        case TOKEN_EXP:
            if (!value_is_num(&arg)) { if (err) err->code = ERR_NONSENSE; }
            else result = value_number(exp(arg.as.num));
            break;
        case TOKEN_INT:
            if (!value_is_num(&arg)) { if (err) err->code = ERR_NONSENSE; }
            else result = value_number(floor(arg.as.num));
            break;
        case TOKEN_LN:
            if (!value_is_num(&arg) || arg.as.num <= 0.0) { if (err) err->code = ERR_INTEGER_RANGE; }
            else result = value_number(log(arg.as.num));
            break;
        case TOKEN_SGN:
            if (!value_is_num(&arg)) { if (err) err->code = ERR_NONSENSE; }
            else {
                if (arg.as.num > 0.0) result = value_number(1.0);
                else if (arg.as.num < 0.0) result = value_number(-1.0);
                else result = value_number(0.0);
            }
            break;
        case TOKEN_SIN:
            if (!value_is_num(&arg)) { if (err) err->code = ERR_NONSENSE; }
            else result = value_number(sin(arg.as.num));
            break;
        case TOKEN_SQR:
            if (!value_is_num(&arg) || arg.as.num < 0.0) { if (err) err->code = ERR_INTEGER_RANGE; }
            else result = value_number(sqrt(arg.as.num));
            break;
        case TOKEN_TAN:
            if (!value_is_num(&arg)) { if (err) err->code = ERR_NONSENSE; }
            else result = value_number(tan(arg.as.num));
            break;
        case TOKEN_CHR_STR:
            if (!value_is_num(&arg)) { if (err) err->code = ERR_NONSENSE; }
            else {
                int code = (int)floor(arg.as.num);
                if (code < 0 || code > 255) { if (err) err->code = ERR_INTEGER_RANGE; }
                else {
                    char c = (char)code;
                    result = value_string_len(&c, 1);
                }
            }
            break;
        case TOKEN_CODE:
            if (!value_is_str(&arg)) { if (err) err->code = ERR_NONSENSE; }
            else {
                if (arg.as.str.len == 0) result = value_number(0.0);
                else result = value_number((unsigned char)arg.as.str.chars[0]);
            }
            break;
        case TOKEN_LEN:
            if (!value_is_str(&arg)) { if (err) err->code = ERR_NONSENSE; }
            else result = value_number((double)arg.as.str.len);
            break;
        case TOKEN_STR_STR:
            if (!value_is_num(&arg)) { if (err) err->code = ERR_NONSENSE; }
            else {
                char *s = value_to_str(&arg);
                result = value_string(s);
                free(s);
            }
            break;
        case TOKEN_VAL:
            result = eval_val_function(&arg, st, err);
            break;
        default:
            if (err) err->code = ERR_NONSENSE;
            break;
    }

    value_free(&arg);
    return result;
}

static Value parse_primary(Lexer *l, SymTab *st, BasicError *err) {
    if (l->current.type == TOKEN_NUMBER) {
        Value v = value_number(l->current.num_val);
        lexer_next(l);
        return v;
    }

    if (l->current.type == TOKEN_STRING) {
        Value v = value_string_len(l->current.str_val, l->current.str_len);
        lexer_next(l);
        return v;
    }

    if (l->current.type == TOKEN_LPAREN) {
        lexer_next(l);
        Value v = expr_eval(l, st, err);
        if (err && err->code != ERR_OK) return value_nil();
        if (l->current.type != TOKEN_RPAREN) {
            if (err) err->code = ERR_NONSENSE;
            value_free(&v);
            return value_nil();
        }
        lexer_next(l);
        return v;
    }

    if (l->current.type == TOKEN_RND) {
        lexer_next(l);
        // Returns pseudo-random float 0 <= r < 1
        double r = (double)rand() / ((double)RAND_MAX + 1.0);
        return value_number(r);
    }

    if (l->current.type == TOKEN_INKEY_STR) {
        lexer_next(l);
        struct pollfd pfd = { .fd = STDIN_FILENO, .events = POLLIN, .revents = 0 };
        if (poll(&pfd, 1, 0) > 0 && (pfd.revents & POLLIN)) {
            char ch = 0;
            if (read(STDIN_FILENO, &ch, 1) == 1) {
                return value_string_len(&ch, 1);
            }
        }
        return value_string("");
    }

    // Built-in functions requiring parentheses
    switch (l->current.type) {
        case TOKEN_ABS:
        case TOKEN_ACS:
        case TOKEN_ASN:
        case TOKEN_ATN:
        case TOKEN_COS:
        case TOKEN_EXP:
        case TOKEN_INT:
        case TOKEN_LN:
        case TOKEN_SGN:
        case TOKEN_SIN:
        case TOKEN_SQR:
        case TOKEN_TAN:
        case TOKEN_CHR_STR:
        case TOKEN_CODE:
        case TOKEN_LEN:
        case TOKEN_STR_STR:
        case TOKEN_VAL:
            return parse_unary_function(l, st, l->current.type, err);
        default:
            break;
    }

    // Identifiers (Variables, Arrays, Slicing)
    if (l->current.type == TOKEN_IDENT) {
        char *name = strdup(l->current.str_val);
        lexer_next(l);

        // Check if next token is '('
        if (l->current.type == TOKEN_LPAREN) {
            // Could be array access OR string slicing
            // Check if name is in array tables:
            bool is_num_array = false;
            for (size_t i = 0; i < st->num_arrays_count; i++) {
                if (strcmp(st->num_arrays[i].name, name) == 0) {
                    is_num_array = true;
                    break;
                }
            }

            bool is_str_array = false;
            for (size_t i = 0; i < st->str_arrays_count; i++) {
                if (strcmp(st->str_arrays[i].name, name) == 0) {
                    is_str_array = true;
                    break;
                }
            }

            if (is_num_array) {
                lexer_next(l); // consume '('
                size_t indices[MAX_ARRAY_DIMS];
                size_t nidx = 0;
                while (true) {
                    Value idx_val = expr_eval(l, st, err);
                    if (err && err->code != ERR_OK) { free(name); return value_nil(); }
                    if (!value_is_num(&idx_val)) { if (err) err->code = ERR_NONSENSE; free(name); return value_nil(); }
                    if (nidx >= MAX_ARRAY_DIMS) { if (err) err->code = ERR_SUBSCRIPT_RANGE; free(name); return value_nil(); }
                    indices[nidx++] = (size_t)floor(idx_val.as.num);

                    if (l->current.type == TOKEN_COMMA) {
                        lexer_next(l);
                    } else if (l->current.type == TOKEN_RPAREN) {
                        lexer_next(l);
                        break;
                    } else {
                        if (err) err->code = ERR_NONSENSE;
                        free(name);
                        return value_nil();
                    }
                }
                double num_res = 0.0;
                if (!symtab_get_num_array(st, name, nidx, indices, &num_res, err)) {
                    free(name);
                    return value_nil();
                }
                free(name);
                return value_number(num_res);
            }

            if (is_str_array) {
                lexer_next(l); // consume '('
                size_t indices[MAX_ARRAY_DIMS];
                size_t nidx = 0;
                while (true) {
                    Value idx_val = expr_eval(l, st, err);
                    if (err && err->code != ERR_OK) { free(name); return value_nil(); }
                    if (!value_is_num(&idx_val)) { if (err) err->code = ERR_NONSENSE; free(name); return value_nil(); }
                    if (nidx >= MAX_ARRAY_DIMS) { if (err) err->code = ERR_SUBSCRIPT_RANGE; free(name); return value_nil(); }
                    indices[nidx++] = (size_t)floor(idx_val.as.num);

                    if (l->current.type == TOKEN_COMMA) {
                        lexer_next(l);
                    } else if (l->current.type == TOKEN_RPAREN) {
                        lexer_next(l);
                        break;
                    } else {
                        if (err) err->code = ERR_NONSENSE;
                        free(name);
                        return value_nil();
                    }
                }
                Value str_res = value_nil();
                if (!symtab_get_str_array(st, name, nidx, indices, &str_res, err)) {
                    free(name);
                    return value_nil();
                }
                free(name);
                return str_res;
            }

            // Not an array - check if it's a string variable being sliced
            size_t nlen = strlen(name);
            if (nlen > 0 && name[nlen - 1] == '$') {
                const char *s = NULL;
                size_t slen = 0;
                if (!symtab_get_str(st, name, &s, &slen)) {
                    if (err) err->code = ERR_VAR_NOT_FOUND;
                    free(name);
                    return value_nil();
                }
                free(name);
                Value str_val = value_string_len(s, slen);
                return parse_slice_on_string(str_val, l, st, err);
            }

            // Numeric variable cannot be sliced like an array
            if (err) err->code = ERR_VAR_NOT_FOUND;
            free(name);
            return value_nil();
        }

        // Scalar variable
        size_t nlen = strlen(name);
        if (nlen > 0 && name[nlen - 1] == '$') {
            const char *s = NULL;
            size_t slen = 0;
            if (!symtab_get_str(st, name, &s, &slen)) {
                if (err) err->code = ERR_VAR_NOT_FOUND;
                free(name);
                return value_nil();
            }
            free(name);
            return value_string_len(s, slen);
        } else {
            double n = 0.0;
            if (!symtab_get_num(st, name, &n)) {
                if (err) err->code = ERR_VAR_NOT_FOUND;
                free(name);
                return value_nil();
            }
            free(name);
            return value_number(n);
        }
    }

    if (err) err->code = ERR_NONSENSE;
    return value_nil();
}

static Value parse_precedence(Lexer *l, SymTab *st, Precedence prec, BasicError *err) {
    Value left = value_nil();

    // Check prefix operators
    if (l->current.type == TOKEN_PLUS) {
        lexer_next(l);
        Value val = parse_precedence(l, st, PREC_UNARY, err);
        if (err && err->code != ERR_OK) return value_nil();
        left = value_pos(&val, err);
        value_free(&val);
    } else if (l->current.type == TOKEN_MINUS) {
        lexer_next(l);
        Value val = parse_precedence(l, st, PREC_UNARY, err);
        if (err && err->code != ERR_OK) return value_nil();
        left = value_neg(&val, err);
        value_free(&val);
    } else if (l->current.type == TOKEN_NOT) {
        lexer_next(l);
        Value val = parse_precedence(l, st, PREC_NOT, err);
        if (err && err->code != ERR_OK) return value_nil();
        left = value_not(&val, err);
        value_free(&val);
    } else {
        left = parse_primary(l, st, err);
    }

    if (err && err->code != ERR_OK) {
        value_free(&left);
        return value_nil();
    }

    // Infix / Postfix loop
    while (true) {
        Precedence next_prec = get_infix_precedence(l->current.type);
        if (next_prec <= PREC_NONE || next_prec < prec) {
            break;
        }

        TokenType op = l->current.type;

        // Postfix string slicing on expression result, e.g. ("abc" + "def")(2 TO 3)
        if (op == TOKEN_LPAREN) {
            if (value_is_str(&left)) {
                left = parse_slice_on_string(left, l, st, err);
                if (err && err->code != ERR_OK) return value_nil();
                continue;
            } else {
                break;
            }
        }

        lexer_next(l);

        // Right-associative for '^'
        Precedence right_prec = (op == TOKEN_CARET) ? next_prec : (Precedence)(next_prec + 1);
        Value right = parse_precedence(l, st, right_prec, err);
        if (err && err->code != ERR_OK) {
            value_free(&left);
            return value_nil();
        }

        Value result = value_nil();
        switch (op) {
            case TOKEN_PLUS:          result = value_add(&left, &right, err); break;
            case TOKEN_MINUS:         result = value_sub(&left, &right, err); break;
            case TOKEN_STAR:          result = value_mul(&left, &right, err); break;
            case TOKEN_SLASH:         result = value_div(&left, &right, err); break;
            case TOKEN_CARET:         result = value_pow(&left, &right, err); break;
            case TOKEN_EQUAL:         result = value_eq(&left, &right, err); break;
            case TOKEN_NOT_EQUAL:     result = value_ne(&left, &right, err); break;
            case TOKEN_LESS:          result = value_lt(&left, &right, err); break;
            case TOKEN_GREATER:       result = value_gt(&left, &right, err); break;
            case TOKEN_LESS_EQUAL:    result = value_le(&left, &right, err); break;
            case TOKEN_GREATER_EQUAL: result = value_ge(&left, &right, err); break;
            case TOKEN_AND:           result = value_and(&left, &right, err); break;
            case TOKEN_OR:            result = value_or(&left, &right, err); break;
            default:
                if (err) err->code = ERR_NONSENSE;
                break;
        }

        value_free(&left);
        value_free(&right);
        left = result;

        if (err && err->code != ERR_OK) {
            value_free(&left);
            return value_nil();
        }
    }

    return left;
}

Value expr_eval(Lexer *l, SymTab *st, BasicError *err) {
    return parse_precedence(l, st, PREC_NONE, err);
}
