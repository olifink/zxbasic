#define _POSIX_C_SOURCE 200809L

#include "normalize.h"
#include "lexer.h"
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <ctype.h>

static bool is_zx_keyword(const char *upper) {
    static const char *const keywords[] = {
        "LET", "PRINT", "INPUT", "GOTO", "GOSUB", "RETURN", "IF", "THEN",
        "FOR", "TO", "STEP", "NEXT", "DATA", "READ", "RESTORE", "DIM",
        "STOP", "REM", "RUN", "LIST", "NEW", "CLEAR", "CLS", "SAVE",
        "LOAD", "EDIT", "AUTO", "EXIT", "RENUM",
        "INK", "PAPER", "BRIGHT", "INVERSE", "AT",
        "AND", "OR", "NOT",
        "ABS", "ACS", "ASN", "ATN", "COS", "EXP", "INT", "LN", "RND",
        "SGN", "SIN", "SQR", "TAN",
        "CHR$", "CODE", "LEN", "STR$", "VAL", "INKEY$",
        NULL
    };

    for (size_t i = 0; keywords[i] != NULL; i++) {
        if (strcmp(upper, keywords[i]) == 0) {
            return true;
        }
    }
    return false;
}

char *normalize_keywords(const char *source) {
    if (!source) return NULL;
    size_t cap = strlen(source) + 64;
    size_t out_len = 0;
    char *out = malloc(cap);
    if (!out) return NULL;

    size_t i = 0;
    while (source[i] != '\0') {
        char c = source[i];

        // String literal: preserve verbatim
        if (c == '"') {
            if (out_len + 1 >= cap) { cap = cap * 2 + 32; out = realloc(out, cap); }
            out[out_len++] = source[i++];
            while (source[i] != '\0') {
                char sc = source[i++];
                if (out_len + 1 >= cap) { cap = cap * 2 + 32; out = realloc(out, cap); }
                out[out_len++] = sc;
                if (sc == '"') {
                    if (source[i] == '"') {
                        // Escaped quote
                        if (out_len + 1 >= cap) { cap = cap * 2 + 32; out = realloc(out, cap); }
                        out[out_len++] = source[i++];
                    } else {
                        break; // End of string
                    }
                }
            }
            continue;
        }

        // Word (keyword or identifier)
        if (isalpha((unsigned char)c)) {
            size_t start = i;
            while (isalnum((unsigned char)source[i]) || source[i] == '_') {
                i++;
            }
            if (source[i] == '$') {
                i++;
            }
            size_t wlen = i - start;
            char word[128];
            if (wlen < sizeof(word)) {
                memcpy(word, source + start, wlen);
                word[wlen] = '\0';
                char upper[128];
                for (size_t k = 0; k < wlen; k++) {
                    upper[k] = (char)toupper((unsigned char)word[k]);
                }
                upper[wlen] = '\0';

                if (strcmp(upper, "REM") == 0) {
                    // Emit REM in uppercase
                    const char *rem_str = "REM";
                    size_t rlen = 3;
                    while (out_len + rlen >= cap) { cap = cap * 2 + 32; out = realloc(out, cap); }
                    memcpy(out + out_len, rem_str, rlen);
                    out_len += rlen;
                    // Copy remainder of the line verbatim
                    while (source[i] != '\0') {
                        if (out_len + 1 >= cap) { cap = cap * 2 + 32; out = realloc(out, cap); }
                        out[out_len++] = source[i++];
                    }
                    break;
                }

                if (is_zx_keyword(upper)) {
                    while (out_len + wlen >= cap) { cap = cap * 2 + 32; out = realloc(out, cap); }
                    memcpy(out + out_len, upper, wlen);
                    out_len += wlen;
                } else {
                    while (out_len + wlen >= cap) { cap = cap * 2 + 32; out = realloc(out, cap); }
                    memcpy(out + out_len, source + start, wlen);
                    out_len += wlen;
                }
            } else {
                while (out_len + wlen >= cap) { cap = cap * 2 + 32; out = realloc(out, cap); }
                memcpy(out + out_len, source + start, wlen);
                out_len += wlen;
            }
            continue;
        }

        // Other characters
        if (out_len + 1 >= cap) { cap = cap * 2 + 32; out = realloc(out, cap); }
        out[out_len++] = source[i++];
    }

    out[out_len] = '\0';
    return out;
}

typedef enum {
    VPREC_NONE = 0,
    VPREC_OR,
    VPREC_AND,
    VPREC_NOT,
    VPREC_RELATIONAL,
    VPREC_TERM,
    VPREC_FACTOR,
    VPREC_UNARY,
    VPREC_POWER,
    VPREC_POSTFIX
} VPrecedence;

static VPrecedence get_vprec(TokenType t) {
    switch (t) {
        case TOKEN_OR: return VPREC_OR;
        case TOKEN_AND: return VPREC_AND;
        case TOKEN_EQUAL:
        case TOKEN_NOT_EQUAL:
        case TOKEN_LESS:
        case TOKEN_GREATER:
        case TOKEN_LESS_EQUAL:
        case TOKEN_GREATER_EQUAL: return VPREC_RELATIONAL;
        case TOKEN_PLUS:
        case TOKEN_MINUS: return VPREC_TERM;
        case TOKEN_STAR:
        case TOKEN_SLASH: return VPREC_FACTOR;
        case TOKEN_CARET: return VPREC_POWER;
        default: return VPREC_NONE;
    }
}

static bool validate_precedence(Lexer *l, VPrecedence prec, size_t *err_col);

static bool validate_primary(Lexer *l, size_t *err_col) {
    if (l->current.type == TOKEN_NUMBER || l->current.type == TOKEN_STRING) {
        lexer_next(l);
        return true;
    }

    if (l->current.type == TOKEN_LPAREN) {
        lexer_next(l);
        if (!validate_precedence(l, VPREC_NONE, err_col)) return false;
        if (l->current.type != TOKEN_RPAREN) {
            *err_col = l->current.col;
            return false;
        }
        lexer_next(l);
        return true;
    }

    if (l->current.type == TOKEN_RND || l->current.type == TOKEN_INKEY_STR) {
        lexer_next(l);
        return true;
    }

    // Built-in functions with parentheses
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
        case TOKEN_VAL: {
            lexer_next(l);
            if (l->current.type != TOKEN_LPAREN) {
                *err_col = l->current.col;
                return false;
            }
            lexer_next(l);
            if (!validate_precedence(l, VPREC_NONE, err_col)) return false;
            if (l->current.type != TOKEN_RPAREN) {
                *err_col = l->current.col;
                return false;
            }
            lexer_next(l);
            return true;
        }
        default:
            break;
    }

    // Identifiers (variables, arrays, slicing)
    if (l->current.type == TOKEN_IDENT) {
        lexer_next(l);
        if (l->current.type == TOKEN_LPAREN) {
            lexer_next(l);
            if (l->current.type == TOKEN_TO) {
                lexer_next(l);
                if (l->current.type != TOKEN_RPAREN) {
                    if (!validate_precedence(l, VPREC_NONE, err_col)) return false;
                }
            } else {
                if (!validate_precedence(l, VPREC_NONE, err_col)) return false;
                if (l->current.type == TOKEN_TO) {
                    lexer_next(l);
                    if (l->current.type != TOKEN_RPAREN) {
                        if (!validate_precedence(l, VPREC_NONE, err_col)) return false;
                    }
                } else {
                    while (l->current.type == TOKEN_COMMA) {
                        lexer_next(l);
                        if (!validate_precedence(l, VPREC_NONE, err_col)) return false;
                    }
                }
            }

            if (l->current.type != TOKEN_RPAREN) {
                *err_col = l->current.col;
                return false;
            }
            lexer_next(l);
        }
        return true;
    }

    *err_col = l->current.col;
    return false;
}

static bool validate_precedence(Lexer *l, VPrecedence prec, size_t *err_col) {
    if (l->current.type == TOKEN_PLUS || l->current.type == TOKEN_MINUS) {
        lexer_next(l);
        if (!validate_precedence(l, VPREC_UNARY, err_col)) return false;
    } else if (l->current.type == TOKEN_NOT) {
        lexer_next(l);
        if (!validate_precedence(l, VPREC_NOT, err_col)) return false;
    } else {
        if (!validate_primary(l, err_col)) return false;
    }

    while (true) {
        VPrecedence next_prec = get_vprec(l->current.type);
        if (next_prec <= VPREC_NONE || next_prec < prec) {
            break;
        }

        TokenType op = l->current.type;
        lexer_next(l);

        VPrecedence right_prec = (op == TOKEN_CARET) ? next_prec : (VPrecedence)(next_prec + 1);
        if (!validate_precedence(l, right_prec, err_col)) {
            return false;
        }
    }

    return true;
}

static bool validate_statement(Lexer *l, size_t *err_col) {
    TokenType t = l->current.type;

    switch (t) {
        case TOKEN_REM:
            // Consumed till EOF by lexer
            return true;

        case TOKEN_LET: {
            lexer_next(l);
            if (l->current.type != TOKEN_IDENT) {
                *err_col = l->current.col;
                return false;
            }
            lexer_next(l);

            if (l->current.type == TOKEN_LPAREN) {
                lexer_next(l);
                if (l->current.type == TOKEN_TO) {
                    lexer_next(l);
                    if (l->current.type != TOKEN_RPAREN) {
                        if (!validate_precedence(l, VPREC_NONE, err_col)) return false;
                    }
                } else {
                    if (!validate_precedence(l, VPREC_NONE, err_col)) return false;
                    if (l->current.type == TOKEN_TO) {
                        lexer_next(l);
                        if (l->current.type != TOKEN_RPAREN) {
                            if (!validate_precedence(l, VPREC_NONE, err_col)) return false;
                        }
                    } else {
                        while (l->current.type == TOKEN_COMMA) {
                            lexer_next(l);
                            if (!validate_precedence(l, VPREC_NONE, err_col)) return false;
                        }
                    }
                }
                if (l->current.type != TOKEN_RPAREN) {
                    *err_col = l->current.col;
                    return false;
                }
                lexer_next(l);
            }

            if (l->current.type != TOKEN_EQUAL) {
                *err_col = l->current.col;
                return false;
            }
            lexer_next(l);

            return validate_precedence(l, VPREC_NONE, err_col);
        }

        case TOKEN_PRINT: {
            lexer_next(l);
            while (l->current.type != TOKEN_EOF && l->current.type != TOKEN_COLON) {
                if (l->current.type == TOKEN_SEMICOLON || l->current.type == TOKEN_COMMA || l->current.type == TOKEN_APOSTROPHE) {
                    lexer_next(l);
                    continue;
                }
                if (l->current.type == TOKEN_AT) {
                    lexer_next(l);
                    if (!validate_precedence(l, VPREC_NONE, err_col)) return false;
                    if (l->current.type != TOKEN_COMMA) {
                        *err_col = l->current.col;
                        return false;
                    }
                    lexer_next(l);
                    if (!validate_precedence(l, VPREC_NONE, err_col)) return false;
                    continue;
                }
                if (l->current.type == TOKEN_INK || l->current.type == TOKEN_PAPER ||
                    l->current.type == TOKEN_BRIGHT || l->current.type == TOKEN_INVERSE) {
                    lexer_next(l);
                    if (!validate_precedence(l, VPREC_NONE, err_col)) return false;
                    continue;
                }
                if (!validate_precedence(l, VPREC_NONE, err_col)) return false;
                if (l->current.type == TOKEN_SEMICOLON || l->current.type == TOKEN_COMMA || l->current.type == TOKEN_APOSTROPHE) {
                    lexer_next(l);
                }
            }
            return true;
        }

        case TOKEN_INPUT: {
            lexer_next(l);
            while (l->current.type == TOKEN_SEMICOLON || l->current.type == TOKEN_COMMA ||
                   l->current.type == TOKEN_AT || l->current.type == TOKEN_INK ||
                   l->current.type == TOKEN_PAPER || l->current.type == TOKEN_BRIGHT ||
                   l->current.type == TOKEN_INVERSE || l->current.type == TOKEN_STRING) {
                if (l->current.type == TOKEN_SEMICOLON || l->current.type == TOKEN_COMMA) {
                    lexer_next(l);
                    continue;
                }
                if (l->current.type == TOKEN_STRING) {
                    lexer_next(l);
                    continue;
                }
                if (l->current.type == TOKEN_AT) {
                    lexer_next(l);
                    if (!validate_precedence(l, VPREC_NONE, err_col)) return false;
                    if (l->current.type != TOKEN_COMMA) {
                        *err_col = l->current.col;
                        return false;
                    }
                    lexer_next(l);
                    if (!validate_precedence(l, VPREC_NONE, err_col)) return false;
                    continue;
                }
                if (l->current.type == TOKEN_INK || l->current.type == TOKEN_PAPER ||
                    l->current.type == TOKEN_BRIGHT || l->current.type == TOKEN_INVERSE) {
                    lexer_next(l);
                    if (!validate_precedence(l, VPREC_NONE, err_col)) return false;
                    continue;
                }
            }
            if (l->current.type != TOKEN_IDENT) {
                *err_col = l->current.col;
                return false;
            }
            lexer_next(l);
            return true;
        }

        case TOKEN_DIM: {
            lexer_next(l);
            if (l->current.type != TOKEN_IDENT) {
                *err_col = l->current.col;
                return false;
            }
            lexer_next(l);
            if (l->current.type != TOKEN_LPAREN) {
                *err_col = l->current.col;
                return false;
            }
            lexer_next(l);

            while (true) {
                if (!validate_precedence(l, VPREC_NONE, err_col)) return false;
                if (l->current.type == TOKEN_COMMA) {
                    lexer_next(l);
                } else if (l->current.type == TOKEN_RPAREN) {
                    lexer_next(l);
                    break;
                } else {
                    *err_col = l->current.col;
                    return false;
                }
            }
            return true;
        }

        case TOKEN_GOTO:
        case TOKEN_GOSUB:
            lexer_next(l);
            return validate_precedence(l, VPREC_NONE, err_col);

        case TOKEN_RETURN:
        case TOKEN_STOP:
        case TOKEN_CLS:
        case TOKEN_CLEAR:
            lexer_next(l);
            return true;

        case TOKEN_IF: {
            lexer_next(l);
            if (!validate_precedence(l, VPREC_NONE, err_col)) return false;
            if (l->current.type != TOKEN_THEN) {
                *err_col = l->current.col;
                return false;
            }
            lexer_next(l);
            if (l->current.type == TOKEN_NUMBER) {
                lexer_next(l);
                return true;
            }
            return validate_statement(l, err_col);
        }

        case TOKEN_FOR: {
            lexer_next(l);
            if (l->current.type != TOKEN_IDENT) {
                *err_col = l->current.col;
                return false;
            }
            lexer_next(l);
            if (l->current.type != TOKEN_EQUAL) {
                *err_col = l->current.col;
                return false;
            }
            lexer_next(l);
            if (!validate_precedence(l, VPREC_NONE, err_col)) return false;
            if (l->current.type != TOKEN_TO) {
                *err_col = l->current.col;
                return false;
            }
            lexer_next(l);
            if (!validate_precedence(l, VPREC_NONE, err_col)) return false;
            if (l->current.type == TOKEN_STEP) {
                lexer_next(l);
                if (!validate_precedence(l, VPREC_NONE, err_col)) return false;
            }
            return true;
        }

        case TOKEN_NEXT:
            lexer_next(l);
            if (l->current.type != TOKEN_IDENT) {
                *err_col = l->current.col;
                return false;
            }
            lexer_next(l);
            return true;

        case TOKEN_DATA:
            lexer_next(l);
            while (l->current.type != TOKEN_EOF && l->current.type != TOKEN_COLON) {
                lexer_next(l);
            }
            return true;

        case TOKEN_READ:
            lexer_next(l);
            while (true) {
                if (l->current.type != TOKEN_IDENT) {
                    *err_col = l->current.col;
                    return false;
                }
                lexer_next(l);
                if (l->current.type == TOKEN_COMMA) {
                    lexer_next(l);
                } else {
                    break;
                }
            }
            return true;

        case TOKEN_RESTORE:
            lexer_next(l);
            if (l->current.type != TOKEN_EOF && l->current.type != TOKEN_COLON) {
                return validate_precedence(l, VPREC_NONE, err_col);
            }
            return true;

        case TOKEN_EXIT:
            lexer_next(l);
            if (l->current.type != TOKEN_EOF && l->current.type != TOKEN_COLON) {
                return validate_precedence(l, VPREC_NONE, err_col);
            }
            return true;

        // Immediate-only commands are forbidden in stored program lines
        case TOKEN_EDIT:
        case TOKEN_AUTO:
        case TOKEN_RENUM:
            *err_col = l->current.col;
            return false;

        case TOKEN_INK:
        case TOKEN_PAPER:
        case TOKEN_BRIGHT:
        case TOKEN_INVERSE:
            lexer_next(l);
            return validate_precedence(l, VPREC_NONE, err_col);

        case TOKEN_AT:
            // Standalone AT is illegal (allowed only within PRINT and INPUT)
            *err_col = l->current.col;
            return false;

        default:
            *err_col = l->current.col;
            return false;
    }
}

bool syntax_validate_line(const char *source, size_t *err_col) {
    if (!source || !err_col) return false;

    Lexer l;
    lexer_init(&l, source);

    if (l.current.type == TOKEN_UNKNOWN) {
        *err_col = l.current.col;
        token_free(&l.current);
        return false;
    }

    while (l.current.type != TOKEN_EOF) {
        if (!validate_statement(&l, err_col)) {
            token_free(&l.current);
            return false;
        }

        if (l.current.type == TOKEN_COLON) {
            lexer_next(&l);
        } else if (l.current.type != TOKEN_EOF) {
            *err_col = l.current.col;
            token_free(&l.current);
            return false;
        }
    }

    token_free(&l.current);
    return true;
}
